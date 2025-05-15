/*

                                                +-----------------+
                                                | Destination     |
                                                | Router          |
                                                |                 |
                                                +-----------------+
                                                        ▲
                                                        |
                                                        | Bottleneck Link
                                                        | (bottleneckBw, delayMs*2)
                                                        |
                  +---------------------------+         |         +---------------------------+
                  |                           |         |         |                           |
+-------------+   |                           |         |         |                           |
| Main Source |---| Source                    |---------|-------->| Intermediate              |
| (NADA+WebRTC)|  | Router                    |                   | Router                    |
+-------------+   |                           |                   |                           |
                  |                           |                   |                           |
                  +---------------------------+                   +---------------------------+
                                                                          ▲
                                                                          |
                                                                          |
                                                                  +--------------------+
                                                                  |                    |
                                                                  | Competing          |
                                                                  | Sources TCP (1-5)  |
                                                                  |                    |
                                                                  +--------------------+
                                                                          |
                                                                          |
                                                                          |
                                                                  +----------------+
                                                                  | Data Rates:    |
                                                                  | 2Mbps, 3Mbps,  |
                                                                  | 4Mbps, etc.    |
                                                                  +----------------+
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/nada-header.h"
#include "ns3/nada-improved.h"
#include "ns3/nada-udp-client.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/video-receiver.h"

#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WebRtcNadaSimulation");

// Add jitter to make traffic more realistic
double
AddJitter(double baseValue, double jitterPercent)
{
    // Create a uniform random variable with +/- jitter percent
    static Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double jitterFactor = 1.0 + rng->GetValue(-jitterPercent, jitterPercent);
    return baseValue * jitterFactor;
}

// Track congestion and adapt sending behavior
class CongestionTracker
{
  public:
    CongestionTracker()
        : m_totalSent(0),
          m_totalLost(0),
          m_lastRtt(Seconds(0)),
          m_lastRate(DataRate("1Mbps")),
          m_congested(false)
    {
    }

    DataRate UpdateRate(DataRate currentRate, double lossRate, Time rtt)
    {
        // Track congestion state
        m_lastRtt = rtt;
        m_totalSent += 10; // Increment counter with each update

        if (lossRate > 0.01)
        {
            m_totalLost += static_cast<uint32_t>(lossRate * 10);
            m_congested = true;
        }

        DataRate newRate = currentRate;

        // Very conservative rate adjustments
        if (lossRate > 0.05 || rtt > Seconds(0.2))
        {
            // Heavy congestion - reduce rate significantly
            newRate = DataRate(currentRate.GetBitRate() * 0.5);
            m_congested = true;
        }
        else if (lossRate > 0.01)
        {
            // Moderate congestion - reduce rate
            newRate = DataRate(currentRate.GetBitRate() * 0.8);
            m_congested = true;
        }
        else if (m_totalSent > 100 && (double)m_totalLost / m_totalSent < 0.01)
        {
            // Very conservative increase
            newRate = DataRate(currentRate.GetBitRate() * 1.01);
            m_totalSent = 0;
            m_totalLost = 0;
            m_congested = false;
        }

        // Lower cap on rate
        if (newRate > DataRate("1.5Mbps"))
        {
            newRate = DataRate("1.5Mbps");
        }

        m_lastRate = newRate;
        return newRate;
    }

    bool IsCongested() const
    {
        return m_congested;
    }

  private:
    uint32_t m_totalSent;
    uint32_t m_totalLost;
    Time m_lastRtt;
    DataRate m_lastRate;
    bool m_congested;
};

// Function to send WebRTC video frames with improved pacing
// Replace the SendVideoFrame function with this improved implementation
void
SendVideoFrame(Ptr<UdpNadaClient> client,
               uint32_t& frameCount,
               uint32_t keyFrameInterval,
               uint32_t frameSize,
               EventId& frameEvent,
               Time frameInterval,
               CongestionTracker& congestion,
               uint32_t& totalPacketsSent,
               uint32_t maxPackets)
{
    if (!client || totalPacketsSent >= maxPackets)
    {
        // Stop sending if client is null or max packets reached
        NS_LOG_INFO("Stopping video frame sending");
        return;
    }

    if (totalPacketsSent >= maxPackets && frameEvent.IsPending())
    {
        Simulator::Cancel(frameEvent);
    }

    // Determine frame type (key frame or delta frame)
    bool isKeyFrame = (frameCount % keyFrameInterval == 0);
    VideoFrameType frameType = isKeyFrame ? KEY_FRAME : DELTA_FRAME;

    // Smaller frame sizes
    uint32_t currentFrameSize = isKeyFrame ? frameSize * 1.5 : frameSize;

    // Get current network conditions
    Ptr<NadaCongestionControl> nadaCC = client->GetNode()->GetObject<NadaCongestionControl>();

    // Get current network conditions with fallback values
    DataRate nadaRate = nadaCC ? nadaCC->GetCurrentRate() : DataRate("500kbps");
    double lossRate = nadaCC ? nadaCC->GetLossRate() : 0;
    Time rtt = nadaCC ? nadaCC->GetBaseDelay() * 2 : Seconds(0.050);

    // Apply rate control - limit the data more aggressively
    DataRate sendRate = congestion.UpdateRate(nadaRate, lossRate, rtt);

    // Calculate how many bytes we should send per second at current rate
    double bytesPerSecond = sendRate.GetBitRate() / 8.0;

    // Calculate how many bytes we should send per frame interval
    double targetBytesPerFrame = bytesPerSecond / (1.0 / frameInterval.GetSeconds());

    // Hard cap on frame size - be more restrictive
    if (currentFrameSize > targetBytesPerFrame * 0.8)
    {
        double reductionFactor = targetBytesPerFrame / currentFrameSize;
        // Never reduce below 40% to maintain minimum quality
        reductionFactor = std::max(0.4, reductionFactor * 0.8);
        currentFrameSize = static_cast<uint32_t>(currentFrameSize * reductionFactor);
    }

    // Additional congestion-based reduction
    if (congestion.IsCongested() || lossRate > 0.01)
    {
        double congestionFactor = isKeyFrame ? 0.6 : 0.5;
        currentFrameSize = static_cast<uint32_t>(currentFrameSize * congestionFactor);
    }

    // Set frame parameters in client
    client->SetVideoFrameType(frameType);
    client->SetVideoFrameSize(currentFrameSize);

    NS_LOG_INFO("Frame #" << frameCount << " (" << (isKeyFrame ? "key" : "delta") << ")"
                          << ", size: " << currentFrameSize << " bytes"
                          << ", rate: " << sendRate << ", loss: " << (lossRate * 100) << "%"
                          << ", rtt: " << rtt.GetMilliSeconds() << "ms");

    // Fragment into MTU-sized packets
    uint32_t mtu = 1400; // Allow for headers but reduce fragmentation
    uint32_t remainingBytes = currentFrameSize;
    uint32_t numPackets = (currentFrameSize + mtu - 1) / mtu; // Ceiling division

    // Log the number of packets to be generated
    NS_LOG_INFO("Fragmenting frame into " << numPackets << " packets");

    // Calculate time to transmit frame with proper pacing
    // Use a more conservative approach to avoid overwhelming the network
    double frameTimeSeconds =
        std::min(frameInterval.GetSeconds() * 0.8,              // Use at most 80% of frame interval
                 currentFrameSize * 8.0 / sendRate.GetBitRate() // Theoretical time at current rate
        );

    // Ensure minimum time to avoid bursts
    frameTimeSeconds = std::max(0.005, frameTimeSeconds);

    // Calculate packet interval with minimum spacing constraint
    double minPacketSpacing = 0.002; // 2ms minimum between packets
    double packetInterval = std::max(minPacketSpacing, frameTimeSeconds / numPackets);

    NS_LOG_INFO("Frame transmission time: " << frameTimeSeconds << "s, packet interval: "
                                            << packetInterval * 1000 << "ms");

    // Store original packet size
    Ptr<UdpNadaClient> nadaClient = DynamicCast<UdpNadaClient>(client);
    uint32_t originalSize = 0;
    UintegerValue val;
    nadaClient->GetAttribute("PacketSize", val);
    originalSize = val.Get();

    uint32_t packetsToSend = std::min(numPackets, maxPackets - totalPacketsSent);
    // Send packets with improved pacing
    for (uint32_t i = 0; i < packetsToSend; i++)
    {
        uint32_t packetSize = std::min(mtu, remainingBytes);
        nadaClient->SetAttribute("PacketSize", UintegerValue(packetSize));

        // Add jitter to packet timing for realism
        double jitteredInterval = AddJitter(packetInterval, 0.05);
        Time sendTime = Seconds(std::max(i * jitteredInterval, i * minPacketSpacing));

        if (i == 0)
        {
            nadaClient->Send();
            totalPacketsSent++;
        }
        else
        {
            uint32_t* pCounter = &totalPacketsSent;
            Simulator::Schedule(sendTime, [nadaClient, pCounter, maxPackets](void) {
                if (*pCounter >= maxPackets)
                {
                    return;
                }
                nadaClient->Send();
                (*pCounter)++;
            });
        }
        remainingBytes -= packetSize;
    }

    // Restore original packet size
    nadaClient->SetAttribute("PacketSize", UintegerValue(originalSize));

    // Increment frame counter
    frameCount++;

    // Update NADA with current frame information
    if (nadaCC)
    {
        // Pass frame information to NADA for better rate adaptation
        nadaCC->UpdateVideoFrameInfo(currentFrameSize, isKeyFrame, frameInterval);
    }

    // Schedule next frame with appropriate timing
    // Ensure we don't schedule faster than the configured frame rate
    Time jitteredFrameInterval = Seconds(AddJitter(frameInterval.GetSeconds(), 0.05));
    if (totalPacketsSent < maxPackets)
    {
        frameEvent = Simulator::Schedule(jitteredFrameInterval,
                                         &SendVideoFrame,
                                         client,
                                         frameCount,
                                         keyFrameInterval,
                                         frameSize,
                                         frameEvent,
                                         frameInterval,
                                         std::ref(congestion),
                                         std::ref(totalPacketsSent),
                                         maxPackets);
    }
}

int
main(int argc, char* argv[])
{
    uint32_t packetSize = 1000;           // bytes
    std::string dataRate = "1024Mbps";    // Higher access link capacity
    std::string bottleneckBw = "500Mbps"; // Typical home internet upload
    uint32_t delayMs = 25;                // Reduced base delay

    // Original multipath parameters for comaptibility
    std::string dataRate1 = "1024kbps";
    std::string dataRate2 = "500kbps";
    uint32_t delayMs1 = 20;
    uint32_t delayMs2 = 40;
    uint32_t pathSelectionStrategy = 0;

    uint32_t simulationTime = 60;     // Simulation time in seconds
    uint32_t numCompetingSources = 2; // Fewer competing sources
    bool enableWebRTC = true;         // Enable WebRTC by default
    uint32_t keyFrameInterval = 100;  // Less frequent key frames
    uint32_t frameRate = 15;          // More typical frame rate
    bool logNada = false;             // Enable detailed NADA logging
    uint32_t queueSize = 200;         // Queue size in packets
    std::string queueDisc = "CoDel";  // Queue discipline (CoDel, PfifoFast, PIE)
    bool enableAqm = false;
    uint32_t maxTotalPackets = 10000; // Default target packet count

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of packets to send", packetSize);
    cmd.AddValue("dataRate", "Data rate of primary source", dataRate);
    cmd.AddValue("bottleneckBw", "Bottleneck link bandwidth", bottleneckBw);
    cmd.AddValue("delayMs", "Link delay in milliseconds", delayMs);

    cmd.AddValue("dataRate1", "Data rate of first path (ignored)", dataRate1);
    cmd.AddValue("dataRate2", "Data rate of second path (ignored)", dataRate2);
    cmd.AddValue("delayMs1", "Link delay of first path in milliseconds (ignored)", delayMs1);
    cmd.AddValue("delayMs2", "Link delay of second path in milliseconds (ignored)", delayMs2);
    cmd.AddValue("pathSelection", "Path selection strategy (ignored)", pathSelectionStrategy);

    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("numCompetingSources", "Number of competing traffic sources", numCompetingSources);
    cmd.AddValue("webrtc", "Enable WebRTC-like traffic pattern", enableWebRTC);
    cmd.AddValue("keyFrameInterval", "Interval between key frames", keyFrameInterval);
    cmd.AddValue("frameRate", "Video frame rate", frameRate);
    cmd.AddValue("logNada", "Enable detailed NADA logging", logNada);
    cmd.AddValue("queueSize", "Queue size in packets", queueSize);
    cmd.AddValue("queueDisc", "Queue discipline (CoDel, PfifoFast, PIE)", queueDisc);
    cmd.AddValue("enableAqm", "Enable Active Queue Management", enableAqm);
    cmd.AddValue("maxPackets",
                 "Maximum total packets to send across the simulation",
                 maxTotalPackets);
    cmd.Parse(argc, argv);

    if (dataRate1 != "1024kbps" || dataRate2 != "500kbps" || delayMs1 != 20 || delayMs2 != 40 ||
        pathSelectionStrategy != 0)
    {
        NS_LOG_WARN("Note: Path-specific parameters (dataRate1, dataRate2, delayMs1, delayMs2, "
                    "pathSelection) "
                    "are ignored in single-path simulation");
    }

    // Configure output
    Time::SetResolution(Time::NS);
    LogComponentEnable("WebRtcNadaSimulation", LOG_LEVEL_INFO);

    if (logNada)
    {
        LogComponentEnable("NadaCongestionControl", LOG_LEVEL_INFO);
    }

    if (enableWebRTC)
    {
        NS_LOG_INFO("WebRTC mode enabled with frameRate=" << frameRate << ", keyFrameInterval="
                                                          << keyFrameInterval);
    }

    Config::SetDefault("ns3::PointToPointNetDevice::TxQueue",
                       StringValue("ns3::DropTailQueue<Packet>"));
    Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize",
                       QueueSizeValue(QueueSize(std::to_string(queueSize) + "p")));

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(packetSize));
    Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));

    // Create nodes
    NodeContainer sourceRouter;
    sourceRouter.Create(1);

    NodeContainer intermediateRouter;
    intermediateRouter.Create(1);

    NodeContainer destinationRouter;
    destinationRouter.Create(1);

    NodeContainer additionalSources;
    additionalSources.Create(numCompetingSources);

    // Create links
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(dataRate));
    p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs)));

    // Source to Intermediate Router link
    NetDeviceContainer sourcesToIntermediate =
        p2p.Install(sourceRouter.Get(0), intermediateRouter.Get(0));

    // Create bottleneck link (intermediate to destination)
    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBw));
    bottleneck.SetChannelAttribute(
        "Delay",
        TimeValue(MilliSeconds(delayMs * 2))); // Higher delay on bottleneck
    NetDeviceContainer intermediateToDestination =
        bottleneck.Install(intermediateRouter.Get(0), destinationRouter.Get(0));

    if (enableAqm)
    {
        NS_LOG_INFO("Attempting to install " << queueDisc
                                             << " queue discipline on bottleneck link");
        try
        {
            TrafficControlHelper tch;
            if (queueDisc == "CoDel")
            {
                Config::SetDefault("ns3::CoDelQueueDisc::Interval", TimeValue(Seconds(0.1)));
                Config::SetDefault("ns3::CoDelQueueDisc::Target", TimeValue(MilliSeconds(5)));
                tch.SetRootQueueDisc("ns3::CoDelQueueDisc");
            }
            else if (queueDisc == "PIE")
            {
                Config::SetDefault("ns3::PieQueueDisc::QueueDelayReference",
                                   TimeValue(Seconds(0.02)));
                tch.SetRootQueueDisc("ns3::PieQueueDisc");
            }
            else if (queueDisc == "FqCoDel")
            {
                tch.SetRootQueueDisc("ns3::FqCoDelQueueDisc");
            }
            else
            {
                tch.SetRootQueueDisc("ns3::PfifoFastQueueDisc");
            }

            // Install queue disc on each node with a NetDevice on the bottleneck link
            QueueDiscContainer qdiscs;
            qdiscs.Add(tch.Install(intermediateRouter.Get(0)->GetDevice(1)));
            qdiscs.Add(tch.Install(destinationRouter.Get(0)->GetDevice(0)));

            if (qdiscs.GetN() > 0)
            {
                NS_LOG_INFO("Successfully installed " << queueDisc << " queue discipline on "
                                                      << qdiscs.GetN() << " devices");
            }
            else
            {
                NS_LOG_WARN("Queue discipline installation failed, falling back to default");
            }
        }
        catch (const std::exception& e)
        {
            NS_LOG_ERROR("Exception while installing queue discipline: " << e.what());
            NS_LOG_WARN("Continuing with default queue management");
        }
    }
    else
    {
        NS_LOG_INFO("AQM disabled by command line parameter");
    }

    std::vector<NetDeviceContainer> additionalToIntermediate(numCompetingSources);
    for (uint32_t i = 0; i < numCompetingSources; i++)
    {
        // Create point-to-point connection from additional source to intermediate router
        additionalToIntermediate[i] =
            p2p.Install(additionalSources.Get(i), intermediateRouter.Get(0));
    }

    // Install internet stack on all nodes
    InternetStackHelper internet;
    internet.Install(sourceRouter);
    internet.Install(intermediateRouter);
    internet.Install(destinationRouter);
    internet.Install(additionalSources);

    // Configure IP addresses
    Ipv4AddressHelper ipv4;

    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer sourceRouterIfaces = ipv4.Assign(sourcesToIntermediate);

    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckIfaces = ipv4.Assign(intermediateToDestination);

    // Assign IPs to additional sources
    std::vector<Ipv4InterfaceContainer> additionalSourceIfaces(numCompetingSources);
    for (uint32_t i = 0; i < numCompetingSources; i++)
    {
        std::stringstream ss;
        ss << "10.1." << (i + 3) << ".0";
        ipv4.SetBase(ss.str().c_str(), "255.255.255.0");
        additionalSourceIfaces[i] = ipv4.Assign(additionalToIntermediate[i]);
    }

    // Install NADA congestion control on main source router
    NadaCongestionControlHelper nada;
    nada.Install(sourceRouter.Get(0));

    // Set up routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Create UDP applications on the source router
    uint16_t port = 9;
    Ipv4Address destIp = bottleneckIfaces.GetAddress(1);
    NS_LOG_INFO("Destination IP: " << destIp);

    InetSocketAddress destAddr(bottleneckIfaces.GetAddress(1), port);
    NS_LOG_INFO("Creating destination socket address: " << destAddr.GetIpv4() << ":"
                                                        << destAddr.GetPort());

    UdpNadaClientHelper client(destAddr);
    client.SetAttribute("PacketSize", UintegerValue(packetSize));
    client.SetAttribute("MaxPackets", UintegerValue(maxTotalPackets)); // Unlimited

    // Configure NADA parameters for WebRTC behavior
    if (enableWebRTC)
    {
        Ptr<NadaCongestionControl> nadaCC = sourceRouter.Get(0)->GetObject<NadaCongestionControl>();
        if (nadaCC)
        {
            // Enable video mode for WebRTC-like behavior
            nadaCC->SetVideoMode(true);

            // Configure more realistic NADA parameters
            nadaCC->SetMinRate(DataRate("100kbps")); // Minimum usable video rate
            nadaCC->SetMaxRate(DataRate("1Mbps"));   // Maximum rate (avoid overwhelming)
            nadaCC->SetRttMax(MilliSeconds(200));    // Maximum RTT to consider

            // Make NADA more responsive to congestion
            nadaCC->SetXRef(DataRate("500kbps"));

            NS_LOG_INFO("Video mode enabled in NADA congestion control with optimized parameters");
        }
    }

    ApplicationContainer sourceApps = client.Install(sourceRouter.Get(0));
    sourceApps.Start(Seconds(1.0));
    sourceApps.Stop(Seconds(simulationTime - 1));

    // Configure WebRTC behavior if enabled
    CongestionTracker congestionTracker;

    if (enableWebRTC)
    {
        // Get the NADA client object
        Ptr<UdpNadaClient> nadaClient = DynamicCast<UdpNadaClient>(sourceApps.Get(0));
        if (nadaClient)
        {
            // Enable video mode in the client
            nadaClient->SetVideoMode(true);

            // Initial frame count
            uint32_t frameCount = 0;

            // Calculate frame interval based on frame rate
            Time frameInterval = Seconds(1.0 / frameRate);

            // Create event to send first frame after a brief startup delay
            uint32_t totalPacketsSent = 0;

            EventId frameEvent;
            frameEvent = Simulator::Schedule(Seconds(1.5),
                                             &SendVideoFrame,
                                             nadaClient,
                                             frameCount,
                                             keyFrameInterval,
                                             packetSize,
                                             frameEvent,
                                             frameInterval,
                                             std::ref(congestionTracker),
                                             std::ref(totalPacketsSent),
                                             maxTotalPackets);

            NS_LOG_INFO("WebRTC traffic generator started - Frame rate: "
                        << frameRate << ", Key frame interval: " << keyFrameInterval);
        }
    }

    // Create TCP sink applications at destination for competing TCP traffic
    uint16_t tcpPort = 50000;
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), tcpPort));
    ApplicationContainer sinkApps = sinkHelper.Install(destinationRouter.Get(0));
    sinkApps.Start(Seconds(0.5));
    sinkApps.Stop(Seconds(simulationTime));

    // Create competing TCP bulk send applications instead of UDP clients
    std::vector<ApplicationContainer> competingApps(numCompetingSources);
    for (uint32_t i = 0; i < numCompetingSources; i++)
    {
        // Configure TCP bulk send application
        BulkSendHelper source("ns3::TcpSocketFactory",
                              InetSocketAddress(bottleneckIfaces.GetAddress(1), tcpPort));

        // Configure sending behavior
        source.SetAttribute("MaxBytes", UintegerValue(0)); // Unlimited
        source.SetAttribute("SendSize", UintegerValue(packetSize));

        // Install on competing sources
        competingApps[i] = source.Install(additionalSources.Get(i));

        // Start and stop times (staggered like the original)
        competingApps[i].Start(Seconds(15.0 + i * 8)); // merge si mai putin 2
        competingApps[i].Stop(Seconds(simulationTime - 5 - i));

        NS_LOG_INFO("TCP competing source " << i << " starts at " << (15.0 + i * 8)
                                            << "s and stops at " << (simulationTime - 5 - i)
                                            << "s");
    }

    // Create UDP receiver at destination
    VideoReceiverHelper videoReceiver(port);
    videoReceiver.SetAttribute("FrameRate",
                               UintegerValue(frameRate)); // Set to match your video frame rate
    ApplicationContainer serverApp = videoReceiver.Install(destinationRouter.Get(0));

    serverApp.Start(Seconds(0.5));
    serverApp.Stop(Seconds(simulationTime));

    // Set up flow monitor
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    // Run simulation
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // Process flow monitor statistics
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

    std::cout << "\n=== SIMULATION RESULTS ===\n";
    std::cout << "Simulation time: " << simulationTime << " seconds\n";
    std::cout << "WebRTC mode: " << (enableWebRTC ? "enabled" : "disabled") << "\n";
    if (enableWebRTC)
    {
        std::cout << "  Frame rate: " << frameRate << " fps\n";
        std::cout << "  Key frame interval: " << keyFrameInterval << " frames\n";
    }
    std::cout << "Bottleneck bandwidth: " << bottleneckBw << "\n";
    std::cout << "RTT: " << (delayMs * 4) << " ms\n";
    std::cout << "Queue discipline: " << queueDisc << "\n";
    std::cout << "Queue size: " << queueSize << " packets\n\n";

    // Track totals for summary
    uint32_t totalTxPackets = 0;
    uint32_t totalRxPackets = 0;
    uint64_t totalTxBytes = 0;
    uint64_t totalRxBytes = 0;

    uint32_t nadaTxPackets = 0;
    uint32_t nadaRxPackets = 0;
    uint64_t nadaTxBytes = 0;
    uint64_t nadaRxBytes = 0;

    uint32_t tcpTxPackets = 0;
    uint32_t tcpRxPackets = 0;
    uint64_t tcpTxBytes = 0;
    uint64_t tcpRxBytes = 0;

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        // Check if this is the NADA flow (from source router)
        bool isNadaFlow = (t.sourceAddress == sourceRouterIfaces.GetAddress(0));

        bool isAck = (t.sourceAddress == bottleneckIfaces.GetAddress(1) ||
                      (t.protocol == 6 && t.sourcePort == tcpPort)); // TCP protocol and server port

        // Skip ACK flows
        if (isAck)
        {
            continue;
        }

        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
                  << t.destinationAddress << ")" << (isNadaFlow && enableWebRTC ? " [WebRTC]" : "")
                  << "\n";

        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
        std::cout << "  Tx Bytes: " << i->second.txBytes << "\n";
        std::cout << "  Rx Bytes: " << i->second.rxBytes << "\n";

        // Track overall statistics
        totalTxPackets += i->second.txPackets;
        totalRxPackets += i->second.rxPackets;
        totalTxBytes += i->second.txBytes;
        totalRxBytes += i->second.rxBytes;

        // Track per-protocol statistics
        if (isNadaFlow)
        {
            nadaTxPackets += i->second.txPackets;
            nadaRxPackets += i->second.rxPackets;
            nadaTxBytes += i->second.txBytes;
            nadaRxBytes += i->second.rxBytes;
        }
        else
        {
            // Must be TCP competing flow
            tcpTxPackets += i->second.txPackets;
            tcpRxPackets += i->second.rxPackets;
            tcpTxBytes += i->second.txBytes;
            tcpRxBytes += i->second.rxBytes;
        }

        double duration =
            i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds();

        if (duration > 0)
        {
            std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / duration / 1000000
                      << " Mbps\n";
        }
        else
        {
            std::cout << "  Throughput: N/A (duration too short)\n";
        }

        if (i->second.rxPackets > 0)
        {
            std::cout << "  Mean delay: " << i->second.delaySum.GetSeconds() / i->second.rxPackets
                      << " seconds\n";
            std::cout << "  Mean jitter: "
                      << i->second.jitterSum.GetSeconds() / (i->second.rxPackets - 1)
                      << " seconds\n";
        }
        else
        {
            std::cout << "  Mean delay: N/A (no received packets)\n";
            std::cout << "  Mean jitter: N/A (no received packets)\n";
        }

        if (i->second.txPackets > 0)
        {
            std::cout << "  Packet loss: "
                      << 100.0 * (i->second.txPackets - i->second.rxPackets) / i->second.txPackets
                      << "%\n";
        }
        else
        {
            std::cout << "  Packet loss: N/A (no transmitted packets)\n";
        }

        std::cout << "\n";
    }

    std::cout << "=== OVERALL STATISTICS ===\n";
    std::cout << "Total Tx Packets: " << totalTxPackets << "\n";
    std::cout << "Total Rx Packets: " << totalRxPackets << "\n";
    std::cout << "Overall packet loss: "
              << 100.0 * (totalTxPackets - totalRxPackets) / totalTxPackets << "%\n";
    std::cout << "Total Tx Bytes: " << totalTxBytes << "\n";
    std::cout << "Total Rx Bytes: " << totalRxBytes << "\n";
    std::cout << "Average network efficiency: " << 100.0 * totalRxBytes / totalTxBytes << "%\n\n";

    // Print NADA/WebRTC specific statistics
    std::cout << "=== NADA/WebRTC STATISTICS ===\n";
    std::cout << "NADA Tx Packets: " << nadaTxPackets << "\n";
    std::cout << "NADA Rx Packets: " << nadaRxPackets << "\n";
    if (nadaTxPackets > 0)
    {
        std::cout << "NADA packet loss: " << 100.0 * (nadaTxPackets - nadaRxPackets) / nadaTxPackets
                  << "%\n";
    }
    std::cout << "NADA Tx Bytes: " << nadaTxBytes << "\n";
    std::cout << "NADA Rx Bytes: " << nadaRxBytes << "\n";
    if (nadaTxBytes > 0)
    {
        std::cout << "NADA efficiency: " << 100.0 * nadaRxBytes / nadaTxBytes << "%\n";
    }

    // Print TCP specific statistics
    std::cout << "\n=== TCP COMPETING FLOWS STATISTICS ===\n";
    std::cout << "TCP Tx Packets: " << tcpTxPackets << "\n";
    std::cout << "TCP Rx Packets: " << tcpRxPackets << "\n";
    if (tcpTxPackets > 0)
    {
        std::cout << "TCP packet loss: " << 100.0 * (tcpTxPackets - tcpRxPackets) / tcpTxPackets
                  << "%\n";
    }
    std::cout << "TCP Tx Bytes: " << tcpTxBytes << "\n";
    std::cout << "TCP Rx Bytes: " << tcpRxBytes << "\n";
    if (tcpTxBytes > 0)
    {
        std::cout << "TCP efficiency: " << 100.0 * tcpRxBytes / tcpTxBytes << "%\n";
    }

    Ptr<VideoReceiver> videoReceiverApp = DynamicCast<VideoReceiver>(serverApp.Get(0));
    if (videoReceiverApp)
    {
        std::cout << "\n=== VIDEO RECEIVER STATISTICS ===\n";
        std::cout << videoReceiverApp->GetBufferStats();
        std::cout << "Buffer underruns: " << videoReceiverApp->GetBufferUnderruns() << "\n";
        std::cout << "Average buffer length: " << videoReceiverApp->GetAverageBufferLength()
                  << " ms\n";
    }
    std::cout << "\n";


    Simulator::Destroy();
    return 0;
}
