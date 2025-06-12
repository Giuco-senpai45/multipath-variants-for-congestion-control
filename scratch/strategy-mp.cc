/*
 * NS-3 Simulation with Multipath NADA Congestion Control for WebRTC with Competing Traffic
 *
 * Topology:
 *                                  +----------------+
 *                              /---|    RouterA     |---\
 *                             /    +----------------+    \
 *                            /        ^                   \
 *                           /         |                    \
 *  +-----------------+     /    +----------------+      +----------------+
 *  |                 |----/     | Competing      |      |                |
 *  |  Source Node    |          | Sources A (1-N)|      |  Destination   |
 *  | (NADA WebRTC)   |          +----------------+      |  Node          |
 *  |                 |----\                             |                |
 *  +-----------------+     \    +----------------+      +----------------+
 *                           \   | Competing      |     /
 *                            \  | Sources B (1-N)|    /
 *                             \ +----------------+   /
 *                              \                    /
 *                               \-+----------------+
 *                                |    RouterB      |
 *                                +-----------------+
 *
 * This simulation creates a topology with multiple paths between source and destination
 * and adds competing traffic sources on each path to simulate realistic network conditions.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mp-factory.h"
#include "ns3/mp-nada-base.h"
#include "ns3/mp-nada-client.h"
#include "ns3/nada-header.h"
#include "ns3/nada-improved.h"
#include "ns3/nada-udp-client.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/video-receiver.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultipathCompetingWebRtcSimulation");

class Counter : public SimpleRefCount<Counter>
{
  public:
    Counter()
        : count(0)
    {
    }

    // **ADD: Thread-safe increment**
    void Increment()
    {
        count++;
    }

    // **ADD: Safe getter**
    uint32_t GetCount() const
    {
        return count;
    }

    uint32_t count;
};

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

double
AddJitter(double baseValue, double jitterPercent)
{
    static Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double jitterFactor = 1.0 + rng->GetValue(-jitterPercent, jitterPercent);
    return baseValue * jitterFactor;
}

void
ProcessFrameAcknowledgment(WebRtcFrameStats* stats,
                           bool isKeyFrame,
                           Ptr<Counter> packetsSentCounter)
{
    if (!stats)
    {
        NS_LOG_ERROR("ProcessFrameAcknowledgment: stats pointer is null");
        return;
    }

    if (!packetsSentCounter)
    {
        NS_LOG_ERROR("ProcessFrameAcknowledgment: packetsSentCounter is null");
        return;
    }

    try
    {
        uint32_t count = packetsSentCounter->GetCount();
        NS_LOG_DEBUG("Processing frame acknowledgment with " << count << " packets");

        if (count > 0 && count < 1000000) // Sanity check to detect corruption
        {
            stats->RecordFrameAcked(isKeyFrame);
        }
        else
        {
            NS_LOG_ERROR("Detected corrupted packet count: " << count);
        }
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception in ProcessFrameAcknowledgment: " << e.what());
    }
    catch (...)
    {
        NS_LOG_ERROR("Unknown exception in ProcessFrameAcknowledgment");
    }
}

bool
ValidateClientSockets(Ptr<MultiPathNadaClient> client)
{
    if (!client)
    {
        return false;
    }

    // **FIX: Lightweight validation only**
    static bool hasValidatedOnce = false;
    if (hasValidatedOnce)
    {
        // Skip heavy validation after first success
        return client->IsReady();
    }

    bool isReady = client->IsReady();
    if (isReady)
    {
        hasValidatedOnce = true;
        NS_LOG_INFO("Client validated successfully - skipping future heavy validation");
    }

    return isReady;
}

void
SendMultipathVideoFrame(Ptr<MultiPathNadaClientBase> client,
                        uint32_t& frameCount,
                        uint32_t keyFrameInterval,
                        uint32_t frameSize,
                        EventId& frameEvent,
                        Time frameInterval,
                        WebRtcFrameStats& stats,
                        uint32_t& totalPacketsSent,
                        uint32_t maxPackets)
{
    // Check if we've reached the packet limit
    if (totalPacketsSent >= maxPackets)
    {
        NS_LOG_INFO("MP: Reached maximum packets limit (" << maxPackets << "), stopping transmission");
        return;
    }

    if (!client->IsReady())
    {
        NS_LOG_WARN("MP: Client not ready, rescheduling frame after 100ms");
        frameEvent = Simulator::Schedule(MilliSeconds(100),
                                         &SendMultipathVideoFrame,
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

    // **ALIGNED: Use exact same frame logic as simple-nada**
    bool isKeyFrame = (frameCount % keyFrameInterval == 0);
    uint32_t currentFrameSize = isKeyFrame ? frameSize * 2 : frameSize;
    uint32_t mtu = 1500;
    uint32_t numPacketsNeeded = (currentFrameSize + mtu - 1) / mtu;

    NS_LOG_INFO("MP: Sending " << (isKeyFrame ? "key" : "delta") << " frame #" << frameCount
                << " (size: " << currentFrameSize
                << " bytes, packets: " << numPacketsNeeded << ")"
                << " using strategy: " << client->GetStrategyName());

    // **ALIGNED: Configure video frame properties identically**
    client->SetVideoMode(true);
    client->SetKeyFrameStatus(isKeyFrame);
    client->SetPacketSize(mtu);

    DataRate totalRate = client->GetTotalRate();
    double rateMbps = totalRate.GetBitRate() / 1000000.0;

    stats.RecordFrameSent(isKeyFrame);
    Ptr<Counter> packetsSentCounter = Create<Counter>();

    uint32_t packetsToSend = std::min(numPacketsNeeded, maxPackets - totalPacketsSent);

    // **ALIGNED: Use same transmission pattern as simple-nada**
    if (packetsToSend > 0)
    {
        double packetIntervalMs = rateMbps > 1000.0 ? 0.1 : 1.0;

        for (uint32_t i = 0; i < packetsToSend; ++i)
        {
            Time packetDelay = MilliSeconds(i * packetIntervalMs);

            // **ALIGNED: Same lambda capture pattern as simple-nada**
            Simulator::Schedule(packetDelay, [client, mtu, packetsSentCounter, isKeyFrame]() {
                Ptr<Packet> packet = Create<Packet>(mtu);
                client->SetKeyFrameStatus(isKeyFrame);
                bool sent = client->Send(packet);
                if (sent && packetsSentCounter)
                {
                    packetsSentCounter->count++;
                }
                NS_LOG_DEBUG("MP: Scheduled packet sent: " << sent);
            });
        }

        // **ALIGNED: Update totalPacketsSent immediately**
        totalPacketsSent += packetsToSend;
    }

    // **ALIGNED: Same acknowledgment processing as simple-nada**
    WebRtcFrameStats* statsPtr = &stats;
    Time ackDelay = rateMbps > 1000.0 ? MilliSeconds(10) : MilliSeconds(50);

    Simulator::Schedule(ackDelay,
                        &ProcessFrameAcknowledgment,
                        statsPtr,
                        isKeyFrame,
                        packetsSentCounter);

    frameCount++;

    // **ALIGNED: Same frame scheduling logic as simple-nada**
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

    NS_LOG_INFO("MP: Frame " << frameCount << " (" << (isKeyFrame ? "KEY" : "DELTA")
                << ") scheduled - next in " << jitteredInterval.GetMilliSeconds() << "ms"
                << " (Strategy: " << client->GetStrategyName() << ")");

    // **ALIGNED: Same stopping condition as simple-nada**
    if (totalPacketsSent < maxPackets)
    {
        frameEvent = Simulator::Schedule(jitteredInterval,
                                         &SendMultipathVideoFrame,
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
        NS_LOG_INFO("MP: Transmission completed - reached packet limit");
    }
}

int
main(int argc, char* argv[])
{
    // Simulation parameters
    uint32_t packetSize = 1000;
    std::string bottleneckBw = "500Mbps";
    // Multipath parameters
    std::string dataRate1 = "10Mbps";
    std::string dataRate2 = "5Mbps";
    uint32_t delayMs1 = 20;
    uint32_t delayMs2 = 30;

    uint32_t simulationTime = 60;
    uint32_t keyFrameInterval = 100;
    uint32_t frameRate = 15;
    bool logDetails = false;
    uint32_t maxPackets = 1000;
    uint32_t queueSize = 100;
    std::string queueDisc = "CoDel";
    uint32_t pathSelectionStrategy =
        0; // 0=weighted, 1=best, 2=equal, 3=redundant, 4=frame-aware, 5=buffer-aware

    uint32_t numCompetingSourcesPathA = 2;
    uint32_t numCompetingSourcesPathB = 2;
    double competingIntensityA = 0.5;
    double competingIntensityB = 0.5;
    bool enableAQM = false;

    double targetBufferLength = 3.0;
    double bufferWeightFactor = 0.3;

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("bottleneckBw", "Bandwidth for bottleneck link", bottleneckBw);
    cmd.AddValue("packetSize", "Size of packets to send", packetSize);
    cmd.AddValue("dataRate1", "Data rate of first path", dataRate1);
    cmd.AddValue("dataRate2", "Data rate of second path", dataRate2);
    cmd.AddValue("delayMs1", "Link delay of first path in milliseconds", delayMs1);
    cmd.AddValue("delayMs2", "Link delay of second path in milliseconds", delayMs2);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("keyFrameInterval", "Interval between key frames", keyFrameInterval);
    cmd.AddValue("frameRate", "Video frame rate", frameRate);
    cmd.AddValue("logDetails", "Enable detailed logging", logDetails);
    cmd.AddValue("maxPackets", "Maximum packets to send", maxPackets);
    cmd.AddValue("pathSelectionStrategy",
                 "Path selection strategy:\n"
                 "  0=WEIGHTED (dynamic quality-based weighting)\n"
                 "  1=BEST_PATH (concentrate on best performing path)\n"
                 "  2=EQUAL (round-robin distribution)\n"
                 "  3=REDUNDANT (send on all paths simultaneously)\n"
                 "  4=FRAME_AWARE (key frames on reliable paths, delta frames distributed)\n"
                 "  5=BUFFER_AWARE (adjust based on video buffer status)",
                 pathSelectionStrategy);
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
    cmd.AddValue("enableAQM", "Enable Active Queue Management", enableAQM);
    cmd.AddValue("targetBufferLength",
                 "Target buffer length in seconds for buffer-aware strategy",
                 targetBufferLength);
    cmd.AddValue("bufferWeightFactor",
                 "Buffer influence factor (0-1) for buffer-aware strategy",
                 bufferWeightFactor);
    cmd.Parse(argc, argv);

    // Configure logging
    Time::SetResolution(Time::NS);
    LogComponentEnable("MultipathCompetingWebRtcSimulation", LOG_LEVEL_INFO);

    NS_LOG_INFO("Starting simulation with parameters:");
    NS_LOG_INFO("  packetSize: " << packetSize);
    NS_LOG_INFO("  dataRate1: " << dataRate1 << ", dataRate2: " << dataRate2);
    NS_LOG_INFO("  delayMs1: " << delayMs1 << ", delayMs2: " << delayMs2);
    NS_LOG_INFO("  simulationTime: " << simulationTime);
    NS_LOG_INFO("  pathSelectionStrategy: " << pathSelectionStrategy);
    NS_LOG_INFO("  competingSourcesA: " << numCompetingSourcesPathA);
    NS_LOG_INFO("  competingSourcesB: " << numCompetingSourcesPathB);

    std::cout << "**STRATEGY-MP CONFIGURATION:**" << std::endl;
    std::cout << "  Frame rate: " << frameRate << " fps" << std::endl;
    std::cout << "  Key frame interval: " << keyFrameInterval << " frames" << std::endl;
    std::cout << "  Frame size: " << packetSize << " bytes" << std::endl;
    std::cout << "  Max packets: " << maxPackets << std::endl;
    std::cout << "  Expected frames in " << simulationTime << "s: " << (frameRate * simulationTime) << std::endl;

    // Configure TCP options for competing sources
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(packetSize));
    Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));

    if (logDetails)
    {
        LogComponentEnable("MultiPathNadaClientBase", LOG_LEVEL_INFO);
        LogComponentEnable("MultiPathNadaClientFactory", LOG_LEVEL_INFO);
        LogComponentEnable("NadaCongestionControl", LOG_LEVEL_INFO);
    }

    // Set up queue configuration
    Config::SetDefault("ns3::PointToPointNetDevice::TxQueue",
                       StringValue("ns3::DropTailQueue<Packet>"));
    Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize",
                       QueueSizeValue(QueueSize(std::to_string(queueSize) + "p")));

    // Configure AQM if enabled
    if (enableAQM)
    {
        if (queueDisc == "CoDel")
        {
            Config::SetDefault("ns3::CoDelQueueDisc::Interval", TimeValue(Seconds(0.1)));
            Config::SetDefault("ns3::CoDelQueueDisc::Target", TimeValue(MilliSeconds(5)));
        }
        else if (queueDisc == "PIE")
        {
            Config::SetDefault("ns3::PieQueueDisc::QueueDelayReference", TimeValue(Seconds(0.02)));
        }
    }

    NS_LOG_INFO("Creating nodes");
    // Create nodes
    NodeContainer source;
    source.Create(1);

    NodeContainer routerA;
    routerA.Create(1);

    NodeContainer routerB;
    routerB.Create(1);

    NodeContainer destination;
    destination.Create(1);

    // Create competing source nodes
    NodeContainer competingSourcesA;
    competingSourcesA.Create(numCompetingSourcesPathA);

    NodeContainer competingSourcesB;
    competingSourcesB.Create(numCompetingSourcesPathB);

    NS_LOG_INFO("Setting up network links");
    // Create the two path links
    PointToPointHelper p2p1;
    p2p1.SetDeviceAttribute("DataRate", StringValue(dataRate1));
    p2p1.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs1)));

    PointToPointHelper p2p2;
    p2p2.SetDeviceAttribute("DataRate", StringValue(dataRate2));
    p2p2.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs2)));

    NS_LOG_INFO("Installing network devices for path 1");
    // Install devices - Path 1: Source -> RouterA -> Destination
    NetDeviceContainer devSourceToRouterA = p2p1.Install(source.Get(0), routerA.Get(0));
    NetDeviceContainer devRouterAToDestination = p2p1.Install(routerA.Get(0), destination.Get(0));

    NS_LOG_INFO("Installing network devices for path 2");
    // Install devices - Path 2: Source -> RouterB -> Destination
    NetDeviceContainer devSourceToRouterB = p2p2.Install(source.Get(0), routerB.Get(0));
    NetDeviceContainer devRouterBToDestination = p2p2.Install(routerB.Get(0), destination.Get(0));

    // Install devices for competing sources on path A
    std::vector<NetDeviceContainer> devCompetingToRouterA;
    for (uint32_t i = 0; i < numCompetingSourcesPathA; i++)
    {
        NetDeviceContainer dev = p2p1.Install(competingSourcesA.Get(i), routerA.Get(0));
        devCompetingToRouterA.push_back(dev);
    }

    // Install devices for competing sources on path B
    std::vector<NetDeviceContainer> devCompetingToRouterB;
    for (uint32_t i = 0; i < numCompetingSourcesPathB; i++)
    {
        NetDeviceContainer dev = p2p2.Install(competingSourcesB.Get(i), routerB.Get(0));
        devCompetingToRouterB.push_back(dev);
    }

    // Enable tracing
    AsciiTraceHelper ascii;
    p2p1.EnableAsciiAll(ascii.CreateFileStream("mp-competing-path1-trace.tr"));
    p2p2.EnableAsciiAll(ascii.CreateFileStream("mp-competing-path2-trace.tr"));

    NS_LOG_INFO("Installing internet stack on nodes");
    // Install internet stack
    InternetStackHelper internet;
    internet.Install(source);
    internet.Install(routerA);
    internet.Install(routerB);
    internet.Install(destination);
    internet.Install(competingSourcesA);
    internet.Install(competingSourcesB);

    NS_LOG_INFO("Configuring IP addresses");
    // Configure IP addresses
    Ipv4AddressHelper ipv4;

    // Path 1 IP addressing
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcSourceRouterA = ipv4.Assign(devSourceToRouterA);
    NS_LOG_INFO("Path 1 first segment: " << ifcSourceRouterA.GetAddress(0) << " -> "
                                         << ifcSourceRouterA.GetAddress(1));

    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcRouterADestination = ipv4.Assign(devRouterAToDestination);
    NS_LOG_INFO("Path 1 second segment: " << ifcRouterADestination.GetAddress(0) << " -> "
                                          << ifcRouterADestination.GetAddress(1));

    // Path 2 IP addressing
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcSourceRouterB = ipv4.Assign(devSourceToRouterB);
    NS_LOG_INFO("Path 2 first segment: " << ifcSourceRouterB.GetAddress(0) << " -> "
                                         << ifcSourceRouterB.GetAddress(1));

    ipv4.SetBase("10.2.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcRouterBDestination = ipv4.Assign(devRouterBToDestination);
    NS_LOG_INFO("Path 2 second segment: " << ifcRouterBDestination.GetAddress(0) << " -> "
                                          << ifcRouterBDestination.GetAddress(1));

    // Assign IPs to competing sources on path A
    std::vector<Ipv4InterfaceContainer> ifcCompetingSourcesA;
    for (uint32_t i = 0; i < numCompetingSourcesPathA; i++)
    {
        std::stringstream subnet;
        subnet << "10.11." << (i + 1) << ".0";
        ipv4.SetBase(subnet.str().c_str(), "255.255.255.0");
        Ipv4InterfaceContainer ifc = ipv4.Assign(devCompetingToRouterA[i]);
        ifcCompetingSourcesA.push_back(ifc);
        NS_LOG_INFO("Competing source A" << i << ": " << ifc.GetAddress(0));
    }

    // Assign IPs to competing sources on path B
    std::vector<Ipv4InterfaceContainer> ifcCompetingSourcesB;
    for (uint32_t i = 0; i < numCompetingSourcesPathB; i++)
    {
        std::stringstream subnet;
        subnet << "10.21." << (i + 1) << ".0";
        ipv4.SetBase(subnet.str().c_str(), "255.255.255.0");
        Ipv4InterfaceContainer ifc = ipv4.Assign(devCompetingToRouterB[i]);
        ifcCompetingSourcesB.push_back(ifc);
        NS_LOG_INFO("Competing source B" << i << ": " << ifc.GetAddress(0));
    }

    // Apply AQM if enabled
    if (enableAQM)
    {
        TrafficControlHelper tch;
        if (queueDisc == "CoDel")
        {
            tch.SetRootQueueDisc("ns3::CoDelQueueDisc");
        }
        else if (queueDisc == "PIE")
        {
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

        // Install queue disc on routers
        tch.Install(routerA.Get(0)->GetDevice(1)); // Router A -> Destination
        tch.Install(routerB.Get(0)->GetDevice(1)); // Router B -> Destination

        NS_LOG_INFO("Installed " << queueDisc << " queue discipline on bottleneck links");
    }

    NS_LOG_INFO("Setting up routing");
    // Set up routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // **NEW: Use factory to create strategy-specific client**
    NS_LOG_INFO("Creating MultiPathNadaClient with strategy " << pathSelectionStrategy);

    MultiPathNadaClientFactory::StrategyType strategyType =
        static_cast<MultiPathNadaClientFactory::StrategyType>(pathSelectionStrategy);

    Ptr<MultiPathNadaClientBase> mpClient = MultiPathNadaClientFactory::Create(strategyType);

    if (!mpClient)
    {
        NS_LOG_ERROR("Failed to create MultiPathNadaClient with strategy "
                     << pathSelectionStrategy);
        return 1;
    }

    NS_LOG_INFO("Created " << mpClient->GetStrategyName() << " strategy client successfully");

    // **UPDATED: Configure client (same interface)**
    mpClient->SetPacketSize(packetSize);
    mpClient->SetMaxPackets(maxPackets);

    NS_LOG_INFO("Creating server application at destination");
    // **SAME: Video receiver setup**
    uint16_t videoPort = 9;
    VideoReceiverHelper server(videoPort);
    server.SetAttribute("FrameRate", UintegerValue(frameRate));
    ApplicationContainer serverApp = server.Install(destination.Get(0));

    // **NEW: Strategy-specific configuration**
    if (pathSelectionStrategy == 5) // BUFFER_AWARE
    {
        Ptr<VideoReceiver> videoReceiver = DynamicCast<VideoReceiver>(serverApp.Get(0));
        if (videoReceiver)
        {
            mpClient->SetVideoReceiver(videoReceiver);
            NS_LOG_INFO("Linked video receiver to buffer-aware client");
        }
        else
        {
            NS_LOG_WARN("Failed to link video receiver to buffer-aware client");
        }
    }

    DataRate rate1(dataRate1);
    DataRate rate2(dataRate2);
    bool isHighSpeed = (rate1.GetBitRate() >= 1e9 || rate2.GetBitRate() >= 1e9);

    if (isHighSpeed)
    {
        NS_LOG_INFO("High-speed link detected - reducing logging overhead");
        logDetails = false; // **Force disable detailed logging**

        // **Disable time prefixes to reduce logging overhead**
        LogComponentDisableAll(LOG_PREFIX_TIME);
        LogComponentDisableAll(LOG_PREFIX_FUNC);
        LogComponentDisableAll(LOG_PREFIX_NODE);
    }

    NS_LOG_INFO("Adding path 1 to MultiPathNadaClient");
    uint16_t port = 9;
    InetSocketAddress destAddr1(ifcRouterADestination.GetAddress(1), port);
    bool path1Added = mpClient->AddPath(ifcSourceRouterA.GetAddress(0),
                                        destAddr1,
                                        1,   // Path ID
                                        0.5, // Weight
                                        DataRate(dataRate1));
    NS_LOG_INFO("Path 1 added: " << (path1Added ? "success" : "failed"));

    NS_LOG_INFO("Adding path 2 to MultiPathNadaClient");
    InetSocketAddress destAddr2(ifcRouterBDestination.GetAddress(1), port);
    bool path2Added = mpClient->AddPath(ifcSourceRouterB.GetAddress(0),
                                        destAddr2,
                                        2,   // Path ID
                                        0.5, // Weight
                                        DataRate(dataRate2));
    NS_LOG_INFO("Path 2 added: " << (path2Added ? "success" : "failed"));

    NS_LOG_INFO("Setting NADA congestion control parameters");
    mpClient->SetNadaAdaptability(1,                           // Path ID
                                  DataRate("100kbps"),         // minRate
                                  DataRate(dataRate1) * 0.9,   // maxRate (90% of link capacity)
                                  MilliSeconds(delayMs1 * 4)); // rttMax = 4x one-way delay

    mpClient->SetNadaAdaptability(2,                           // Path ID
                                  DataRate("100kbps"),         // minRate
                                  DataRate(dataRate2) * 0.9,   // maxRate (90% of link capacity)
                                  MilliSeconds(delayMs2 * 4)); // rttMax = 4x one-way delay

    // Create TCP sinks and competing traffic (same as before)
    NS_LOG_INFO("Setting up TCP competing traffic sources on path A");
    std::vector<ApplicationContainer> competingAppsA;

    uint16_t tcpPortBase = 50000;
    PacketSinkHelper sinkHelperA("ns3::TcpSocketFactory",
                                 InetSocketAddress(Ipv4Address::GetAny(), tcpPortBase));
    ApplicationContainer sinkAppsA = sinkHelperA.Install(destination.Get(0));
    sinkAppsA.Start(Seconds(0.5));
    sinkAppsA.Stop(Seconds(simulationTime));

    for (uint32_t i = 0; i < numCompetingSourcesPathA; i++)
    {
        uint16_t tcpPort = tcpPortBase + i;
        BulkSendHelper competingTcpA(
            "ns3::TcpSocketFactory",
            InetSocketAddress(ifcRouterADestination.GetAddress(1), tcpPort));
        competingTcpA.SetAttribute("MaxBytes", UintegerValue(0));
        competingTcpA.SetAttribute("SendSize", UintegerValue(packetSize));

        if (competingIntensityA > 0.7)
        {
            std::string tcpVariant = "ns3::TcpNewReno";
            Config::Set("/NodeList/" + std::to_string(competingSourcesA.Get(i)->GetId()) +
                            "/$ns3::TcpL4Protocol/SocketType",
                        TypeIdValue(TypeId::LookupByName(tcpVariant)));
        }

        ApplicationContainer app = competingTcpA.Install(competingSourcesA.Get(i));
        app.Start(Seconds(0.5 + i * 0.5));
        app.Stop(Seconds(simulationTime - 1));
        competingAppsA.push_back(app);
        NS_LOG_INFO("TCP competing source A" << i << " connected to port " << tcpPort);
    }

    // Create competing traffic on path B
    NS_LOG_INFO("Setting up TCP competing traffic sources on path B");
    std::vector<ApplicationContainer> competingAppsB;

    uint16_t tcpPortBaseB = tcpPortBase + numCompetingSourcesPathA;
    PacketSinkHelper sinkHelperB("ns3::TcpSocketFactory",
                                 InetSocketAddress(Ipv4Address::GetAny(), tcpPortBaseB));
    ApplicationContainer sinkAppsB = sinkHelperB.Install(destination.Get(0));
    sinkAppsB.Start(Seconds(0.5));
    sinkAppsB.Stop(Seconds(simulationTime));

    for (uint32_t i = 0; i < numCompetingSourcesPathB; i++)
    {
        uint16_t tcpPort = tcpPortBaseB + i;
        BulkSendHelper competingTcpB(
            "ns3::TcpSocketFactory",
            InetSocketAddress(ifcRouterBDestination.GetAddress(1), tcpPort));
        competingTcpB.SetAttribute("MaxBytes", UintegerValue(0));
        competingTcpB.SetAttribute("SendSize", UintegerValue(packetSize));

        if (competingIntensityB > 0.7)
        {
            std::string tcpVariant = "ns3::TcpNewReno";
            Config::Set("/NodeList/" + std::to_string(competingSourcesB.Get(i)->GetId()) +
                            "/$ns3::TcpL4Protocol/SocketType",
                        TypeIdValue(TypeId::LookupByName(tcpVariant)));
        }

        ApplicationContainer app = competingTcpB.Install(competingSourcesB.Get(i));
        app.Start(Seconds(0.7 + i * 0.5));
        app.Stop(Seconds(simulationTime - 1));
        competingAppsB.push_back(app);
        NS_LOG_INFO("TCP competing source B" << i << " connected to port " << tcpPort);
    }

    NS_LOG_INFO("Setting up WebRTC video streaming");
    // Set up WebRTC video streaming
    uint32_t frameCount = 0;
    Time frameInterval = Seconds(1.0 / frameRate);
    uint32_t totalPacketsSent = 0;
    WebRtcFrameStats frameStats;
    EventId frameEvent;

    NS_LOG_INFO("Starting applications");
    serverApp.Start(Seconds(0.1));

    // **UPDATED: Install application (no helper needed)**
    Ptr<Node> sourceNode = source.Get(0);
    sourceNode->AddApplication(mpClient);
    mpClient->SetStartTime(Seconds(0.2));
    mpClient->SetStopTime(Seconds(simulationTime - 0.5));

    NS_LOG_INFO("Using strategy: " << mpClient->GetStrategyName());

    Simulator::Schedule(Seconds(0.3), [mpClient]() {
        // The base class will handle socket initialization internally
        NS_LOG_INFO("Client initialization scheduled");
    });

    Simulator::Schedule(Seconds(0.5), [mpClient]() {
        // Base class will validate sockets internally
        NS_LOG_INFO("Client validation scheduled");
    });

    // **UPDATED: Client readiness check (simplified)**
    Simulator::Schedule(Seconds(0.6), [mpClient]() {
        if (mpClient->IsReady())
        {
            NS_LOG_INFO("Client is ready for strategy: " << mpClient->GetStrategyName());
        }
        else
        {
            NS_LOG_WARN("Client not ready yet for strategy: " << mpClient->GetStrategyName());
            // Let the base class handle retry logic internally
        }
    });

    for (int i = 0; i < 10; i++)
    {
        Simulator::Schedule(Seconds(1.0 + i * 5), [mpClient]() {
            if (mpClient->IsReady())
            {
                NS_LOG_INFO("Client status: Ready, Strategy: " << mpClient->GetStrategyName());
            }
            else
            {
                NS_LOG_WARN("Client status: Not ready, Strategy: " << mpClient->GetStrategyName());
            }
        });
    }

    if (!isHighSpeed)
    {
        for (int i = 0; i < 10; i++)
        {
            Simulator::Schedule(Seconds(1.0 + i * 5), [mpClient]() {
                if (mpClient->IsReady())
                {
                    NS_LOG_INFO("Client status: Ready, Strategy: " << mpClient->GetStrategyName());
                }
                else
                {
                    NS_LOG_WARN(
                        "Client status: Not ready, Strategy: " << mpClient->GetStrategyName());
                }
            });
        }

        // Status reporting - only for non-high-speed
        for (int i = 0; i < 2; i++) // **Reduce from 10 to 2 reports**
        {
            Simulator::Schedule(Seconds(5.0 + i * 10),
                                [mpClient]() { mpClient->ReportSocketStatus(); });
        }
    }

    NS_LOG_INFO("Giving applications time to initialize");
    // Allow more time for socket initialization
    Time socketInitDelay = Seconds(1.0);

    NS_LOG_INFO("Scheduling first video frame with delay for initialization: "
                << socketInitDelay.GetSeconds() << "s");

    // Start sending WebRTC frames with even longer delay
    Simulator::Schedule(socketInitDelay,
                        &SendMultipathVideoFrame,
                        mpClient,
                        std::ref(frameCount),
                        keyFrameInterval,
                        packetSize,
                        std::ref(frameEvent),
                        frameInterval,
                        std::ref(frameStats),
                        std::ref(totalPacketsSent),
                        maxPackets);

    NS_LOG_INFO("Setting up flow monitor");
    // Set up flow monitor
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;

    flowHelper.SetMonitorAttribute("DelayBinWidth", DoubleValue(0.001));
    flowHelper.SetMonitorAttribute("JitterBinWidth", DoubleValue(0.001));
    flowHelper.SetMonitorAttribute("PacketSizeBinWidth", DoubleValue(20));

    flowMonitor = flowHelper.InstallAll();

    NS_LOG_INFO("Starting simulation for " << simulationTime << " seconds");
    // Run simulation
    Simulator::Stop(Seconds(simulationTime));

    try
    {
        Simulator::Run();
        NS_LOG_INFO("Simulation completed successfully");
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception during simulation: " << e.what());
        return 1;
    }

    NS_LOG_INFO("Processing simulation results");
    // Print results
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

    std::cout << "\n=== MULTIPATH COMPETING WEBRTC SIMULATION RESULTS ===\n";
    std::cout << "Simulation time: " << simulationTime << " seconds\n";
    std::cout << "Strategy: " << mpClient->GetStrategyName() << "\n";
    std::cout << "Path 1: " << dataRate1 << ", " << delayMs1 << "ms delay, "
              << numCompetingSourcesPathA
              << " competing sources (intensity: " << competingIntensityA << ")\n";
    std::cout << "Path 2: " << dataRate2 << ", " << delayMs2 << "ms delay, "
              << numCompetingSourcesPathB
              << " competing sources (intensity: " << competingIntensityB << ")\n\n";

    // Group flows by source for better analysis
    std::vector<std::pair<FlowId, FlowMonitor::FlowStats>> mainSourceFlows;
    std::vector<std::pair<FlowId, FlowMonitor::FlowStats>> pathACompetingFlows;
    std::vector<std::pair<FlowId, FlowMonitor::FlowStats>> pathBCompetingFlows;

    // Print per-flow statistics
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        // Identify the flow source and add to appropriate group
        if (t.sourceAddress == ifcSourceRouterA.GetAddress(0) ||
            t.sourceAddress == ifcSourceRouterB.GetAddress(0))
        {
            mainSourceFlows.push_back(std::make_pair(i->first, i->second));
        }
        else
        {
            // Check if it's a competing source from path A
            bool isPathA = false;
            for (uint32_t j = 0; j < numCompetingSourcesPathA; j++)
            {
                if (t.sourceAddress == ifcCompetingSourcesA[j].GetAddress(0))
                {
                    pathACompetingFlows.push_back(std::make_pair(i->first, i->second));
                    isPathA = true;
                    break;
                }
            }

            // If not path A, check if path B
            if (!isPathA)
            {
                for (uint32_t j = 0; j < numCompetingSourcesPathB; j++)
                {
                    if (t.sourceAddress == ifcCompetingSourcesB[j].GetAddress(0))
                    {
                        pathBCompetingFlows.push_back(std::make_pair(i->first, i->second));
                        break;
                    }
                }
            }
        }

        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
                  << t.destinationAddress << ")\n";
        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
        std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / simulationTime / 1000000
                  << " Mbps\n";
        if (i->second.rxPackets > 0)
        {
            std::cout << "  Mean delay: " << i->second.delaySum.GetSeconds() / i->second.rxPackets
                      << " seconds\n";
            std::cout << "  Mean jitter: "
                      << i->second.jitterSum.GetSeconds() / (i->second.rxPackets - 1)
                      << " seconds\n";
        }
        if (i->second.txPackets > 0)
        {
            std::cout << "  Packet loss: "
                      << 100.0 * (i->second.txPackets - i->second.rxPackets) / i->second.txPackets
                      << "%\n";
        }
        std::cout << "\n";
    }

    // Print aggregated statistics by source type
    std::cout << "\n=== AGGREGATED STATISTICS BY SOURCE TYPE ===\n";

    // Helper function to print aggregated stats
    auto printAggStats = [](const std::string& sourceType,
                            const std::vector<std::pair<FlowId, FlowMonitor::FlowStats>>& flows,
                            double simulationTime) {
        if (flows.empty())
        {
            return;
        }

        uint64_t totalTx = 0, totalRx = 0;
        uint64_t totalTxBytes = 0, totalRxBytes = 0;
        double totalDelay = 0.0;
        uint64_t totalDelayedPackets = 0;

        for (const auto& flow : flows)
        {
            totalTx += flow.second.txPackets;
            totalRx += flow.second.rxPackets;
            totalTxBytes += flow.second.txBytes;
            totalRxBytes += flow.second.rxBytes;
            totalDelay += flow.second.delaySum.GetSeconds();
            totalDelayedPackets += flow.second.rxPackets;
        }

        std::cout << sourceType << ":\n";
        std::cout << "  Total Flows: " << flows.size() << "\n";
        std::cout << "  Total Tx Packets: " << totalTx << "\n";
        std::cout << "  Total Rx Packets: " << totalRx << "\n";
        std::cout << "  Total Tx Bytes: " << totalTxBytes << "\n";
        std::cout << "  Total Throughput: " << totalRxBytes * 8.0 / simulationTime / 1000000
                  << " Mbps\n";

        if (totalDelayedPackets > 0)
        {
            std::cout << "  Average Delay: " << totalDelay / totalDelayedPackets << " seconds\n";
        }

        if (totalTx > 0)
        {
            std::cout << "  Overall Packet Loss: " << 100.0 * (totalTx - totalRx) / totalTx
                      << "%\n";
        }
        std::cout << "\n";
    };

    Ptr<VideoReceiver> videoReceiverInstance = DynamicCast<VideoReceiver>(serverApp.Get(0));
    if (videoReceiverInstance)
    {
        std::cout << "\n=== VIDEO RECEIVER STATISTICS ===\n";
        std::cout << videoReceiverInstance->GetBufferStats();
        std::cout << "Buffer underruns: " << videoReceiverInstance->GetBufferUnderruns() << "\n";
        std::cout << "Average buffer length: " << videoReceiverInstance->GetAverageBufferLength()
                  << " ms\n";
    }
    std::cout << "\n";

    if (pathSelectionStrategy == 5) // BUFFER_AWARE
    {
        mpClient->SetVideoReceiver(videoReceiverInstance);
        NS_LOG_INFO("Configured buffer-aware strategy with target buffer: "
                    << targetBufferLength << "s, weight factor: " << bufferWeightFactor);

        Simulator::Schedule(Seconds(20.0), [videoReceiverInstance]() {
            NS_LOG_INFO("Buffer status check:");
            NS_LOG_INFO("  Buffer length: " << videoReceiverInstance->GetAverageBufferLength()
                                            << "ms");
            NS_LOG_INFO("  Buffer underruns: " << videoReceiverInstance->GetBufferUnderruns());
        });
    }

    printAggStats("WebRTC Source (Multipath)", mainSourceFlows, simulationTime);
    printAggStats("Path A Competing Sources", pathACompetingFlows, simulationTime);
    printAggStats("Path B Competing Sources", pathBCompetingFlows, simulationTime);

    // Print frame statistics
    frameStats.PrintStats();

    NS_LOG_INFO("Collecting path statistics");
    // **UPDATED: Path statistics reporting**
    std::cout << "\nPath Statistics (Strategy: " << mpClient->GetStrategyName() << "):\n";
    for (uint32_t pathId = 1; pathId <= mpClient->GetNumPaths(); pathId++)
    {
        std::map<std::string, double> pathStats = mpClient->GetPathStats(pathId);
        if (!pathStats.empty())
        {
            std::cout << "  Path " << pathId << ":\n";
            std::cout << "    Weight: " << pathStats["weight"] << "\n";
            std::cout << "    Rate: " << pathStats["rate_bps"] / 1000000.0 << " Mbps\n";
            std::cout << "    Packets sent: " << pathStats["packets_sent"] << "\n";
            std::cout << "    Packets acked: " << pathStats["packets_acked"] << "\n";
            std::cout << "    RTT: " << pathStats["rtt_ms"] << " ms\n";
            std::cout << "    Delay: " << pathStats["delay_ms"] << " ms\n";
        }
    }

    NS_LOG_INFO("Cleaning up simulation");
    Simulator::Destroy();
    NS_LOG_INFO("Simulation finished successfully");
    return 0;
}
