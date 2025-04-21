/*
 * Test script for NADA congestion control
 * This script tests the NADA implementation under various network conditions
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include "ns3/nada-header.h"
#include "ns3/nada-udp-client.h"
#include "ns3/nada-improved.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("NadaTest");

int
main(int argc, char* argv[])
{
    std::string testScenario = "baseline";
    uint32_t simulationTime = 120;

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("scenario", "Test scenario (baseline, competing, variable)", testScenario);
    cmd.AddValue("time", "Simulation time in seconds", simulationTime);
    cmd.Parse(argc, argv);

    // Configure output
    Time::SetResolution(Time::NS);
    LogComponentEnable("NadaTest", LOG_LEVEL_INFO);

    // Create nodes
    NodeContainer nodes;
    nodes.Create(4); // source, router1, router2, destination

    // Create the point-to-point links
    PointToPointHelper p2p1;
    p2p1.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p1.SetChannelAttribute("Delay", TimeValue(MilliSeconds(10)));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    bottleneck.SetChannelAttribute("Delay", TimeValue(MilliSeconds(50)));

    // Install the links
    NetDeviceContainer devices1 = p2p1.Install(nodes.Get(0), nodes.Get(1));
    NetDeviceContainer bottleneckDevices = bottleneck.Install(nodes.Get(1), nodes.Get(2));
    NetDeviceContainer devices3 = p2p1.Install(nodes.Get(2), nodes.Get(3));

    // Install the internet stack
    InternetStackHelper internet;
    internet.Install(nodes);

    // Configure IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces1 = ipv4.Assign(devices1);

    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckInterfaces = ipv4.Assign(bottleneckDevices);

    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces3 = ipv4.Assign(devices3);

    // Set up routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Install NADA on source node
    NadaCongestionControlHelper nada;
    nada.Install(nodes.Get(0));

    // Create a NADA UDP client at the source
    uint16_t port = 9;
    Address serverAddress(InetSocketAddress(interfaces3.GetAddress(1), port));
    UdpNadaClientHelper client(serverAddress);
    client.SetAttribute("PacketSize", UintegerValue(1000));

    ApplicationContainer clientApp = client.Install(nodes.Get(0));
    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simulationTime - 1));

    // Create a UDP server at the destination
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(nodes.Get(3));
    serverApp.Start(Seconds(0.5));
    serverApp.Stop(Seconds(simulationTime));

    // Configure the test scenario
    if (testScenario == "competing")
    {
        // Create competing traffic sources
        UdpClientHelper competingClient(interfaces3.GetAddress(1), port);
        competingClient.SetAttribute("PacketSize", UintegerValue(1000));
        competingClient.SetAttribute("Interval", TimeValue(Seconds(0.001))); // ~8Mbps

        // Install on router1 to create bottleneck competition
        ApplicationContainer competingApp = competingClient.Install(nodes.Get(1));
        competingApp.Start(Seconds(30.0)); // Start after NADA has stabilized
        competingApp.Stop(Seconds(simulationTime - 30));
    }
    else if (testScenario == "variable")
    {
        // Create a trace to change bottleneck bandwidth over time
        Ptr<NetDevice> device = bottleneckDevices.Get(0);
        Ptr<PointToPointNetDevice> p2pDevice = DynamicCast<PointToPointNetDevice>(device);

        // Schedule bandwidth changes
        Simulator::Schedule(Seconds(40),
                            &PointToPointNetDevice::SetDataRate,
                            p2pDevice,
                            DataRate("2Mbps"));
        Simulator::Schedule(Seconds(80),
                            &PointToPointNetDevice::SetDataRate,
                            p2pDevice,
                            DataRate("8Mbps"));

        NS_LOG_INFO("Variable bandwidth scenario: 5Mbps -> 2Mbps -> 8Mbps");
    }

    // Set up flow monitor
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    // Create throughput tracer
    AsciiTraceHelper ascii;
    bottleneck.EnableAsciiAll(ascii.CreateFileStream("nada-bottleneck.tr"));

    // Run the simulation
    NS_LOG_INFO("Running simulation for " << simulationTime << " seconds...");
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // Print flow monitor statistics
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    NS_LOG_INFO("Results for scenario: " << testScenario);
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        // Skip non-UDP flows or flows not ending at our server
        if (t.protocol != 17 || t.destinationPort != port)
        {
            continue;
        }

        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
                  << t.destinationAddress << ")\n";
        std::cout << "  Tx packets: " << i->second.txPackets << "\n";
        std::cout << "  Rx packets: " << i->second.rxPackets << "\n";
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
        std::cout << "  Jitter: " << i->second.jitterSum.GetSeconds() / (i->second.rxPackets - 1)
                  << " seconds\n";
    }

    Simulator::Destroy();
    return 0;
}
