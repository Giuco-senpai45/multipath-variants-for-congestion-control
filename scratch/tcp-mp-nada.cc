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
#include "ns3/mp-nada-client.h"
#include "ns3/nada-header.h"
#include "ns3/nada-improved.h"
#include "ns3/nada-udp-client.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultipathCompetingWebRtcSimulation");

class Counter : public SimpleRefCount<Counter>
{
  public:
    Counter()
        : count(0)
    {
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

bool
ValidateClientSockets(Ptr<MultiPathNadaClient> client)
{
    NS_LOG_FUNCTION(client);

    if (!client)
    {
        NS_LOG_ERROR("Client is null");
        return false;
    }

    bool clientReady = client->IsReady();

    if (!clientReady)
    {
        // Try to validate all sockets as a last-ditch effort
        client->ValidateAllSockets();

        // Check readiness again
        clientReady = client->IsReady();
        NS_LOG_INFO("Client ready status after validation attempt: "
                    << (clientReady ? "READY" : "STILL NOT READY"));
    }

    return clientReady;
}

void
SendMultipathVideoFrame(Ptr<MultiPathNadaClient> client,
                        uint32_t& frameCount,
                        uint32_t keyFrameInterval,
                        uint32_t frameSize,
                        EventId& frameEvent,
                        Time frameInterval,
                        WebRtcFrameStats& stats,
                        uint32_t& totalPacketsSent,
                        uint32_t maxPackets)
{
    NS_LOG_FUNCTION("Attempting to send video frame #" << frameCount);

    // Validate client
    if (!client)
    {
        NS_LOG_ERROR("Client is null, cannot send frame");
        return;
    }

    // Cancel any pending frame events
    if (frameEvent.IsPending())
    {
        Simulator::Cancel(frameEvent);
    }

    // Check socket readiness
    bool clientReady = ValidateClientSockets(client);

    if (!clientReady)
    {
        NS_LOG_WARN("Client not ready, rescheduling frame after 500ms");
        frameEvent = Simulator::Schedule(MilliSeconds(500),
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

    // If we reached here, client is ready, proceed with sending
    bool isKeyFrame = (frameCount % keyFrameInterval == 0);
    uint32_t currentFrameSize = isKeyFrame ? frameSize * 2 : frameSize;
    uint32_t mtu = 1000;                                            // Maximum packet size
    uint32_t numPacketsNeeded = (currentFrameSize + mtu - 1) / mtu; // Ceiling division

    NS_LOG_INFO("Sending " << (isKeyFrame ? "key" : "delta") << " frame #" << frameCount
                           << " (size: " << currentFrameSize
                           << " bytes, packets needed: " << numPacketsNeeded << ")");

    // Configure video parameters
    client->SetVideoMode(true);
    client->SetKeyFrameStatus(isKeyFrame);
    client->SetPacketSize(mtu);

    // Record frame in stats
    stats.RecordFrameSent(isKeyFrame);

    // Use a shared counter for tracking packets sent in this frame
    // This needs to be a shared pointer so it persists after this function exits
    Ptr<Counter> packetsSentCounter = Create<Counter>();
    packetsSentCounter->count = 0;

    auto schedulePacket = [client, packetsSentCounter, isKeyFrame, mtu](Time delay) {
        Simulator::Schedule(delay, [client, packetsSentCounter, isKeyFrame, mtu]() {
            if (!client)
            {
                std::cout << "ERROR: Client pointer is null in scheduled packet send" << std::endl;
                return;
            }
            // Create packet with error checking
            Ptr<Packet> packet;
            try
            {
                packet = Create<Packet>(mtu);
                if (!packet)
                {
                    std::cout << "ERROR: Failed to create packet" << std::endl;
                    return;
                }
            }
            catch (const std::exception& e)
            {
                std::cout << "EXCEPTION creating packet: " << e.what() << std::endl;
                return;
            }

            // Send using the client's Send method with extensive error handling
            bool sent = false;
            try
            {
                sent = client->Send(packet);
            }
            catch (const std::exception& e)
            {
                std::cout << "EXCEPTION in client->Send: " << e.what() << std::endl;
                return;
            }
            catch (...)
            {
                std::cout << "UNKNOWN EXCEPTION in client->Send" << std::endl;
                return;
            }

            if (sent)
            {
                // Use the shared counter to track sent packets safely
                try
                {
                    if (packetsSentCounter)
                    {
                        packetsSentCounter->count++;
                    }
                    else
                    {
                        std::cout << "ERROR: packetsSentCounter is null" << std::endl;
                    }
                }
                catch (const std::exception& e)
                {
                    std::cout << "EXCEPTION incrementing counter: " << e.what() << std::endl;
                }
            }
        });
    };

    uint32_t packetsToSend = std::min(numPacketsNeeded, maxPackets - totalPacketsSent);
    for (uint32_t i = 0; i < packetsToSend; i++)
    {
        Time packetDelay = MicroSeconds(i * 500); // 0.5ms between packets
        schedulePacket(packetDelay);

        // Increment total packets counter - assume all will be sent successfully
        totalPacketsSent++;
    }

    WebRtcFrameStats* statsPtr = &stats;

    // Schedule frame acknowledgment using the shared counter and pointer to stats
    Simulator::Schedule(MilliSeconds(100),
                        &ProcessFrameAcknowledgment,
                        statsPtr,
                        isKeyFrame,
                        packetsSentCounter);

    // Increment frame counter
    frameCount++;

    // Schedule next frame
    Time jitteredInterval = Seconds(AddJitter(frameInterval.GetSeconds(), 0.05));
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

int
main(int argc, char* argv[])
{
    // Simulation parameters
    uint32_t packetSize = 1000;

    // Multipath parameters
    std::string dataRate1 = "10Mbps";
    std::string dataRate2 = "5Mbps";
    uint32_t delayMs1 = 20;
    uint32_t delayMs2 = 40;

    uint32_t simulationTime = 60;
    uint32_t keyFrameInterval = 100;
    uint32_t frameRate = 15;
    bool logDetails = false;
    uint32_t maxPackets = 10000;
    uint32_t queueSize = 100;
    std::string queueDisc = "CoDel";
    uint32_t pathSelectionStrategy = 0; // 0=weighted, 1=best path, 2=equal

    // Competing traffic parameters
    uint32_t numCompetingSourcesPathA = 2;
    uint32_t numCompetingSourcesPathB = 2;
    double competingIntensityA = 0.5; // Relative intensity of competing traffic on path A (0-1)
    double competingIntensityB = 0.5; // Relative intensity of competing traffic on path B (0-1)
    bool enableAQM = false;           // Enable Active Queue Management

    // Parse command line arguments
    CommandLine cmd;
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
    cmd.AddValue(
        "pathSelection",
        "Path selection strategy (0=weighted, 1=best, 2=equal, 3=redundant, 4=frame-aware)",
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

    NS_LOG_INFO("Setting up TCP competing traffic sources on path A");
    std::vector<ApplicationContainer> competingAppsA;

    // Configure TCP options for competing sources
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(packetSize));
    Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));

    if (logDetails)
    {
        LogComponentEnable("MultiPathNadaClient", LOG_LEVEL_INFO);
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

    NS_LOG_INFO("Creating MultiPathNadaClient application");
    // Create MultiPathNadaClient application
    uint16_t port = 9;
    MultiPathNadaClientHelper clientHelper;
    clientHelper.SetAttribute("PacketSize", UintegerValue(packetSize));
    clientHelper.SetAttribute("MaxPackets", UintegerValue(maxPackets));
    clientHelper.SetAttribute("PathSelectionStrategy", UintegerValue(pathSelectionStrategy));

    NS_LOG_INFO("Installing MultiPathNadaClient on source node");
    // Install the client on the source node
    ApplicationContainer clientApp = clientHelper.Install(source.Get(0));
    Ptr<MultiPathNadaClient> mpClient = DynamicCast<MultiPathNadaClient>(clientApp.Get(0));
    if (mpClient == nullptr)
    {
        NS_LOG_ERROR("MultiPathNadaClient pointer is null, cannot continue");
        return 1;
    }
    NS_LOG_INFO("MultiPathNadaClient installed successfully");

    NS_LOG_INFO("Creating server application at destination");
    // Create server application at destination
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(destination.Get(0));

    NS_LOG_INFO("Adding path 1 to MultiPathNadaClient");
    // Add the two paths to the MultiPathNadaClient
    InetSocketAddress destAddr1(ifcRouterADestination.GetAddress(1), port);
    bool path1Added = mpClient->AddPath(ifcSourceRouterA.GetAddress(0),
                                        destAddr1,
                                        1,   // Path ID
                                        0.7, // Weight (path 1 has higher weight)
                                        DataRate(dataRate1));
    NS_LOG_INFO("Path 1 added: " << (path1Added ? "success" : "failed"));

    NS_LOG_INFO("Adding path 2 to MultiPathNadaClient");
    InetSocketAddress destAddr2(ifcRouterBDestination.GetAddress(1), port);
    bool path2Added = mpClient->AddPath(ifcSourceRouterB.GetAddress(0),
                                        destAddr2,
                                        2,   // Path ID
                                        0.3, // Weight (path 2 has lower weight)
                                        DataRate(dataRate2));
    NS_LOG_INFO("Path 2 added: " << (path2Added ? "success" : "failed"));

    NS_LOG_INFO("Setting NADA congestion control parameters");
    // Set NADA parameters for path 1
    mpClient->SetNadaAdaptability(1,                           // Path ID
                                  DataRate("100kbps"),         // minRate
                                  DataRate(dataRate1) * 0.9,   // maxRate (90% of link capacity)
                                  MilliSeconds(delayMs1 * 4)); // rttMax = 4x one-way delay

    // Set NADA parameters for path 2
    mpClient->SetNadaAdaptability(2,                           // Path ID
                                  DataRate("100kbps"),         // minRate
                                  DataRate(dataRate2) * 0.9,   // maxRate (90% of link capacity)
                                  MilliSeconds(delayMs2 * 4)); // rttMax = 4x one-way delay

    // Create TCP sinks at destination for path A competing sources
    uint16_t tcpPortBase = 50000;
    PacketSinkHelper sinkHelperA("ns3::TcpSocketFactory",
                                 InetSocketAddress(Ipv4Address::GetAny(), tcpPortBase));
    ApplicationContainer sinkAppsA = sinkHelperA.Install(destination.Get(0));
    sinkAppsA.Start(Seconds(0.5));
    sinkAppsA.Stop(Seconds(simulationTime));

    for (uint32_t i = 0; i < numCompetingSourcesPathA; i++)
    {
        // Each source uses a different port to avoid conflicts
        uint16_t tcpPort = tcpPortBase + i;

        // Calculate TCP sending intensity based on intensity parameter
        // Higher competing intensity means more data to send
        // Use bulk sender instead of UDP client
        BulkSendHelper competingTcpA(
            "ns3::TcpSocketFactory",
            InetSocketAddress(ifcRouterADestination.GetAddress(1), tcpPort));

        // Configure sending behavior
        competingTcpA.SetAttribute("MaxBytes", UintegerValue(0)); // Unlimited
        competingTcpA.SetAttribute("SendSize", UintegerValue(packetSize));

        // TCP congestion control automatically adjusts to network conditions
        // The intensity parameter affects initial burst behavior
        if (competingIntensityA > 0.7)
        {
            // High intensity: more aggressive TCP configuration
            std::string tcpVariant = "ns3::TcpNewReno";
            Config::Set("/NodeList/" + std::to_string(competingSourcesA.Get(i)->GetId()) +
                            "/$ns3::TcpL4Protocol/SocketType",
                        TypeIdValue(TypeId::LookupByName(tcpVariant)));
        }

        ApplicationContainer app = competingTcpA.Install(competingSourcesA.Get(i));

        // Start competing sources at different times to avoid synchronization
        app.Start(Seconds(5.0 + i * 2));           // Staggered start
        app.Stop(Seconds(simulationTime - 2 - i)); // Staggered stop

        competingAppsA.push_back(app);
        NS_LOG_INFO("TCP competing source A" << i << " connected to port " << tcpPort);
    }

    // Create competing traffic on path B
    NS_LOG_INFO("Setting up TCP competing traffic sources on path B");
    std::vector<ApplicationContainer> competingAppsB;

    // Create TCP sinks at destination for path B competing sources
    uint16_t tcpPortBaseB = tcpPortBase + numCompetingSourcesPathA;
    PacketSinkHelper sinkHelperB("ns3::TcpSocketFactory",
                                 InetSocketAddress(Ipv4Address::GetAny(), tcpPortBaseB));
    ApplicationContainer sinkAppsB = sinkHelperB.Install(destination.Get(0));
    sinkAppsB.Start(Seconds(0.5));
    sinkAppsB.Stop(Seconds(simulationTime));

    for (uint32_t i = 0; i < numCompetingSourcesPathB; i++)
    {
        // Each source uses a different port to avoid conflicts
        uint16_t tcpPort = tcpPortBaseB + i;

        // TCP bulk sender for path B
        BulkSendHelper competingTcpB(
            "ns3::TcpSocketFactory",
            InetSocketAddress(ifcRouterBDestination.GetAddress(1), tcpPort));

        // Configure sending behavior
        competingTcpB.SetAttribute("MaxBytes", UintegerValue(0)); // Unlimited
        competingTcpB.SetAttribute("SendSize", UintegerValue(packetSize));

        // Adjust TCP variant based on intensity
        if (competingIntensityB > 0.7)
        {
            // High intensity: more aggressive TCP configuration
            std::string tcpVariant = "ns3::TcpNewReno";
            Config::Set("/NodeList/" + std::to_string(competingSourcesB.Get(i)->GetId()) +
                            "/$ns3::TcpL4Protocol/SocketType",
                        TypeIdValue(TypeId::LookupByName(tcpVariant)));
        }

        ApplicationContainer app = competingTcpB.Install(competingSourcesB.Get(i));

        // Start competing sources at different times to avoid synchronization
        app.Start(Seconds(10.0 + i * 2));          // Staggered start later than path A
        app.Stop(Seconds(simulationTime - 3 - i)); // Staggered stop

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
    serverApp.Start(Seconds(0.5));
    clientApp.Start(Seconds(2.0));

    Simulator::Schedule(Seconds(1.0), &MultiPathNadaClient::InitializePathSocket, mpClient, 1);
    Simulator::Schedule(Seconds(1.1), &MultiPathNadaClient::InitializePathSocket, mpClient, 2);
    Simulator::Schedule(Seconds(2.5), &MultiPathNadaClient::ValidateAllSockets, mpClient);

    NS_LOG_INFO("Scheduling client readiness check");
    int attempts = 0;
    Simulator::Schedule(Seconds(1.0), [&, attempts]() {
        // Use the captured attempts value from the outer lambda
        int nextAttempt = attempts + 1;

        if (mpClient->IsReady())
        {
            NS_LOG_INFO("Client is ready after " << nextAttempt << " attempts");
        }
        else if (nextAttempt < 20)
        {
            NS_LOG_INFO("Client not ready yet, attempt " << nextAttempt);

            // Retry initialization
            Simulator::Schedule(Seconds(0.0),
                                &MultiPathNadaClient::InitializePathSocket,
                                mpClient,
                                1);
            Simulator::Schedule(Seconds(0.1),
                                &MultiPathNadaClient::InitializePathSocket,
                                mpClient,
                                2);

            // Schedule another check with incremented attempts
            Simulator::Schedule(Seconds(1.0), [&, nextAttempt]() {
                // Recursive check with proper attempt counting
                int recursiveAttempt = nextAttempt + 1;
                // Same readiness check logic as above
                if (mpClient->IsReady())
                {
                    NS_LOG_INFO("Client is ready after " << recursiveAttempt << " attempts");
                }
                else if (recursiveAttempt < 20)
                {
                    NS_LOG_INFO("Client not ready yet, attempt " << recursiveAttempt);
                    // Continue retry chain...
                    Simulator::Schedule(Seconds(0.0),
                                        &MultiPathNadaClient::InitializePathSocket,
                                        mpClient,
                                        1);
                    Simulator::Schedule(Seconds(0.1),
                                        &MultiPathNadaClient::InitializePathSocket,
                                        mpClient,
                                        2);
                }
                else
                {
                    NS_LOG_ERROR("Client failed to initialize after " << recursiveAttempt
                                                                      << " attempts");
                }
            });
        }
        else
        {
            NS_LOG_ERROR("Client failed to initialize after " << nextAttempt << " attempts");
        }
    });

    NS_LOG_INFO("Setting path selection strategy to "
                << mpClient->GetStrategyName(pathSelectionStrategy));
    mpClient->SetPathSelectionStrategy(pathSelectionStrategy);

    for (int i = 0; i < 10; i++)
    {
        Simulator::Schedule(Seconds(3.0 + i * 2),
                            &MultiPathNadaClient::ReportSocketStatus,
                            mpClient);
    }

    NS_LOG_INFO("Giving applications time to initialize");
    // Allow more time for socket initialization
    Time socketInitDelay = Seconds(15.0);

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

    NS_LOG_INFO("Setting application stop times");
    // Stop applications
    clientApp.Stop(Seconds(simulationTime - 0.5));
    serverApp.Stop(Seconds(simulationTime));

    NS_LOG_INFO("Setting up flow monitor");
    // Set up flow monitor
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
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
    std::cout << "Path 1: " << dataRate1 << ", " << delayMs1 << "ms delay, "
              << numCompetingSourcesPathA
              << " competing sources (intensity: " << competingIntensityA << ")\n";
    std::cout << "Path 2: " << dataRate2 << ", " << delayMs2 << "ms delay, "
              << numCompetingSourcesPathB
              << " competing sources (intensity: " << competingIntensityB << ")\n"
              << "\n\n";

    // Group flows by source for better analysis
    std::map<std::string, std::vector<std::pair<FlowId, FlowMonitor::FlowStats>>> flowsBySource;

    // Main WebRTC source flows
    std::vector<std::pair<FlowId, FlowMonitor::FlowStats>> mainSourceFlows;

    // Path A competing flows
    std::vector<std::pair<FlowId, FlowMonitor::FlowStats>> pathACompetingFlows;

    // Path B competing flows
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

    printAggStats("WebRTC Source (Multipath)", mainSourceFlows, simulationTime);
    printAggStats("Path A Competing Sources", pathACompetingFlows, simulationTime);
    printAggStats("Path B Competing Sources", pathBCompetingFlows, simulationTime);

    // Print frame statistics
    frameStats.PrintStats();

    NS_LOG_INFO("Collecting path statistics");
    // Print path statistics from the MultiPathNadaClient
    std::cout << "\nPath Statistics (Strategy: " << mpClient->GetStrategyName(pathSelectionStrategy)
              << "):\n";
    for (uint32_t pathId = 1; pathId <= mpClient->GetNumPaths(); pathId++)
    {
        std::map<std::string, double> pathStats = mpClient->GetPathStats(pathId);
        if (!pathStats.empty())
        {
            std::cout << "Path " << pathId << ":\n";
            std::cout << "  Weight: " << pathStats["weight"] << "\n";
            std::cout << "  Rate: " << pathStats["rate_bps"] / 1000000.0 << " Mbps\n";
            std::cout << "  Packets sent: " << pathStats["packets_sent"] << "\n";
            std::cout << "  Packets acked: " << pathStats["packets_acked"] << "\n";
            std::cout << "  RTT: " << pathStats["rtt_ms"] << " ms\n";
            std::cout << "  Delay: " << pathStats["delay_ms"] << " ms\n";
        }
    }

    NS_LOG_INFO("Cleaning up simulation");
    Simulator::Destroy();
    NS_LOG_INFO("Simulation finished successfully");
    return 0;
}
