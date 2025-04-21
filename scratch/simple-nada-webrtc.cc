/*
 * NS-3 Simulation with NADA Congestion Control Algorithm for WebRTC
 * Topology: Source Router -> Intermediate Router (with additional sources) -> Destination Router
 * Using UDP transport with NADA congestion control with WebRTC video frame patterns
 *
 *
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
                                                                  +----------------+
                                                                  |                |
                                                                  | Competing      |
                                                                  | Sources (1-5)  |
                                                                  |                |
                                                                  +----------------+
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
        // Keep track of packets
        m_totalSent++;
        if (lossRate > 0.01)
        {
            m_totalLost++;
        }

        // Store RTT for trends
        if (rtt.IsPositive())
        {
            m_lastRtt = rtt;
        }

        // Detect congestion
        bool wasCongested = m_congested;
        m_congested = (m_totalSent > 30 && (double)m_totalLost / m_totalSent > 0.05) ||
                      (rtt > Seconds(0.2)) || (lossRate > 0.1);

        DataRate newRate = currentRate;

        if (lossRate > 0.2)
        {
            // Severe congestion - drastic reduction
            newRate = DataRate(currentRate.GetBitRate() * 0.5);
            NS_LOG_INFO("Severe congestion! Drastically reducing rate to " << newRate);
            m_totalSent = 0;
            m_totalLost = 0;
        }
        else if (m_congested && !wasCongested)
        {
            // Sharp reduction on congestion onset
            newRate = DataRate(currentRate.GetBitRate() * 0.7);
            NS_LOG_INFO("Congestion detected! Reducing rate to " << newRate);
            m_totalSent = 0;
            m_totalLost = 0;
        }
        else if (m_congested)
        {
            // Continue reducing if still congested - MORE aggressive reduction
            newRate = DataRate(currentRate.GetBitRate() * 0.9);
        }
        else if (m_totalSent > 100 && (double)m_totalLost / m_totalSent < 0.01)
        {
            // Slow increase when conditions are good - MORE conservative increase
            newRate = DataRate(currentRate.GetBitRate() * 1.02);
            m_totalSent = 0;
            m_totalLost = 0;
        }

        // LOWER cap on rate to avoid overwhelming the network
        if (newRate > DataRate("3Mbps"))
        {
            newRate = DataRate("3Mbps");
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
void
SendVideoFrame(Ptr<UdpNadaClient> client,
               uint32_t& frameCount,
               uint32_t keyFrameInterval,
               uint32_t frameSize,
               EventId& frameEvent,
               Time frameInterval,
               CongestionTracker& congestion)
{
    if (!client)
    {
        return;
    }

    // Determine frame type (key frame or delta frame)
    bool isKeyFrame = (frameCount % keyFrameInterval == 0);
    VideoFrameType frameType = isKeyFrame ? KEY_FRAME : DELTA_FRAME;

    // More realistic frame sizes - key frames are larger but not excessive
    uint32_t currentFrameSize = isKeyFrame ? frameSize * 2.5 : frameSize;

    // Get the NADA congestion control object
    Ptr<NadaCongestionControl> nadaCC = client->GetNode()->GetObject<NadaCongestionControl>();

    // Get current network conditions
    DataRate nadaRate = nadaCC ? nadaCC->GetCurrentRate() : DataRate("1Mbps");
    double lossRate = nadaCC ? nadaCC->GetLossRate() : 0;
    Time rtt = nadaCC ? nadaCC->GetBaseDelay() * 2 : Seconds(0.050);

    // Apply additional rate control based on observed congestion
    DataRate sendRate = congestion.UpdateRate(nadaRate, lossRate, rtt);

    // If we're in congestion, reduce frame quality
    if (congestion.IsCongested())
    {
        // More significant reduction during congestion
        currentFrameSize = isKeyFrame ? frameSize * 1.2 : frameSize * 0.7;
        NS_LOG_INFO("Reducing frame size due to congestion to " << currentFrameSize << " bytes");
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

    // Calculate time to transmit frame with proper pacing
    double frameTimeSeconds = std::max(0.002, currentFrameSize * 8.0 / sendRate.GetBitRate());

    // Ensure we don't exceed frame interval with transmission time
    if (frameTimeSeconds > frameInterval.GetSeconds() * 0.8)
    {
        frameTimeSeconds = frameInterval.GetSeconds() * 0.8;
        NS_LOG_INFO("Limiting frame transmission time to " << frameTimeSeconds << " seconds");
    }

    // If congested, spread packets more widely
    if (congestion.IsCongested())
    {
        frameTimeSeconds = std::min(frameInterval.GetSeconds() * 0.9, frameTimeSeconds * 1.5);
    }

    double minPacketSpacing = numPackets > 10 ? 0.010 : 0.005;
    double packetInterval = std::max(minPacketSpacing, frameTimeSeconds / numPackets);

    // Store original packet size
    Ptr<UdpNadaClient> nadaClient = DynamicCast<UdpNadaClient>(client);
    uint32_t originalSize = 0;
    UintegerValue val;
    nadaClient->GetAttribute("PacketSize", val);
    originalSize = val.Get();

    // Send packets with improved pacing
    for (uint32_t i = 0; i < numPackets; i++)
    {
        uint32_t packetSize = std::min(mtu, remainingBytes);
        nadaClient->SetAttribute("PacketSize", UintegerValue(packetSize));

        // Always maintain a minimum spacing between packets (at least 2ms)
        // and add jitter to simulate real networks
        double jitteredInterval = AddJitter(packetInterval, 0.05);
        Time sendTime = Seconds(std::max(i * jitteredInterval, i * minPacketSpacing));

        if (i == 0)
        {
            nadaClient->Send();
        }
        else
        {
            Simulator::Schedule(sendTime, &UdpNadaClient::Send, nadaClient);
        }
        remainingBytes -= packetSize;
    }

    // Restore original packet size
    nadaClient->SetAttribute("PacketSize", UintegerValue(originalSize));

    // Increment frame counter
    frameCount++;

    // Schedule next frame with a slight jitter to avoid synchronization
    Time jitteredFrameInterval = Seconds(AddJitter(frameInterval.GetSeconds(), 0.05));
    frameEvent = Simulator::Schedule(jitteredFrameInterval,
                                     &SendVideoFrame,
                                     client,
                                     frameCount,
                                     keyFrameInterval,
                                     frameSize,
                                     frameEvent,
                                     frameInterval,
                                     std::ref(congestion));
}

int
main(int argc, char* argv[])
{
    uint32_t packetSize = 1000;           // bytes
    std::string dataRate = "1024Mbps";    // Higher access link capacity
    std::string bottleneckBw = "500Mbps"; // Typical home internet upload
    uint32_t delayMs = 25;                // Reduced base delay
    uint32_t simulationTime = 60;         // Simulation time in seconds
    uint32_t numCompetingSources = 2;     // Fewer competing sources
    bool enableWebRTC = true;             // Enable WebRTC by default
    uint32_t keyFrameInterval = 100;      // Less frequent key frames
    uint32_t frameRate = 15;              // More typical frame rate
    bool logNada = false;                 // Enable detailed NADA logging
    uint32_t queueSize = 200;             // Queue size in packets
    std::string queueDisc = "CoDel";      // Queue discipline (CoDel, PfifoFast, PIE)
    bool enableAqm = false;

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of packets to send", packetSize);
    cmd.AddValue("dataRate", "Data rate of primary source", dataRate);
    cmd.AddValue("bottleneckBw", "Bottleneck link bandwidth", bottleneckBw);
    cmd.AddValue("delayMs", "Link delay in milliseconds", delayMs);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("numCompetingSources", "Number of competing traffic sources", numCompetingSources);
    cmd.AddValue("webrtc", "Enable WebRTC-like traffic pattern", enableWebRTC);
    cmd.AddValue("keyFrameInterval", "Interval between key frames", keyFrameInterval);
    cmd.AddValue("frameRate", "Video frame rate", frameRate);
    cmd.AddValue("logNada", "Enable detailed NADA logging", logNada);
    cmd.AddValue("queueSize", "Queue size in packets", queueSize);
    cmd.AddValue("queueDisc", "Queue discipline (CoDel, PfifoFast, PIE)", queueDisc);
    cmd.AddValue("enableAqm", "Enable Active Queue Management", enableAqm);
    cmd.Parse(argc, argv);

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

    NetDeviceContainer additionalToIntermediate[numCompetingSources];
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
    Ipv4InterfaceContainer additionalSourceIfaces[numCompetingSources];
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
    client.SetAttribute("MaxPackets", UintegerValue(0)); // Unlimited

    // Configure NADA parameters for WebRTC behavior
    if (enableWebRTC)
    {
        Ptr<NadaCongestionControl> nadaCC = sourceRouter.Get(0)->GetObject<NadaCongestionControl>();
        if (nadaCC)
        {
            // Enable video mode for WebRTC-like behavior
            nadaCC->SetVideoMode(true);

            // Configure more realistic NADA parameters
            nadaCC->SetMinRate(DataRate("300kbps")); // Minimum usable video rate
            nadaCC->SetMaxRate(DataRate("3Mbps"));   // Maximum rate (avoid overwhelming)
            nadaCC->SetRttMax(MilliSeconds(200));    // Maximum RTT to consider

            // Make NADA more responsive to congestion
            nadaCC->SetXRef(DataRate("2Mbps"));

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
            EventId frameEvent;
            frameEvent = Simulator::Schedule(Seconds(1.5),
                                             &SendVideoFrame,
                                             nadaClient,
                                             frameCount,
                                             keyFrameInterval,
                                             packetSize,
                                             frameEvent,
                                             frameInterval,
                                             std::ref(congestionTracker));

            NS_LOG_INFO("WebRTC traffic generator started - Frame rate: "
                        << frameRate << ", Key frame interval: " << keyFrameInterval);
        }
    }

    // Create additional traffic sources with more reasonable behavior patterns
    UdpClientHelper additionalClient(bottleneckIfaces.GetAddress(1), port);
    additionalClient.SetAttribute("PacketSize", UintegerValue(packetSize));
    additionalClient.SetAttribute("MaxPackets", UintegerValue(0));

    ApplicationContainer additionalApps[numCompetingSources];
    for (uint32_t i = 0; i < numCompetingSources; i++)
    {
        // More reasonable intervals for different rates (less aggressive)
        double interval = 0.008 * (1 + i); // Less aggressive rates
        additionalClient.SetAttribute("Interval", TimeValue(Seconds(interval)));
        additionalApps[i] = additionalClient.Install(additionalSources.Get(i));
        additionalApps[i].Start(Seconds(15.0 + i * 8));          // Start later and more staggered
        additionalApps[i].Stop(Seconds(simulationTime - 5 - i)); // Stop earlier
    }

    // Create UDP receiver at destination
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(destinationRouter.Get(0));
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

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        // Check if this is the NADA flow (from source router)
        bool isNadaFlow = (t.sourceAddress == sourceRouterIfaces.GetAddress(0));

        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
                  << t.destinationAddress << ")" << (isNadaFlow && enableWebRTC ? " [WebRTC]" : "")
                  << "\n";

        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
        std::cout << "  Tx Bytes: " << i->second.txBytes << "\n";
        std::cout << "  Rx Bytes: " << i->second.rxBytes << "\n";

        totalTxPackets += i->second.txPackets;
        totalRxPackets += i->second.rxPackets;
        totalTxBytes += i->second.txBytes;
        totalRxBytes += i->second.rxBytes;

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

    // Print summary statistics
    std::cout << "=== OVERALL STATISTICS ===\n";
    std::cout << "Total Tx Packets: " << totalTxPackets << "\n";
    std::cout << "Total Rx Packets: " << totalRxPackets << "\n";
    std::cout << "Overall packet loss: "
              << 100.0 * (totalTxPackets - totalRxPackets) / totalTxPackets << "%\n";
    std::cout << "Total Tx Bytes: " << totalTxBytes << "\n";
    std::cout << "Total Rx Bytes: " << totalRxBytes << "\n";
    std::cout << "Average network efficiency: " << 100.0 * totalRxBytes / totalTxBytes << "%\n\n";

    Simulator::Destroy();
    return 0;
}
