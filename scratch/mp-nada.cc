#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/network-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/mp-nada-client.h"
#include "ns3/nada-udp-client.h"
#include "ns3/string.h"

/*
                                               +----------------+
                                               |                |
                                               |  Destination   |
                                               |     Node       |
                                               |                |
                                               +----------------+
                                                 ↑            ↑
                                                /              \
                               Path 1          /                \          Path 2
                          (bottleneckBw1)     /                  \     (bottleneckBw2)
                        (delayMs1 * 2 ms)    /                    \   (delayMs2 * 2 ms)
                                            /                      \
                  +------------------+     /                        \    +------------------+
                  |                  |    /                          \   |                  |
                  | Intermediate     |   /                            \  | Intermediate     |
                  | Router 1         |←-/                              \→| Router 2         |
                  |                  |                                   |                  |
                  +------------------+                                   +------------------+
                           ↑                                                      ↑
                           |                                                      |
                           | (dataRate1)                                          | (dataRate2)
                           | (delayMs1 ms)                                        | (delayMs2 ms)
                           |                                                      |
                           |               +------------------+                   |
                           |               |                  |                   |
                           |               |    Source Node   |                   |
                           +--------------→|    (MP-NADA)     |←------------------+
                                           |                  |
                                           +------------------+
                                                    ↑
                                                   /|\
                           Competing Sources       / | \       Competing Sources
                                Path 1            /  |  \          Path 2
                          +-----------------+    /   |   \    +-----------------+
                          |                 |   /    |    \   |                 |
                          | CompetingSrc11  |→-/     |     \←-| CompetingSrc21  |
                          | CompetingSrc12  |→-      |      ←-| CompetingSrc22  |
                          | ...             |        |        | ...             |
                          +-----------------+        |        +-----------------+
                                                     ↓
                                             +----------------+
                                             | Path Selection |
                                             |   Strategy:    |
                                             |   Weighted     |
                                             +----------------+
*/

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultiPathNadaSimulation");

int main(int argc, char *argv[]) {
    // Enable more detailed logging for debugging
    LogComponentEnable("MultiPathNadaClient", LOG_LEVEL_INFO);
    LogComponentEnable("UdpNadaClient", LOG_LEVEL_INFO);
    LogComponentEnable("UdpServer", LOG_LEVEL_INFO);
    LogComponentEnable("MultiPathNadaSimulation", LOG_LEVEL_INFO);

    // Simulation parameters
    uint32_t packetSize = 1000;            // bytes
    std::string dataRate1 = "5Mbps";       // Data rate for path 1
    std::string dataRate2 = "8Mbps";       // Data rate for path 2
    std::string bottleneckBw1 = "10Mbps";  // Bottleneck bandwidth for path 1
    std::string bottleneckBw2 = "12Mbps";  // Bottleneck bandwidth for path 2
    uint32_t delayMs1 = 50;                // Link delay for path 1 (ms)
    uint32_t delayMs2 = 30;                // Link delay for path 2 (ms)
    uint32_t simulationTime = 60;          // Simulation time in seconds
    uint32_t numCompetingSources = 2;      // Number of additional traffic sources on each path
    uint32_t pathSelectionStrategy = 0;    // Path selection strategy (0=weighted)

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of packets to send", packetSize);
    cmd.AddValue("dataRate1", "Data rate of path 1", dataRate1);
    cmd.AddValue("dataRate2", "Data rate of path 2", dataRate2);
    cmd.AddValue("bottleneckBw1", "Bottleneck link bandwidth for path 1", bottleneckBw1);
    cmd.AddValue("bottleneckBw2", "Bottleneck link bandwidth for path 2", bottleneckBw2);
    cmd.AddValue("delayMs1", "Link delay in milliseconds for path 1", delayMs1);
    cmd.AddValue("delayMs2", "Link delay in milliseconds for path 2", delayMs2);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("numCompetingSources", "Number of competing traffic sources on each path", numCompetingSources);
    cmd.AddValue("pathSelectionStrategy", "Path selection strategy", pathSelectionStrategy);
    cmd.Parse(argc, argv);

    // Configure output
    Time::SetResolution(Time::NS);

    // Create nodes
    NS_LOG_INFO("Creating nodes...");
    NodeContainer sourceNode;
    sourceNode.Create(1);
    NodeContainer intermediateRouter1;
    intermediateRouter1.Create(1);
    NodeContainer intermediateRouter2;
    intermediateRouter2.Create(1);
    NodeContainer destinationNode;
    destinationNode.Create(1);
    NodeContainer competingSources1;
    competingSources1.Create(numCompetingSources);
    NodeContainer competingSources2;
    competingSources2.Create(numCompetingSources);

    // Create links for path 1
    NS_LOG_INFO("Creating links for path 1...");
    PointToPointHelper p2p1;
    p2p1.SetDeviceAttribute("DataRate", StringValue(dataRate1));
    p2p1.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs1)));
    NetDeviceContainer sourceToIntermediate1 = p2p1.Install(sourceNode.Get(0), intermediateRouter1.Get(0));

    PointToPointHelper bottleneck1;
    bottleneck1.SetDeviceAttribute("DataRate", StringValue(bottleneckBw1));
    bottleneck1.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs1 * 2)));
    NetDeviceContainer intermediate1ToDestination = bottleneck1.Install(intermediateRouter1.Get(0), destinationNode.Get(0));

    // Create links for path 2
    NS_LOG_INFO("Creating links for path 2...");
    PointToPointHelper p2p2;
    p2p2.SetDeviceAttribute("DataRate", StringValue(dataRate2));
    p2p2.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs2)));
    NetDeviceContainer sourceToIntermediate2 = p2p2.Install(sourceNode.Get(0), intermediateRouter2.Get(0));

    PointToPointHelper bottleneck2;
    bottleneck2.SetDeviceAttribute("DataRate", StringValue(bottleneckBw2));
    bottleneck2.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs2 * 2)));
    NetDeviceContainer intermediate2ToDestination = bottleneck2.Install(intermediateRouter2.Get(0), destinationNode.Get(0));

    // Install internet stack on all nodes
    NS_LOG_INFO("Installing Internet stack...");
    InternetStackHelper internet;
    internet.Install(sourceNode);
    internet.Install(intermediateRouter1);
    internet.Install(intermediateRouter2);
    internet.Install(destinationNode);
    internet.Install(competingSources1);
    internet.Install(competingSources2);

    // Configure IP addresses
    NS_LOG_INFO("Assigning IP addresses...");
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer sourceToIntermediate1Ifaces = ipv4.Assign(sourceToIntermediate1);

    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer intermediate1ToDestinationIfaces = ipv4.Assign(intermediate1ToDestination);

    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer sourceToIntermediate2Ifaces = ipv4.Assign(sourceToIntermediate2);

    ipv4.SetBase("10.2.2.0", "255.255.255.0");
    Ipv4InterfaceContainer intermediate2ToDestinationIfaces = ipv4.Assign(intermediate2ToDestination);

    // Set up routing
    NS_LOG_INFO("Setting up routing...");
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Install NADA congestion control on source
    NS_LOG_INFO("Installing NADA congestion control...");
    NadaCongestionControlHelper nada;
    nada.Install(sourceNode.Get(0));

    // Create UDP server at destination
    NS_LOG_INFO("Setting up server at destination...");
    uint16_t port = 9;
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(destinationNode.Get(0));
    serverApp.Start(Seconds(0.5));
    serverApp.Stop(Seconds(simulationTime));

    // Create MultiPath NADA client using the helper
    NS_LOG_INFO("Creating MultiPath NADA client...");
    MultiPathNadaClientHelper mpClientHelper;
    mpClientHelper.SetAttribute("PacketSize", UintegerValue(packetSize));
    mpClientHelper.SetAttribute("MaxPackets", UintegerValue(0)); // Unlimited
    mpClientHelper.SetAttribute("Interval", TimeValue(MilliSeconds(200))); // Update interval
    mpClientHelper.SetAttribute("PathSelectionStrategy", UintegerValue(pathSelectionStrategy));

    ApplicationContainer mpClientApp = mpClientHelper.Install(sourceNode.Get(0));
    Ptr<MultiPathNadaClient> mpClient = DynamicCast<MultiPathNadaClient>(mpClientApp.Get(0));

    if (!mpClient) {
        NS_FATAL_ERROR("Failed to create MultiPathNadaClient");
        return 1;
    }

    // Set start and stop times
    mpClientApp.Start(Seconds(1.0));
    mpClientApp.Stop(Seconds(simulationTime - 1));

    // Create addresses for the paths
    InetSocketAddress destAddr1(intermediate1ToDestinationIfaces.GetAddress(1), port);
    InetSocketAddress destAddr2(intermediate2ToDestinationIfaces.GetAddress(1), port);

    InetSocketAddress localAddr1(sourceToIntermediate1Ifaces.GetAddress(0), 0);
    InetSocketAddress localAddr2(sourceToIntermediate2Ifaces.GetAddress(0), 0);

    uint32_t path1Id = 1;
    uint32_t path2Id = 2;
    DataRate initialRate1 = DataRate("1Mbps");
    DataRate initialRate2 = DataRate("1.5Mbps");

    // Add paths to the multipath client
    NS_LOG_INFO("Adding paths to MultiPath NADA client...");
    bool path1Success = mpClient->AddPath(localAddr1, destAddr1, path1Id, 1.0, initialRate1);
    NS_LOG_INFO("Path 1 added: " << (path1Success ? "successfully" : "failed"));

    bool path2Success = mpClient->AddPath(localAddr2, destAddr2, path2Id, 1.5, initialRate2);
    NS_LOG_INFO("Path 2 added: " << (path2Success ? "successfully" : "failed"));

    // Create additional traffic sources for path 1
    NS_LOG_INFO("Creating competing traffic sources for path 1...");
    UdpClientHelper additionalClient1(intermediate1ToDestinationIfaces.GetAddress(1), port);
    additionalClient1.SetAttribute("PacketSize", UintegerValue(packetSize));
    additionalClient1.SetAttribute("MaxPackets", UintegerValue(0));

    ApplicationContainer additionalApps1[numCompetingSources];
    for (uint32_t i = 0; i < numCompetingSources; i++) {
        double interval = 0.001 * (1 + 0.5 * i); // Vary sending rates
        additionalClient1.SetAttribute("Interval", TimeValue(Seconds(interval)));
        additionalApps1[i] = additionalClient1.Install(competingSources1.Get(i));
        additionalApps1[i].Start(Seconds(5.0 + i)); // Stagger start times
        additionalApps1[i].Stop(Seconds(simulationTime - 1));
    }

    // Create additional traffic sources for path 2
    NS_LOG_INFO("Creating competing traffic sources for path 2...");
    UdpClientHelper additionalClient2(intermediate2ToDestinationIfaces.GetAddress(1), port);
    additionalClient2.SetAttribute("PacketSize", UintegerValue(packetSize));
    additionalClient2.SetAttribute("MaxPackets", UintegerValue(0));

    ApplicationContainer additionalApps2[numCompetingSources];
    for (uint32_t i = 0; i < numCompetingSources; i++) {
        double interval = 0.001 * (1 + 0.5 * i); // Vary sending rates
        additionalClient2.SetAttribute("Interval", TimeValue(Seconds(interval)));
        additionalApps2[i] = additionalClient2.Install(competingSources2.Get(i));
        additionalApps2[i].Start(Seconds(10.0 + i));
        additionalApps2[i].Stop(Seconds(simulationTime - 1));
    }

    // Set up flow monitor
    NS_LOG_INFO("Setting up flow monitor...");
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    // Run simulation
    NS_LOG_INFO("Starting simulation...");
    Simulator::Stop(Seconds(simulationTime));

    try {
        Simulator::Run();
        NS_LOG_INFO("Simulation completed successfully.");
    } catch (const std::exception& e) {
        std::cerr << "Exception during simulation: " << e.what() << std::endl;
        return 1;
    }

    // Process flow monitor statistics
    NS_LOG_INFO("Processing flow monitor statistics...");
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";

        if (i->second.rxPackets > 0) {
            std::cout << "  Throughput: " << i->second.rxBytes * 8.0 /
                (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) /
                1000000 << " Mbps\n";
            std::cout << "  Mean delay: " << i->second.delaySum.GetSeconds() / i->second.rxPackets << " seconds\n";
        } else {
            std::cout << "  Throughput: 0 Mbps (no packets received)\n";
            std::cout << "  Mean delay: N/A (no packets received)\n";
        }

        std::cout << "  Packet loss: " << 100.0 * (i->second.txPackets - i->second.rxPackets) / i->second.txPackets << "%\n";
    }

    Simulator::Destroy();
    return 0;
}
