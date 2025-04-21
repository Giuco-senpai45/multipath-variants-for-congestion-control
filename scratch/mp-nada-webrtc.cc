/*
 * NS-3 Simulation with Multipath NADA Congestion Control for WebRTC
 * This simulation creates a topology with multiple paths between source and destination
 * to demonstrate multipath WebRTC with NADA congestion control
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

NS_LOG_COMPONENT_DEFINE("MultipathWebRtcSimulation");

// Track WebRTC frame statistics
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
        if (isKeyFrame)
        {
            m_keyFramesAcked++;
            NS_LOG_INFO("Key frame acknowledged (total: " << m_keyFramesAcked << ")");
        }
        else
        {
            m_deltaFramesAcked++;
            NS_LOG_INFO("Delta frame acknowledged (total: " << m_deltaFramesAcked << ")");
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

// Add jitter to make traffic more realistic
double
AddJitter(double baseValue, double jitterPercent)
{
    static Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double jitterFactor = 1.0 + rng->GetValue(-jitterPercent, jitterPercent);
    return baseValue * jitterFactor;
}

// Send WebRTC video frames over multiple paths
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
    NS_LOG_INFO("Attempting to send video frame #" << frameCount);

    if (!client)
    {
        NS_LOG_ERROR("Client is null, cannot send video frame");
        return;
    }

    if (totalPacketsSent >= maxPackets)
    {
        NS_LOG_INFO("Reached max packets limit, stopping video frame sending");
        return;
    }

    if (frameEvent.IsPending())
    {
        Simulator::Cancel(frameEvent);
    }

    // Check if client is ready to send - need to wait a bit for socket initialization
    bool clientReady = client->IsReady();
    if (!clientReady)
    {
        NS_LOG_INFO("Client not ready to send, rescheduling frame after 500ms");
        // Reschedule after a slightly longer delay
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

    // Determine frame type
    bool isKeyFrame = (frameCount % keyFrameInterval == 0);
    uint32_t currentFrameSize = isKeyFrame ? frameSize * 2 : frameSize;

    // Set path selection strategy based on frame type
    uint32_t pathSelectionStrategy =
        isKeyFrame ? 2 : 0; // Use all paths for key frames, weighted for delta
    client->SetAttribute("PathSelectionStrategy", UintegerValue(pathSelectionStrategy));

    // Calculate number of packets needed for this frame
    uint32_t mtu = 1000;                                      // Use a smaller MTU to avoid issues
    uint32_t numPackets = (currentFrameSize + mtu - 1) / mtu; // Ceiling division

    // Make sure we don't exceed the max packet limit
    uint32_t packetsToSend = std::min(numPackets, maxPackets - totalPacketsSent);

    NS_LOG_INFO("Frame #" << frameCount << ": " << (isKeyFrame ? "KEY" : "delta")
                          << ", size=" << currentFrameSize << " bytes, packets=" << packetsToSend);

    // Update stats and packet count
    stats.RecordFrameSent(isKeyFrame);
    client->SetPacketSize(mtu);
    client->SetMaxPackets(packetsToSend > 0 ? packetsToSend : 1);

    // Try to send the packets
    if (packetsToSend > 0)
    {
        // Create a counter to track successful packet sends for this frame
        std::shared_ptr<uint32_t> successfulSends = std::make_shared<uint32_t>(0);

        for (uint32_t i = 0; i < packetsToSend; i++)
        {
            // Schedule each packet with a small delay to spread them out
            Time packetDelay = MilliSeconds(i * 5); // More spread to avoid congestion

            Simulator::Schedule(packetDelay, [client, successfulSends, isKeyFrame, &stats](void) {
                try
                {
                    // Directly create and send a packet on the appropriate path
                    Ptr<Packet> packet = Create<Packet>(client->GetPacketSize());

                    std::vector<uint32_t> paths;
                    for (uint32_t id = 1; id <= client->GetNumPaths(); id++)
                    {
                        paths.push_back(id);
                    }

                    uint32_t pathId;
                    if (paths.size() > 0)
                    {
                        // Alternate between paths for better distribution
                        static uint32_t pathIndex = 0;
                        pathId = paths[pathIndex % paths.size()];
                        pathIndex++;

                        if (client->SendPacketOnPath(pathId, packet))
                        {
                            (*successfulSends)++;
                            NS_LOG_INFO("Packet sent successfully on path " << pathId);
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    NS_LOG_ERROR("Exception sending packet: " << e.what());
                }
            });
        }

        // Schedule an event to check packet send success and update frame stats
        Simulator::Schedule(MilliSeconds(packetsToSend * 5 + 50),
                            [successfulSends, isKeyFrame, &stats, packetsToSend](void) {
                                NS_LOG_INFO("Frame send stats: " << *successfulSends << "/"
                                                                 << packetsToSend
                                                                 << " packets sent");

                                // Simulate frame acknowledgment - this would come from the receiver
                                // in a real system
                                if (*successfulSends > 0)
                                {
                                    stats.RecordFrameAcked(isKeyFrame);
                                }

                                // If we couldn't send all packets, record partial loss
                                if (*successfulSends < packetsToSend)
                                {
                                    stats.RecordFrameLost(isKeyFrame);
                                }
                            });

        totalPacketsSent += packetsToSend;
    }
    else
    {
        NS_LOG_WARN("Not sending any packets for frame " << frameCount);
    }

    frameCount++;

    // Schedule next frame with appropriate timing
    Time jitteredFrameInterval = Seconds(AddJitter(frameInterval.GetSeconds(), 0.05));
    frameEvent = Simulator::Schedule(jitteredFrameInterval,
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
    std::string dataRate1 = "1024Mbps";
    std::string dataRate2 = "500Mbps";
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
    cmd.AddValue("pathSelection",
                 "Path selection strategy (0=weighted, 1=best, 2=equal)",
                 pathSelectionStrategy);
    cmd.Parse(argc, argv);

    // Configure logging
    Time::SetResolution(Time::NS);
    LogComponentEnable("MultipathWebRtcSimulation", LOG_LEVEL_INFO);

    LogComponentEnable("MultipathWebRtcSimulation", LOG_LEVEL_DEBUG);
    NS_LOG_DEBUG("Starting simulation with parameters:");
    NS_LOG_DEBUG("  packetSize: " << packetSize);
    NS_LOG_DEBUG("  dataRate1: " << dataRate1 << ", dataRate2: " << dataRate2);
    NS_LOG_DEBUG("  delayMs1: " << delayMs1 << ", delayMs2: " << delayMs2);
    NS_LOG_DEBUG("  simulationTime: " << simulationTime);
    NS_LOG_DEBUG("  pathSelectionStrategy: " << pathSelectionStrategy);
    NS_LOG_DEBUG("  maxPackets: " << maxPackets);

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

    NS_LOG_DEBUG("Creating nodes");
    // Create nodes
    NodeContainer source;
    source.Create(1);

    NodeContainer routerA;
    routerA.Create(1);

    NodeContainer routerB;
    routerB.Create(1);

    NodeContainer destination;
    destination.Create(1);

    NS_LOG_DEBUG("Setting up network links");
    // Create the two path links
    PointToPointHelper p2p1;
    p2p1.SetDeviceAttribute("DataRate", StringValue(dataRate1));
    p2p1.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs1)));

    PointToPointHelper p2p2;
    p2p2.SetDeviceAttribute("DataRate", StringValue(dataRate2));
    p2p2.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs2)));

    NS_LOG_DEBUG("Installing network devices for path 1");
    // Install devices - Path 1: Source -> RouterA -> Destination
    NetDeviceContainer devSourceToRouterA = p2p1.Install(source.Get(0), routerA.Get(0));
    NetDeviceContainer devRouterAToDestination = p2p1.Install(routerA.Get(0), destination.Get(0));

    NS_LOG_DEBUG("Installing network devices for path 2");
    // Install devices - Path 2: Source -> RouterB -> Destination
    NetDeviceContainer devSourceToRouterB = p2p2.Install(source.Get(0), routerB.Get(0));
    NetDeviceContainer devRouterBToDestination = p2p2.Install(routerB.Get(0), destination.Get(0));

    AsciiTraceHelper ascii;
    p2p1.EnableAsciiAll(ascii.CreateFileStream("path1-trace.tr"));
    p2p2.EnableAsciiAll(ascii.CreateFileStream("path2-trace.tr"));

    NS_LOG_DEBUG("Installing internet stack on nodes");
    // Install internet stack
    InternetStackHelper internet;
    internet.Install(source);
    internet.Install(routerA);
    internet.Install(routerB);
    internet.Install(destination);

    internet.EnablePcapIpv4All("multipath-webrtc");

    NS_LOG_DEBUG("Configuring IP addresses");
    // Configure IP addresses
    Ipv4AddressHelper ipv4;

    // Path 1 IP addressing
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcSourceRouterA = ipv4.Assign(devSourceToRouterA);
    NS_LOG_DEBUG("Path 1 first segment: " << ifcSourceRouterA.GetAddress(0) << " -> "
                                          << ifcSourceRouterA.GetAddress(1));

    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcRouterADestination = ipv4.Assign(devRouterAToDestination);
    NS_LOG_DEBUG("Path 1 second segment: " << ifcRouterADestination.GetAddress(0) << " -> "
                                           << ifcRouterADestination.GetAddress(1));

    // Path 2 IP addressing
    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcSourceRouterB = ipv4.Assign(devSourceToRouterB);
    NS_LOG_DEBUG("Path 2 first segment: " << ifcSourceRouterB.GetAddress(0) << " -> "
                                          << ifcSourceRouterB.GetAddress(1));

    ipv4.SetBase("10.2.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifcRouterBDestination = ipv4.Assign(devRouterBToDestination);
    NS_LOG_DEBUG("Path 2 second segment: " << ifcRouterBDestination.GetAddress(0) << " -> "
                                           << ifcRouterBDestination.GetAddress(1));

    NS_LOG_DEBUG("Setting up routing");
    // Set up routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    NS_LOG_DEBUG("Creating MultiPathNadaClient application");
    // Create MultiPathNadaClient application
    uint16_t port = 9;
    MultiPathNadaClientHelper clientHelper;
    clientHelper.SetAttribute("PacketSize", UintegerValue(packetSize));
    clientHelper.SetAttribute("MaxPackets", UintegerValue(maxPackets));
    clientHelper.SetAttribute("PathSelectionStrategy", UintegerValue(pathSelectionStrategy));

    NS_LOG_DEBUG("Installing MultiPathNadaClient on source node");
    // Install the client on the source node
    ApplicationContainer clientApp = clientHelper.Install(source.Get(0));
    Ptr<MultiPathNadaClient> mpClient = DynamicCast<MultiPathNadaClient>(clientApp.Get(0));
    if (mpClient == nullptr)
    {
        NS_LOG_ERROR("MultiPathNadaClient pointer is null, cannot continue");
        return 1;
    }
    NS_LOG_DEBUG("MultiPathNadaClient installed successfully");

    NS_LOG_DEBUG("Creating server application at destination");
    // Create server application at destination
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(destination.Get(0));

    NS_LOG_DEBUG("Adding path 1 to MultiPathNadaClient");
    // Add the two paths to the MultiPathNadaClient
    InetSocketAddress destAddr1(ifcRouterADestination.GetAddress(1), port);
    bool path1Added = mpClient->AddPath(ifcSourceRouterA.GetAddress(0),
                                        destAddr1,
                                        1,   // Path ID
                                        0.7, // Weight (path 1 has higher weight)
                                        DataRate(dataRate1));
    NS_LOG_DEBUG("Path 1 added: " << (path1Added ? "success" : "failed"));

    NS_LOG_DEBUG("Adding path 2 to MultiPathNadaClient");
    InetSocketAddress destAddr2(ifcRouterBDestination.GetAddress(1), port);
    bool path2Added = mpClient->AddPath(ifcSourceRouterB.GetAddress(0),
                                        destAddr2,
                                        2,   // Path ID
                                        0.3, // Weight (path 2 has lower weight)
                                        DataRate(dataRate2));
    NS_LOG_DEBUG("Path 2 added: " << (path2Added ? "success" : "failed"));

    NS_LOG_DEBUG("Setting up WebRTC video streaming");
    // Set up WebRTC video streaming
    uint32_t frameCount = 0;
    Time frameInterval = Seconds(1.0 / frameRate);
    uint32_t totalPacketsSent = 0;
    WebRtcFrameStats frameStats;
    EventId frameEvent;

    NS_LOG_DEBUG("Starting applications");
    serverApp.Start(Seconds(0.5));
    clientApp.Start(Seconds(2.0));
    NS_LOG_DEBUG("Giving applications time to initialize");
    // Allow more time for socket initialization
    Time socketInitDelay = Seconds(5.0);

    NS_LOG_DEBUG("Scheduling first video frame with delay for initialization: "
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

    NS_LOG_DEBUG("Setting application stop times");
    // Stop applications
    clientApp.Stop(Seconds(simulationTime - 0.5));
    serverApp.Stop(Seconds(simulationTime));

    NS_LOG_DEBUG("Setting up flow monitor");
    // Set up flow monitor
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    NS_LOG_DEBUG("Starting simulation for " << simulationTime << " seconds");
    // Run simulation
    Simulator::Stop(Seconds(simulationTime));

    try
    {
        Simulator::Run();
        NS_LOG_DEBUG("Simulation completed successfully");
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception during simulation: " << e.what());
        return 1;
    }
    catch (...)
    {
        NS_LOG_ERROR("Unknown exception during simulation");
        return 1;
    }

    NS_LOG_DEBUG("Processing simulation results");
    // Print results
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

    std::cout << "\n=== MULTIPATH WEBRTC SIMULATION RESULTS ===\n";
    std::cout << "Simulation time: " << simulationTime << " seconds\n";
    std::cout << "Path 1: " << dataRate1 << ", " << delayMs1 << "ms delay\n";
    std::cout << "Path 2: " << dataRate2 << ", " << delayMs2 << "ms delay\n";
    std::cout << "Path selection strategy: "
              << (pathSelectionStrategy == 0 ? "Weighted"
                                             : (pathSelectionStrategy == 1 ? "Best path" : "Equal"))
              << "\n\n";

    // Print per-flow statistics
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
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

    // Print frame statistics
    frameStats.PrintStats();

    NS_LOG_DEBUG("Collecting path statistics");
    // Print path statistics from the MultiPathNadaClient
    std::cout << "\nPath Statistics:\n";
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

    NS_LOG_DEBUG("Cleaning up simulation");
    Simulator::Destroy();
    NS_LOG_DEBUG("Simulation finished successfully");
    return 0;
}
