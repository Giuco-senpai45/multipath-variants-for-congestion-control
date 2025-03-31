/*
 * Simple NS-3 Simulation with NADA Congestion Control Algorithm
 * - Main source sends at fixed 1Mbps rate
 * - Competing sources also send at fixed 1Mbps rate
 *
 *                                           +----------------+
 *                                           | Destination    |
 *                                           | Router         |
 *                                           +----------------+
 *                                                   ▲
 *                                                   |
 *                                                   | Bottleneck Link
 *                                                   | (bottleneckBw, delayMs*2)
 *                                                   |
 *          +--------------------+                   |         +--------------------+
 *          |                    |                   |         |                    |
 * +------+ |                    |                   |         |                    |
 * | NADA |-| Source Router      |-------------------|-------->| Intermediate       |
 * +------+ |                    |                             | Router             |
 *          |                    |                             |                    |
 *          +--------------------+                             +--------------------+
 *                                                                      ▲
 *                                                                      |
 *                                                             +------------------+
 *                                                             | Competing Sources|
 *                                                             | (Fixed 1Mbps)    |
 *                                                             +------------------+
 */

 #include "ns3/applications-module.h"
 #include "ns3/core-module.h"
 #include "ns3/flow-monitor-module.h"
 #include "ns3/internet-module.h"
 #include "ns3/nada-header.h"
 #include "ns3/nada-udp-client.h"
 #include "ns3/nada.h"
 #include "ns3/network-module.h"
 #include "ns3/point-to-point-module.h"
 #include "ns3/traffic-control-module.h"

 using namespace ns3;

 NS_LOG_COMPONENT_DEFINE("SimpleNadaSimulation");

 int main(int argc, char* argv[])
 {
     // Simulation parameters with default values
     uint32_t packetSize = 1000;          // bytes
     std::string bottleneckBw = "5Mbps";  // Bottleneck bandwidth
     uint32_t delayMs = 50;               // Base link delay in ms
     uint32_t simulationTime = 60;        // Simulation time in seconds
     uint32_t numCompetingSources = 3;    // Number of competing sources

     // Parse command line arguments
     CommandLine cmd;
     cmd.AddValue("packetSize", "Size of packets to send", packetSize);
     cmd.AddValue("bottleneckBw", "Bottleneck link bandwidth", bottleneckBw);
     cmd.AddValue("delayMs", "Link delay in milliseconds", delayMs);
     cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
     cmd.AddValue("numCompetingSources", "Number of competing traffic sources", numCompetingSources);
     cmd.Parse(argc, argv);

     // Configure output
     Time::SetResolution(Time::NS);
     LogComponentEnable("SimpleNadaSimulation", LOG_LEVEL_INFO);
     LogComponentEnable("NadaCongestionControl", LOG_LEVEL_INFO);

     // Create nodes
     NodeContainer sourceRouter;
     sourceRouter.Create(1);

     NodeContainer intermediateRouter;
     intermediateRouter.Create(1);

     NodeContainer destinationRouter;
     destinationRouter.Create(1);

     NodeContainer competingSources;
     competingSources.Create(numCompetingSources);

     // Create links - all at fixed 10Mbps except bottleneck
     PointToPointHelper p2p;
     p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
     p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs)));

     // Source to Intermediate Router link
     NetDeviceContainer sourceToIntermediate =
         p2p.Install(sourceRouter.Get(0), intermediateRouter.Get(0));

     // Create bottleneck link (intermediate to destination)
     PointToPointHelper bottleneck;
     bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBw));
     bottleneck.SetChannelAttribute("Delay", TimeValue(MilliSeconds(delayMs * 2)));
     NetDeviceContainer intermediateToDestination =
         bottleneck.Install(intermediateRouter.Get(0), destinationRouter.Get(0));

     // Competing sources to intermediate router links
     NetDeviceContainer competingToIntermediate[numCompetingSources];
     for (uint32_t i = 0; i < numCompetingSources; i++) {
         competingToIntermediate[i] =
             p2p.Install(competingSources.Get(i), intermediateRouter.Get(0));
     }

     // Install internet stack on all nodes
     InternetStackHelper internet;
     internet.Install(sourceRouter);
     internet.Install(intermediateRouter);
     internet.Install(destinationRouter);
     internet.Install(competingSources);

     // Configure IP addresses
     Ipv4AddressHelper ipv4;

     ipv4.SetBase("10.1.1.0", "255.255.255.0");
     Ipv4InterfaceContainer sourceRouterIfaces = ipv4.Assign(sourceToIntermediate);

     ipv4.SetBase("10.1.2.0", "255.255.255.0");
     Ipv4InterfaceContainer bottleneckIfaces = ipv4.Assign(intermediateToDestination);

     // Assign IPs to competing sources
     Ipv4InterfaceContainer competingSourceIfaces[numCompetingSources];
     for (uint32_t i = 0; i < numCompetingSources; i++) {
         std::stringstream ss;
         ss << "10.1." << (i + 3) << ".0";
         ipv4.SetBase(ss.str().c_str(), "255.255.255.0");
         competingSourceIfaces[i] = ipv4.Assign(competingToIntermediate[i]);
     }

     // Install NADA congestion control on main source
     NadaCongestionControlHelper nada;
     nada.Install(sourceRouter.Get(0));

     // Set up routing
     Ipv4GlobalRoutingHelper::PopulateRoutingTables();

     // Create UDP applications
     uint16_t port = 9;

     // Main source with NADA
     InetSocketAddress destAddr(bottleneckIfaces.GetAddress(1), port);
     NS_LOG_INFO("Destination address: " << destAddr.GetIpv4() << ":" << destAddr.GetPort());

     // Calculate interval for 1Mbps with given packet size
     // Interval = (packet_size * 8) / (1 * 10^6) seconds
     double interval = (packetSize * 8.0) / 1000000.0;
     NS_LOG_INFO("Setting interval to " << interval << "s to achieve 1Mbps");

     UdpNadaClientHelper nadaClient(destAddr);
     nadaClient.SetAttribute("PacketSize", UintegerValue(packetSize));
     nadaClient.SetAttribute("MaxPackets", UintegerValue(0)); // Unlimited
     nadaClient.SetAttribute("Interval", TimeValue(Seconds(interval)));

     ApplicationContainer sourceApp = nadaClient.Install(sourceRouter.Get(0));
     sourceApp.Start(Seconds(1.0));
     sourceApp.Stop(Seconds(simulationTime - 1));

     // Competing sources with regular UDP (fixed rate)
     UdpClientHelper competingClient(bottleneckIfaces.GetAddress(1), port);
     competingClient.SetAttribute("PacketSize", UintegerValue(packetSize));
     competingClient.SetAttribute("MaxPackets", UintegerValue(0)); // Unlimited
     competingClient.SetAttribute("Interval", TimeValue(Seconds(interval))); // Same 1Mbps rate

     ApplicationContainer competingApps[numCompetingSources];
     for (uint32_t i = 0; i < numCompetingSources; i++) {
         competingApps[i] = competingClient.Install(competingSources.Get(i));
         competingApps[i].Start(Seconds(5.0 + i)); // Staggered start
         competingApps[i].Stop(Seconds(simulationTime - 1));
     }

     // UDP receiver at destination
     UdpServerHelper server(port);
     ApplicationContainer serverApp = server.Install(destinationRouter.Get(0));
     serverApp.Start(Seconds(0.5));
     serverApp.Stop(Seconds(simulationTime));

     // Set up flow monitor
     Ptr<FlowMonitor> flowMonitor;
     FlowMonitorHelper flowHelper;
     flowMonitor = flowHelper.InstallAll();

     // Run simulation
     NS_LOG_INFO("Starting simulation for " << simulationTime << " seconds");
     Simulator::Stop(Seconds(simulationTime));
     Simulator::Run();

     // Process flow monitor statistics
     NS_LOG_INFO("Processing flow statistics...");
     flowMonitor->CheckForLostPackets();
     Ptr<Ipv4FlowClassifier> classifier =
         DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
     std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

     for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
          i != stats.end(); ++i) {
         Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

         // Skip if it's not our traffic of interest (e.g., routing protocol traffic)
         if (i->second.rxPackets == 0 ||
             (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) < 1.0) {
             continue;
         }

         std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
                   << t.destinationAddress << ")\n";
         std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
         std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
         std::cout << "  Throughput: "
                   << i->second.rxBytes * 8.0 /
                          (i->second.timeLastRxPacket.GetSeconds() -
                           i->second.timeFirstTxPacket.GetSeconds()) / 1000000
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
