/*
 * NS-3 Simulation with NADA Congestion Control Algorithm
 * Topology: Source Router -> Intermediate Router (with additional sources) -> Destination Router
 * Using UDP transport with NADA congestion control
 *
 *
 *                                                 +-----------------+
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
| (NADA)      |   | Router                    |                   | Router                    |
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
#include "ns3/nada-udp-client.h"
#include "ns3/nada-improved.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/core-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("NadaUdpSimulation");

int
main(int argc, char* argv[])
{
    // Simulation parameters
    uint32_t packetSize = 1000;          // bytes
    std::string dataRate = "5Mbps";      // Base data rate for primary flow
    std::string bottleneckBw = "10Mbps"; // Bottleneck bandwidth
    uint32_t delayMs = 50;               // Base link delay in ms
    uint32_t simulationTime = 60;        // Simulation time in seconds
    uint32_t numCompetingSources = 3;    // Number of additional traffic sources

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of packets to send", packetSize);
    cmd.AddValue("dataRate", "Data rate of primary source", dataRate);
    cmd.AddValue("bottleneckBw", "Bottleneck link bandwidth", bottleneckBw);
    cmd.AddValue("delayMs", "Link delay in milliseconds", delayMs);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("numCompetingSources", "Number of competing traffic sources", numCompetingSources);
    cmd.Parse(argc, argv);

    // Configure output
    Time::SetResolution(Time::NS);
    LogComponentEnable("NadaUdpSimulation", LOG_LEVEL_INFO);
    LogComponentEnable("NadaCongestionControl", LOG_LEVEL_INFO); // Enable logging for NADA

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

    // Additional sources to intermediate router links
    NetDeviceContainer additionalToIntermediate[numCompetingSources];
    for (uint32_t i = 0; i < numCompetingSources; i++)
    {
        // Vary data rates slightly for additional sources
        std::stringstream ss;
        ss << (2 + i) << "Mbps"; // Different rates: 2Mbps, 3Mbps, 4Mbps...
        p2p.SetDeviceAttribute("DataRate", StringValue(ss.str()));
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
    // Note: This is a placeholder - you'll need to implement the NADA module
    NadaCongestionControlHelper nada;
    nada.Install(sourceRouter.Get(0));

    // Set up routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Create UDP applications on the source router
    uint16_t port = 9;
    Ipv4Address destIp = bottleneckIfaces.GetAddress(1);
    NS_LOG_INFO("Destination IP: " << destIp);

    InetSocketAddress destAddr(bottleneckIfaces.GetAddress(1), port);
    NS_LOG_INFO("Creating destination socket address: " << destAddr.GetIpv4() << ":" << destAddr.GetPort());

    UdpNadaClientHelper client(destAddr);
    client.SetAttribute("PacketSize", UintegerValue(packetSize));
    client.SetAttribute("MaxPackets", UintegerValue(0)); // Unlimited
    client.SetAttribute(
        "Interval",
        TimeValue(Seconds(0.001))); // Initial sending rate (will be adjusted by NADA)

    ApplicationContainer sourceApps = client.Install(sourceRouter.Get(0));
    sourceApps.Start(Seconds(1.0));
    sourceApps.Stop(Seconds(simulationTime - 1));

    // Create additional traffic sources
    UdpClientHelper additionalClient(bottleneckIfaces.GetAddress(1), port);
    additionalClient.SetAttribute("PacketSize", UintegerValue(packetSize));
    additionalClient.SetAttribute("MaxPackets", UintegerValue(0));

    ApplicationContainer additionalApps[numCompetingSources];
    for (uint32_t i = 0; i < numCompetingSources; i++)
    {
        // Different intervals for different rates
        double interval = 0.001 * (1 + 0.5 * i); // Vary sending rates
        additionalClient.SetAttribute("Interval", TimeValue(Seconds(interval)));
        additionalApps[i] = additionalClient.Install(additionalSources.Get(i));
        additionalApps[i].Start(Seconds(5.0 + i)); // Stagger start times
        additionalApps[i].Stop(Seconds(simulationTime - 1));
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

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
                  << t.destinationAddress << ")\n";
        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
        std::cout << "  Throughput: "
                  << i->second.rxBytes * 8.0 /
                         (i->second.timeLastRxPacket.GetSeconds() -
                          i->second.timeFirstTxPacket.GetSeconds()) /
                         1000000
                  << " Mbps\n";
        std::cout << "  Mean delay: " << i->second.delaySum.GetSeconds() / i->second.rxPackets
                  << " seconds\n";
        std::cout << "  Packet loss: "
                  << 100.0 * (i->second.txPackets - i->second.rxPackets) / i->second.txPackets
                  << "%\n";
    }

    Simulator::Destroy();
    return 0;
}
