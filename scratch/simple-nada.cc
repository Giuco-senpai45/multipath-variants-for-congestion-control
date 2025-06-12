/*
 * Simple Agg-Path NADA Simulation with Competing Traffic
 *
 * This simulation demonstrates a NADA client that aggregates RTT measurements
 * from two paths and sends packets in round-robin fashion, with competing
 * traffic sources on each path.
 *
 * Topology:
 *                    Path 1
 *   Source -------- Router1 -------- Destination
 *      |               |                 |
 *      |         Competing TCP           |
 *      |         Sources (1-N)           |
 *      |                                 |
 *      +------- Router2 ----------------+
 *                    |         Path 2
 *              Competing TCP
 *              Sources (1-N)
 */

#include "ns3/agg-path-nada.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/video-receiver.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AggregatePathNadaSimulation");

class WebRtcFrameStats
{
  public:
    WebRtcFrameStats()
        : m_keyFramesSent(0),
          m_deltaFramesSent(0),
          m_keyFramesAcked(0),
          m_deltaFramesAcked(0),
          m_keyFramesLost(0),
          m_deltaFramesLost(0)
    {
    }

    void RecordFrameSent(bool isKeyFrame)
    {
        if (isKeyFrame)
        {
            m_keyFramesSent++;
        }
        else
        {
            m_deltaFramesSent++;
        }
    }

    void RecordFrameAcked(bool isKeyFrame)
    {
        try
        {
            if (isKeyFrame)
            {
                m_keyFramesAcked++;
            }
            else
            {
                m_deltaFramesAcked++;
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "EXCEPTION in RecordFrameAcked: " << e.what() << std::endl;
        }
    }

    void RecordFrameLost(bool isKeyFrame)
    {
        if (isKeyFrame)
        {
            m_keyFramesLost++;
        }
        else
        {
            m_deltaFramesLost++;
        }
    }

    void PrintStats() const
    {
        std::cout << "WebRTC Frame Statistics:\n";
        std::cout << "  Key frames sent: " << m_keyFramesSent << "\n";
        std::cout << "  Key frames acked: " << m_keyFramesAcked << "\n";
        std::cout << "  Key frame loss: "
                  << (m_keyFramesSent > 0 ? 100.0 * m_keyFramesLost / m_keyFramesSent : 0) << "%\n";
        std::cout << "  Delta frames sent: " << m_deltaFramesSent << "\n";
        std::cout << "  Delta frames acked: " << m_deltaFramesAcked << "\n";
        std::cout << "  Delta frame loss: "
                  << (m_deltaFramesSent > 0 ? 100.0 * m_deltaFramesLost / m_deltaFramesSent : 0)
                  << "%\n";
    }

  private:
    uint32_t m_keyFramesSent;
    uint32_t m_deltaFramesSent;
    uint32_t m_keyFramesAcked;
    uint32_t m_deltaFramesAcked;
    uint32_t m_keyFramesLost;
    uint32_t m_deltaFramesLost;
};

class Counter : public SimpleRefCount<Counter>
{
  public:
    Counter()
        : count(0)
    {
    }

    uint32_t count;
};

void
ProcessFrameAcknowledgment(WebRtcFrameStats* stats,
                           bool isKeyFrame,
                           Ptr<Counter> packetsSentCounter)
{
    if (!stats)
    {
        std::cout << "ERROR: stats pointer is null" << std::endl;
        return;
    }
    if (!packetsSentCounter)
    {
        std::cout << "ERROR: packetsSentCounter is null" << std::endl;
        return;
    }
    try
    {
        if (packetsSentCounter->count > 0)
        {
            stats->RecordFrameAcked(isKeyFrame);
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "EXCEPTION in ProcessFrameAcknowledgment: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cout << "UNKNOWN EXCEPTION in ProcessFrameAcknowledgment" << std::endl;
    }
}

double
AddJitter(double baseValue, double jitterPercent)
{
    static Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double jitterFactor = 1.0 + rng->GetValue(-jitterPercent, jitterPercent);
    return baseValue * jitterFactor;
}


void
SendAggregateVideoFrame(Ptr<AggregatePathNadaClient> client,
                        uint32_t& frameCount,
                        uint32_t keyFrameInterval,
                        uint32_t frameSize,
                        EventId& frameEvent,
                        Time frameInterval,
                        WebRtcFrameStats& stats,
                        uint32_t& totalPacketsSent,
                        uint32_t maxPackets)
{
    // **ALIGNED: Same packet limit check as strategy-mp**
    if (totalPacketsSent >= maxPackets)
    {
        NS_LOG_INFO("AGG: Reached maximum packets limit (" << maxPackets << "), stopping transmission");
        return;
    }

    if (!client->IsReady())
    {
        NS_LOG_WARN("AGG: Client not ready, rescheduling frame after 100ms");
        frameEvent = Simulator::Schedule(MilliSeconds(100),
                                         &SendAggregateVideoFrame,
                                         client,
                                         std::ref(frameCount),
                                         keyFrameInterval,
                                         frameSize,
                                         std::ref(frameEvent),
                                         frameInterval,
                                         std::ref(stats),
                                         std::ref(totalPacketsSent),
                                         maxPackets);
        return;
    }

    // **ALIGNED: Same frame logic as strategy-mp**
    bool isKeyFrame = (frameCount % keyFrameInterval == 0);
    uint32_t currentFrameSize = isKeyFrame ? frameSize * 2 : frameSize;
    uint32_t mtu = 1500;
    uint32_t numPacketsNeeded = (currentFrameSize + mtu - 1) / mtu;

    NS_LOG_INFO("AGG: Sending " << (isKeyFrame ? "key" : "delta") << " frame #" << frameCount
                << " (size: " << currentFrameSize
                << " bytes, packets: " << numPacketsNeeded << ")");

    // **ALIGNED: Configure video frame properties identically**
    client->SetVideoMode(true);
    client->SetKeyFrameStatus(isKeyFrame);

    DataRate totalRate = client->GetTotalRate();
    double rateMbps = totalRate.GetBitRate() / 1000000.0;

    stats.RecordFrameSent(isKeyFrame);
    Ptr<Counter> packetsSentCounter = Create<Counter>();

    uint32_t packetsToSend = std::min(numPacketsNeeded, maxPackets - totalPacketsSent);

    // **ALIGNED: Same transmission pattern as strategy-mp**
    if (packetsToSend > 0)
    {
        double packetIntervalMs = rateMbps > 1000.0 ? 0.1 : 1.0;

        for (uint32_t i = 0; i < packetsToSend; ++i)
        {
            Time packetDelay = MilliSeconds(i * packetIntervalMs);

            Simulator::Schedule(packetDelay, [client, mtu, packetsSentCounter, isKeyFrame]() {
                Ptr<Packet> packet = Create<Packet>(mtu);
                client->SetKeyFrameStatus(isKeyFrame);
                bool sent = client->Send(packet);
                if (sent && packetsSentCounter)
                {
                    packetsSentCounter->count++;
                }
                NS_LOG_DEBUG("AGG: Scheduled packet sent: " << sent);
            });
        }

        totalPacketsSent += packetsToSend;
    }

    // **ALIGNED: Same acknowledgment processing as strategy-mp**
    WebRtcFrameStats* statsPtr = &stats;
    Time ackDelay = rateMbps > 1000.0 ? MilliSeconds(10) : MilliSeconds(50);

    Simulator::Schedule(ackDelay,
                        &ProcessFrameAcknowledgment,
                        statsPtr,
                        isKeyFrame,
                        packetsSentCounter);

    frameCount++;

    // **ALIGNED: Same frame scheduling logic as strategy-mp**
    Time nextInterval = frameInterval;
    if (rateMbps > 1000.0)
    {
        nextInterval = frameInterval * 0.5;
    }
    else if (rateMbps > 100.0)
    {
        nextInterval = frameInterval * 0.7;
    }

    double jitterPercent = rateMbps > 1000.0 ? 0.001 : 0.005;
    Time jitteredInterval = Seconds(AddJitter(nextInterval.GetSeconds(), jitterPercent));

    NS_LOG_INFO("AGG: Frame " << frameCount << " (" << (isKeyFrame ? "KEY" : "DELTA")
                << ") scheduled - next in " << jitteredInterval.GetMilliSeconds() << "ms");

    // **ALIGNED: Same stopping condition as strategy-mp**
    if (totalPacketsSent < maxPackets)
    {
        frameEvent = Simulator::Schedule(jitteredInterval,
                                         &SendAggregateVideoFrame,
                                         client,
                                         std::ref(frameCount),
                                         keyFrameInterval,
                                         frameSize,
                                         std::ref(frameEvent),
                                         frameInterval,
                                         std::ref(stats),
                                         std::ref(totalPacketsSent),
                                         maxPackets);
    }
    else
    {
        NS_LOG_INFO("AGG: Transmission completed - reached packet limit");
    }
}

int
main(int argc, char* argv[])
{
    // Simulation parameters - matching tcp-simple-nada-webrtc
    uint32_t packetSize = 1000;
    std::string bottleneckBw = "500kbps";
    // Agg-path specific parameters
    std::string dataRate1 = "10Mbps";
    std::string dataRate2 = "5Mbps";
    uint32_t delayMs1 = 20;
    uint32_t delayMs2 = 30;

    uint32_t simulationTime = 60;
    uint32_t maxPackets = 1000;
    bool enableWebRTC = true;
    uint32_t keyFrameInterval = 100;
    uint32_t frameRate = 15;
    bool logDetails = false;
    bool logNada = false;
    uint32_t queueSize = 100;
    std::string queueDisc = "CoDel";
    bool enableAQM = false;
    bool enableAqm = false; // Alternative spelling
    uint32_t maxTotalPackets = 10000;

    uint32_t numCompetingSourcesPathA = 2;
    uint32_t numCompetingSourcesPathB = 2;
    double competingIntensityA = 0.5;
    double competingIntensityB = 0.5;

    // Path selection and buffer parameters (for compatibility)
    uint32_t pathSelectionStrategy = 0;
    double targetBufferLength = 3.0;
    double bufferWeightFactor = 0.3;
    bool enableLogging = false;

    // Parse command line arguments - matching tcp-simple-nada-webrtc exactly
    CommandLine cmd;

    // Basic parameters
    cmd.AddValue("packetSize", "Size of packets to send", packetSize);
    cmd.AddValue("bottleneckBw", "Bottleneck link bandwidth", bottleneckBw);

    // Agg-path parameters
    cmd.AddValue("dataRate1", "Data rate of first path", dataRate1);
    cmd.AddValue("dataRate2", "Data rate of second path", dataRate2);
    cmd.AddValue("delayMs1", "Link delay of first path in milliseconds", delayMs1);
    cmd.AddValue("delayMs2", "Link delay of second path in milliseconds", delayMs2);
    cmd.AddValue("pathSelectionStrategy",
                 "Path selection strategy (ignored in agg-path)",
                 pathSelectionStrategy);

    // Simulation parameters
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("maxPackets", "Maximum number of packets to send", maxPackets);
    cmd.AddValue("webrtc", "Enable WebRTC-like traffic pattern", enableWebRTC);
    cmd.AddValue("keyFrameInterval", "Interval between key frames", keyFrameInterval);
    cmd.AddValue("frameRate", "Video frame rate", frameRate);
    cmd.AddValue("logNada", "Enable detailed NADA logging", logNada);
    cmd.AddValue("logDetails", "Enable detailed logging", logDetails);
    cmd.AddValue("enableLogging", "Enable detailed logging", enableLogging);

    // Queue parameters
    cmd.AddValue("queueSize", "Queue size in packets", queueSize);
    cmd.AddValue("queueDisc", "Queue discipline (CoDel, PfifoFast, PIE)", queueDisc);
    cmd.AddValue("enableAQM", "Enable Active Queue Management", enableAQM);
    cmd.AddValue("enableAqm", "Enable Active Queue Management (alternative)", enableAqm);
    cmd.AddValue("maxTotalPackets",
                 "Maximum total packets to send across the simulation",
                 maxTotalPackets);

    cmd.AddValue("competingSourcesA",
                 "Number of competing sources on path A",
                 numCompetingSourcesPathA);
    cmd.AddValue("competingSourcesB",
                 "Number of competing sources on path B",
                 numCompetingSourcesPathB);
    cmd.AddValue("competingIntensityA",
                 "Traffic intensity of competing sources on path A (0-1)",
                 competingIntensityA);
    cmd.AddValue("competingIntensityB",
                 "Traffic intensity of competing sources on path B (0-1)",
                 competingIntensityB);

    // Buffer parameters (for compatibility)
    cmd.AddValue("targetBufferLength",
                 "Target buffer length in seconds for buffer-aware strategy",
                 targetBufferLength);
    cmd.AddValue("bufferWeightFactor",
                 "Buffer influence factor (0-1) for buffer-aware strategy",
                 bufferWeightFactor);

    cmd.Parse(argc, argv);

    // Merge logging flags
    enableLogging = enableLogging || logDetails || logNada;
    enableAQM = enableAQM || enableAqm;

    // Configure logging
    Time::SetResolution(Time::NS);
    LogComponentEnable("AggregatePathNadaSimulation", LOG_LEVEL_INFO);

    if (enableLogging)
    {
        LogComponentEnable("AggregatePathNadaClient", LOG_LEVEL_DEBUG);
        LogComponentEnable("NadaCongestionControl", LOG_LEVEL_INFO);
    }

    // Configure default TCP settings
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(packetSize));
    Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));

    // Configure queue settings
    Config::SetDefault("ns3::PointToPointNetDevice::TxQueue",
                       StringValue("ns3::DropTailQueue<Packet>"));
    Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize",
                       QueueSizeValue(QueueSize(std::to_string(queueSize) + "p")));

    NS_LOG_INFO("Starting Agg-Path NADA Simulation");
    NS_LOG_INFO("Path 1: " << dataRate1 << ", " << delayMs1 << "ms delay");
    NS_LOG_INFO("Path 2: " << dataRate2 << ", " << delayMs2 << "ms delay");
    NS_LOG_INFO("Packet size: " << packetSize << " bytes");
    NS_LOG_INFO("Max packets: " << maxPackets);
    NS_LOG_INFO("WebRTC mode: " << (enableWebRTC ? "enabled" : "disabled"));
    NS_LOG_INFO("Competing sources - Path A: " << numCompetingSourcesPathA
                                               << ", Path B: " << numCompetingSourcesPathB);

    // Create nodes
    NodeContainer source;
    source.Create(1);

    NodeContainer router1;
    router1.Create(1);

    NodeContainer router2;
    router2.Create(1);

    NodeContainer destination;
    destination.Create(1);

    // Create competing source nodes
    NodeContainer competingSourcesA;
    competingSourcesA.Create(numCompetingSourcesPathA);

    NodeContainer competingSourcesB;
    competingSourcesB.Create(numCompetingSourcesPathB);

    DataRate rate1(dataRate1);
    DataRate rate2(dataRate2);
    if (rate1.GetBitRate() >= 1e9 || rate2.GetBitRate() >= 1e9)
    { // 1Gbps+
        NS_LOG_INFO("Applying high-speed optimizations for " << dataRate1);

        // Reduce logging overhead
        LogComponentDisableAll(LOG_PREFIX_TIME);
        LogComponentDisableAll(LOG_PREFIX_FUNC);

        // Optimize packet size for high-speed links
        if (packetSize < 1500)
        {
            packetSize = 1500;
            NS_LOG_INFO("Increased packet size to " << packetSize << " for efficiency");
        }

        // Reduce queue size to prevent excessive buffering
        queueSize = std::min(queueSize, 200u);

        // For redundant transmission, reduce max packets significantly
        if (pathSelectionStrategy == 3)
        {
            maxPackets = std::min(maxPackets, 10000u);
            NS_LOG_INFO("Reduced maxPackets to " << maxPackets << " for redundant transmission");
        }
    }

    // Create point-to-point links
    PointToPointHelper p2p1;
    p2p1.SetDeviceAttribute("DataRate", StringValue(dataRate1));
    p2p1.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs1)));

    PointToPointHelper p2p2;
    p2p2.SetDeviceAttribute("DataRate", StringValue(dataRate2));
    p2p2.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs2)));

    // Install devices
    // Path 1: Source -> Router1 -> Destination
    NetDeviceContainer devSourceRouter1 = p2p1.Install(source.Get(0), router1.Get(0));
    NetDeviceContainer devRouter1Dest = p2p1.Install(router1.Get(0), destination.Get(0));

    // Path 2: Source -> Router2 -> Destination
    NetDeviceContainer devSourceRouter2 = p2p2.Install(source.Get(0), router2.Get(0));
    NetDeviceContainer devRouter2Dest = p2p2.Install(router2.Get(0), destination.Get(0));

    // Competing sources on Path A (connect to Router1)
    std::vector<NetDeviceContainer> devCompetingA(numCompetingSourcesPathA);
    for (uint32_t i = 0; i < numCompetingSourcesPathA; i++)
    {
        devCompetingA[i] = p2p1.Install(competingSourcesA.Get(i), router1.Get(0));
    }

    // Competing sources on Path B (connect to Router2)
    std::vector<NetDeviceContainer> devCompetingB(numCompetingSourcesPathB);
    for (uint32_t i = 0; i < numCompetingSourcesPathB; i++)
    {
        devCompetingB[i] = p2p2.Install(competingSourcesB.Get(i), router2.Get(0));
    }

    // Install internet stack
    InternetStackHelper internet;
    internet.Install(source);
    internet.Install(router1);
    internet.Install(router2);
    internet.Install(destination);
    internet.Install(competingSourcesA);
    internet.Install(competingSourcesB);

    // Assign IP addresses
    Ipv4AddressHelper ipv4;

    // Path 1 addressing
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcSourceRouter1 = ipv4.Assign(devSourceRouter1);

    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcRouter1Dest = ipv4.Assign(devRouter1Dest);

    // Path 2 addressing
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcSourceRouter2 = ipv4.Assign(devSourceRouter2);

    ipv4.SetBase("10.2.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcRouter2Dest = ipv4.Assign(devRouter2Dest);

    // Competing sources addressing
    std::vector<Ipv4InterfaceContainer> ifcCompetingA(numCompetingSourcesPathA);
    for (uint32_t i = 0; i < numCompetingSourcesPathA; i++)
    {
        std::stringstream ss;
        ss << "10.3." << (i + 1) << ".0";
        ipv4.SetBase(ss.str().c_str(), "255.255.255.0");
        ifcCompetingA[i] = ipv4.Assign(devCompetingA[i]);
    }

    std::vector<Ipv4InterfaceContainer> ifcCompetingB(numCompetingSourcesPathB);
    for (uint32_t i = 0; i < numCompetingSourcesPathB; i++)
    {
        std::stringstream ss;
        ss << "10.4." << (i + 1) << ".0";
        ipv4.SetBase(ss.str().c_str(), "255.255.255.0");
        ifcCompetingB[i] = ipv4.Assign(devCompetingB[i]);
    }

    // Configure AQM if enabled
    if (enableAQM)
    {
        NS_LOG_INFO("Configuring " << queueDisc << " queue discipline");
        TrafficControlHelper tch;

        if (queueDisc == "CoDel")
        {
            Config::SetDefault("ns3::CoDelQueueDisc::Interval", TimeValue(Seconds(0.1)));
            Config::SetDefault("ns3::CoDelQueueDisc::Target", TimeValue(MilliSeconds(5)));
            tch.SetRootQueueDisc("ns3::CoDelQueueDisc");
        }
        else if (queueDisc == "PIE")
        {
            Config::SetDefault("ns3::PieQueueDisc::QueueDelayReference", TimeValue(Seconds(0.02)));
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

        // Install on bottleneck links (router to destination)
        QueueDiscContainer qdiscs;
        qdiscs.Add(tch.Install(router1.Get(0)->GetDevice(1))); // Router1 -> Destination
        qdiscs.Add(tch.Install(router2.Get(0)->GetDevice(1))); // Router2 -> Destination

        NS_LOG_INFO("Installed queue discipline on " << qdiscs.GetN() << " devices");
    }

    // Set up routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Create agg-path NADA client
    AggregatePathNadaClientHelper clientHelper;
    clientHelper.SetAttribute("PacketSize", UintegerValue(packetSize));
    clientHelper.SetAttribute("MaxPackets", UintegerValue(maxPackets));

    ApplicationContainer clientApp = clientHelper.Install(source.Get(0));
    Ptr<AggregatePathNadaClient> aggClient = DynamicCast<AggregatePathNadaClient>(clientApp.Get(0));

    if (!aggClient)
    {
        NS_LOG_ERROR("Failed to create AggregatePathNadaClient");
        return 1;
    }

    // Add both paths to the client
    uint16_t port = 9;

    // Path 1: Source -> Router1 -> Destination
    InetSocketAddress localAddr1(ifcSourceRouter1.GetAddress(0), 0); // Any port
    InetSocketAddress remoteAddr1(ifcRouter1Dest.GetAddress(1), port);
    bool path1Added = aggClient->AddPath(1, localAddr1, remoteAddr1);

    // Path 2: Source -> Router2 -> Destination
    InetSocketAddress localAddr2(ifcSourceRouter2.GetAddress(0), 0); // Any port
    InetSocketAddress remoteAddr2(ifcRouter2Dest.GetAddress(1), port);
    bool path2Added = aggClient->AddPath(2, localAddr2, remoteAddr2);

    if (!path1Added || !path2Added)
    {
        NS_LOG_ERROR("Failed to add paths to agg client");
        return 1;
    }

    NS_LOG_INFO("Added both paths to agg-path client");

    DataRate path1Cap(dataRate1);
    DataRate path2Cap(dataRate2);
    aggClient->SetPathCapacities(path1Cap, path2Cap);
    NS_LOG_INFO("Added both paths to agg-path client with capacities: "
                << path1Cap.GetBitRate() / 1000000.0 << "Mbps, "
                << path2Cap.GetBitRate() / 1000000.0 << "Mbps");

    // Create video receiver at destination (matching tcp-simple-webrtc format)
    VideoReceiverHelper videoReceiver(port);
    videoReceiver.SetAttribute("FrameRate", UintegerValue(frameRate));
    ApplicationContainer serverApp = videoReceiver.Install(destination.Get(0));

    // Create TCP packet sinks for competing traffic
    uint16_t tcpPortA = 50000;
    uint16_t tcpPortB = 50001;

    PacketSinkHelper tcpSinkHelperA("ns3::TcpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), tcpPortA));
    ApplicationContainer tcpSinkAppA = tcpSinkHelperA.Install(destination.Get(0));

    PacketSinkHelper tcpSinkHelperB("ns3::TcpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), tcpPortB));
    ApplicationContainer tcpSinkAppB = tcpSinkHelperB.Install(destination.Get(0));

    uint32_t frameCount = 0;
    Time frameInterval = Seconds(1.0 / frameRate);
    uint32_t totalPacketsSent = 0;
    WebRtcFrameStats frameStats;
    EventId frameEvent;

    if (!aggClient)
    {
        NS_LOG_ERROR("AggregatePathNadaClient creation failed");
        return 1;
    }

    // Start applications
    serverApp.Start(Seconds(0.1));
    serverApp.Stop(Seconds(simulationTime));
    tcpSinkAppA.Start(Seconds(0.5));
    tcpSinkAppA.Stop(Seconds(simulationTime));
    tcpSinkAppB.Start(Seconds(0.5));
    tcpSinkAppB.Stop(Seconds(simulationTime));

    clientApp.Start(Seconds(0.2));
    clientApp.Stop(Seconds(simulationTime - 0.5));

    Simulator::Schedule(Seconds(0.3), [aggClient]() {
        NS_LOG_INFO("Initializing aggregate client");
    });

    Simulator::Schedule(Seconds(0.6), [aggClient]() {
        if (aggClient->IsReady())
        {
            NS_LOG_INFO("AGG Client is ready");
        }
        else
        {
            NS_LOG_WARN("AGG Client not ready yet");
        }
    });

    // Create competing TCP sources on Path A
    std::vector<ApplicationContainer> competingAppsA(numCompetingSourcesPathA);
    for (uint32_t i = 0; i < numCompetingSourcesPathA; i++)
    {
        BulkSendHelper source("ns3::TcpSocketFactory",
                              InetSocketAddress(ifcRouter1Dest.GetAddress(1), tcpPortA));

        // Calculate max bytes based on intensity
        uint32_t maxBytes = static_cast<uint32_t>(competingIntensityA * 10000000); // 10MB base
        source.SetAttribute("MaxBytes", UintegerValue(maxBytes));
        source.SetAttribute("SendSize", UintegerValue(packetSize));

        competingAppsA[i] = source.Install(competingSourcesA.Get(i));

        // Staggered start times
        double startTime = 2.0 + i * 1.1 + AddJitter(0.0, 0.1);
        double stopTime = simulationTime - 2.0 - i * 1.0;

        competingAppsA[i].Start(Seconds(startTime));
        competingAppsA[i].Stop(Seconds(stopTime));

        NS_LOG_INFO("Competing source A-" << i << " starts at " << startTime << "s, stops at "
                                          << stopTime << "s");
    }

    // Create competing TCP sources on Path B
    std::vector<ApplicationContainer> competingAppsB(numCompetingSourcesPathB);
    for (uint32_t i = 0; i < numCompetingSourcesPathB; i++)
    {
        BulkSendHelper source("ns3::TcpSocketFactory",
                              InetSocketAddress(ifcRouter2Dest.GetAddress(1), tcpPortB));

        // Calculate max bytes based on intensity
        uint32_t maxBytes = static_cast<uint32_t>(competingIntensityB * 10000000); // 10MB base
        source.SetAttribute("MaxBytes", UintegerValue(maxBytes));
        source.SetAttribute("SendSize", UintegerValue(packetSize));

        competingAppsB[i] = source.Install(competingSourcesB.Get(i));

        // Staggered start times (offset from Path A)
        double startTime = 7.0 + i * 2.5 + AddJitter(0.0, 0.1);
        double stopTime = simulationTime - 3.0 - i * 0.5;

        competingAppsB[i].Start(Seconds(startTime));
        competingAppsB[i].Stop(Seconds(stopTime));

        NS_LOG_INFO("Competing source B-" << i << " starts at " << startTime << "s, stops at "
                                          << stopTime << "s");
    }

    Time videoStartDelay = Seconds(1.0);

    Simulator::Schedule(videoStartDelay,
                        &SendAggregateVideoFrame,
                        aggClient,
                        std::ref(frameCount),
                        keyFrameInterval,
                        packetSize,
                        std::ref(frameEvent),
                        frameInterval,
                        std::ref(frameStats),
                        std::ref(totalPacketsSent),
                        maxPackets);

    std::cout << "**DEBUG: Frame transmission scheduled successfully" << std::endl;

    NS_LOG_INFO("**CRITICAL: Frame-based video transmission scheduled");
    NS_LOG_INFO("  Frame rate: " << frameRate << " fps");
    NS_LOG_INFO("  Frame interval: " << frameInterval.GetMilliSeconds() << " ms");
    NS_LOG_INFO("  Key frame interval: " << keyFrameInterval << " frames");


    // Set up flow monitor
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();

    // Run simulation
    Simulator::Stop(Seconds(simulationTime));

    NS_LOG_INFO("Running simulation for " << simulationTime << " seconds...");
    Simulator::Run();

    // Process flow monitor statistics
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

    // Print results in tcp-simple-webrtc format
    std::cout << "\n=== SIMULATION RESULTS ===\n";
    std::cout << "Simulation time: " << simulationTime << " seconds\n";
    std::cout << "WebRTC mode: " << (enableWebRTC ? "enabled" : "disabled") << "\n";
    if (enableWebRTC)
    {
        std::cout << "  Frame rate: " << frameRate << " fps\n";
        std::cout << "  Key frame interval: " << keyFrameInterval << " frames\n";
    }
    std::cout << "Bottleneck bandwidth: " << dataRate1 << " (Path 1), " << dataRate2
              << " (Path 2)\n";
    std::cout << "RTT: " << (delayMs1 * 2) << " ms (Path 1), " << (delayMs2 * 2)
              << " ms (Path 2)\n";
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

        // Check if this is the NADA agg-path flow (from source)
        bool isNadaFlow = (t.sourceAddress == ifcSourceRouter1.GetAddress(0) ||
                           t.sourceAddress == ifcSourceRouter2.GetAddress(0));

        // Skip small flows (likely ACKs)
        if (i->second.txPackets < 10)
        {
            continue;
        }

        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
                  << t.destinationAddress << ")";
        if (isNadaFlow && enableWebRTC)
        {
            std::cout << " [WebRTC]";
        }
        std::cout << "\n";

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
            std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / simulationTime / 1000000
                      << " Mbps\n";
        }

        if (i->second.rxPackets > 0)
        {
            std::cout << "  Mean delay: " << i->second.delaySum.GetSeconds() / i->second.rxPackets
                      << " seconds\n";
            if (i->second.rxPackets > 1)
            {
                std::cout << "  Mean jitter: "
                          << i->second.jitterSum.GetSeconds() / (i->second.rxPackets - 1)
                          << " seconds\n";
            }
        }

        if (i->second.txPackets > 0)
        {
            std::cout << "  Packet loss: "
                      << 100.0 * (i->second.txPackets - i->second.rxPackets) / i->second.txPackets
                      << "%\n";
        }

        std::cout << "\n";
    }

    std::cout << "=== OVERALL STATISTICS ===\n";
    std::cout << "Total Tx Packets: " << totalTxPackets << "\n";
    std::cout << "Total Rx Packets: " << totalRxPackets << "\n";
    if (totalTxPackets > 0)
    {
        std::cout << "Overall packet loss: "
                  << 100.0 * (totalTxPackets - totalRxPackets) / totalTxPackets << "%\n";
    }
    std::cout << "Total Tx Bytes: " << totalTxBytes << "\n";
    std::cout << "Total Rx Bytes: " << totalRxBytes << "\n";
    if (totalTxBytes > 0)
    {
        std::cout << "Average network efficiency: " << 100.0 * totalRxBytes / totalTxBytes
                  << "%\n\n";
    }

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

    // Print video receiver statistics (matching tcp-simple-webrtc format)
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
