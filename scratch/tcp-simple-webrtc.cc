#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WebRtcWithoutNadaSimulation");

double
AddJitter(double baseValue, double jitterPercent)
{
    // Create a uniform random variable with +/- jitter percent
    static Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double jitterFactor = 1.0 + rng->GetValue(-jitterPercent, jitterPercent);
    return baseValue * jitterFactor;
}

void
SendVideoFrame(Ptr<Socket> socket,
               uint32_t& frameCount,
               uint32_t frameSize,
               Time frameInterval,
               uint32_t keyFrameInterval,
               uint32_t& totalPacketsSent,
               uint32_t maxPackets)
{
    if (!socket || totalPacketsSent >= maxPackets)
    {
        return;
    }

    // Determine if this is a key frame
    bool isKeyFrame = (frameCount % keyFrameInterval == 0);

    // Key frames are larger (similar to the NADA simulation)
    uint32_t currentFrameSize = isKeyFrame ? frameSize * 1.5 : frameSize;

    NS_LOG_INFO("Frame #" << frameCount << " (" << (isKeyFrame ? "key" : "delta") << ") of size "
                          << currentFrameSize << " bytes");

    // Fragment the frame into MTU-sized packets
    uint32_t mtu = 1400; // Standard MTU size allowing for headers
    uint32_t numPackets = (currentFrameSize + mtu - 1) / mtu; // Ceiling division
    uint32_t remainingBytes = currentFrameSize;

    // Calculate time to transmit frame (simple pacing)
    double frameTimeSeconds =
        frameInterval.GetSeconds() * 0.8; // Use 80% of frame interval for transmission

    // Minimum spacing between packets (about 2ms per packet for large frames)
    double minPacketSpacing = 0.000000001; // 1 nanoseconds
    double packetInterval = std::max(minPacketSpacing, frameTimeSeconds / numPackets);

    NS_LOG_INFO("Fragmenting frame into " << numPackets << " packets with " << packetInterval * 1000
                                          << "ms spacing");

    uint32_t packetsToSend = std::min(numPackets, maxPackets - totalPacketsSent);
    if (packetsToSend < numPackets)
    {
        NS_LOG_INFO("Capping packet count to meet budget: " << packetsToSend << " instead of "
                                                            << numPackets);
    }

    // Send fragments with proper spacing
    for (uint32_t i = 0; i < packetsToSend; i++)
    {
        uint32_t packetSize = std::min(mtu, remainingBytes);
        Ptr<Packet> packet = Create<Packet>(packetSize);

        if (i == 0)
        {
            socket->Send(packet);
            totalPacketsSent++;
        }
        else
        {
            // Capture counter by value for lambda
            uint32_t* pCounter = &totalPacketsSent;
            double jitteredInterval = AddJitter(packetInterval, 0.05);
            Time sendTime = Seconds(std::max(i * jitteredInterval, i * minPacketSpacing));

            Simulator::Schedule(sendTime, [socket, packet, pCounter](void) {
                socket->Send(packet);
                (*pCounter)++;
            });
        }
        remainingBytes -= packetSize;
    }

    // Schedule the next frame with slight jitter to avoid synchronization
    Time jitteredFrameInterval = Seconds(AddJitter(frameInterval.GetSeconds(), 0.05));
    if (totalPacketsSent < maxPackets)
    {
        Simulator::Schedule(jitteredFrameInterval,
                            &SendVideoFrame,
                            socket,
                            ++frameCount,
                            frameSize,
                            frameInterval,
                            keyFrameInterval,
                            std::ref(totalPacketsSent),
                            maxPackets);
    }
}

int
main(int argc, char* argv[])
{
    uint32_t packetSize = 1000;           // bytes
    std::string dataRate = "1024Mbps";    // Higher access link capacity
    std::string bottleneckBw = "500Mbps"; // Bottleneck bandwidth
    uint32_t delayMs = 25;                // Base delay
    uint32_t simulationTime = 60;         // Simulation time in seconds
    uint32_t numCompetingSources = 2;     // Competing sources
    uint32_t frameSize = 1200;            // Size of video frame
    uint32_t frameRate = 15;              // Video frame rate
    uint32_t keyFrameInterval = 100;      // Key frame interval (matches NADA simulation)
    uint32_t queueSize = 200;             // Queue size in packets
    std::string queueDisc = "CoDel";      // Queue discipline (CoDel, PfifoFast, PIE)
    bool enableAqm = false;               // Enable AQM by default
    uint32_t maxTotalPackets = 10000;     // Default target packet count

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of packets to send", packetSize);
    cmd.AddValue("dataRate", "Data rate of primary source", dataRate);
    cmd.AddValue("bottleneckBw", "Bottleneck link bandwidth", bottleneckBw);
    cmd.AddValue("delayMs", "Link delay in milliseconds", delayMs);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("numCompetingSources", "Number of competing traffic sources", numCompetingSources);
    cmd.AddValue("frameSize", "Size of video frame", frameSize);
    cmd.AddValue("frameRate", "Video frame rate", frameRate);
    cmd.AddValue("keyFrameInterval", "Interval between key frames", keyFrameInterval);
    cmd.AddValue("queueSize", "Queue size in packets", queueSize);
    cmd.AddValue("queueDisc", "Queue discipline (CoDel, PfifoFast, PIE)", queueDisc);
    cmd.AddValue("enableAqm", "Enable Active Queue Management", enableAqm);
    cmd.AddValue("maxPackets",
                 "Maximum total packets to send across the simulation",
                 maxTotalPackets);
    cmd.Parse(argc, argv);

    // Calculate frame interval based on frame rate
    Time frameInterval = Seconds(1.0 / frameRate);

    // Configure output
    Time::SetResolution(Time::NS);
    LogComponentEnable("WebRtcWithoutNadaSimulation", LOG_LEVEL_INFO);

    // Set queue size
    Config::SetDefault("ns3::PointToPointNetDevice::TxQueue",
                       StringValue("ns3::DropTailQueue<Packet>"));
    Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize",
                       QueueSizeValue(QueueSize(std::to_string(queueSize) + "p")));

    // Configure TCP options - using NewReno as the default congestion control algorithm
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));

    // Set TCP segment size
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1000));

    // Explicitly enable window scaling
    Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));

    // Make sure the TCP senders have enough data in their buffers
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

    // Setup AQM if enabled
    if (enableAqm)
    {
        NS_LOG_INFO("Installing " << queueDisc << " queue discipline on bottleneck link");
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

        // Install on bottleneck link
        QueueDiscContainer qdiscs;
        qdiscs.Add(tch.Install(intermediateRouter.Get(0)->GetDevice(1)));
        qdiscs.Add(tch.Install(destinationRouter.Get(0)->GetDevice(0)));
    }

    // Create connections for additional sources
    NetDeviceContainer additionalToIntermediate[numCompetingSources];
    for (uint32_t i = 0; i < numCompetingSources; i++)
    {
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

    // Set up routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Create a UDP socket for the main source
    uint16_t port = 9;
    Ptr<Socket> sourceSocket =
        Socket::CreateSocket(sourceRouter.Get(0), UdpSocketFactory::GetTypeId());
    sourceSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
    sourceSocket->Connect(InetSocketAddress(bottleneckIfaces.GetAddress(1), port));

    uint16_t tcpPort = 50000;
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), tcpPort));
    ApplicationContainer sinkApps = sinkHelper.Install(destinationRouter.Get(0));
    sinkApps.Start(Seconds(0.5));
    sinkApps.Stop(Seconds(simulationTime));

    // Create TCP bulk send applications for competing sources
    ApplicationContainer competingApps[numCompetingSources];
    for (uint32_t i = 0; i < numCompetingSources; i++)
    {
        BulkSendHelper source("ns3::TcpSocketFactory",
                              InetSocketAddress(bottleneckIfaces.GetAddress(1), tcpPort));

        // Set the amount of data to send (in bytes)
        source.SetAttribute("MaxBytes", UintegerValue(0)); // Unlimited
        source.SetAttribute("SendSize", UintegerValue(packetSize));

        competingApps[i] = source.Install(additionalSources.Get(i));

        // Start later and staggered as in the original code
        competingApps[i].Start(Seconds(15.0 + i * 8));
        competingApps[i].Stop(Seconds(simulationTime - 5 - i));
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

    uint32_t totalPacketsSent = 0;
    uint32_t frameCount = 0;
    Simulator::Schedule(Seconds(1.0),
                        &SendVideoFrame,
                        sourceSocket,
                        std::ref(frameCount),
                        frameSize,
                        frameInterval,
                        keyFrameInterval,
                        std::ref(totalPacketsSent),
                        maxTotalPackets);

    // Run the simulation
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // Process flow monitor statistics
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

    std::cout << "\n=== SIMULATION RESULTS ===\n";
    std::cout << "Simulation time: " << simulationTime << " seconds\n";
    std::cout << "WebRTC mode: enabled (without congestion control)\n";
    std::cout << "  Frame rate: " << frameRate << " fps\n";
    std::cout << "  Key frame interval: " << keyFrameInterval << " frames\n";
    std::cout << "Bottleneck bandwidth: " << bottleneckBw << "\n";
    std::cout << "RTT: " << (delayMs * 4) << " ms\n";
    std::cout << "Queue discipline: " << queueDisc << "\n";
    std::cout << "Queue size: " << queueSize << " packets\n\n";

    // Track totals for summary
    uint32_t totalTxPackets = 0;
    uint32_t totalRxPackets = 0;
    uint64_t totalTxBytes = 0;
    uint64_t totalRxBytes = 0;

    // Track WebRTC flow statistics separately
    uint32_t webrtcTxPackets = 0;
    uint32_t webrtcRxPackets = 0;
    uint64_t webrtcTxBytes = 0;
    uint64_t webrtcRxBytes = 0;

    // Track TCP flow statistics separately
    uint32_t tcpTxPackets = 0;
    uint32_t tcpRxPackets = 0;
    uint64_t tcpTxBytes = 0;
    uint64_t tcpRxBytes = 0;

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

        // Check if this is the WebRTC flow (from source router)
        bool isWebRtcFlow = (t.sourceAddress == sourceRouterIfaces.GetAddress(0));

        // Detect ACK flows - from destination router back to sources or TCP server port
        bool isAck = (t.sourceAddress == bottleneckIfaces.GetAddress(1) ||
                      (t.protocol == 6 && t.sourcePort == tcpPort));

        // Skip ACK flows in our statistics
        if (isAck)
        {
            continue;
        }

        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
                  << t.destinationAddress << ")" << (isWebRtcFlow ? " [WebRTC]" : "") << "\n";

        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
        std::cout << "  Tx Bytes: " << i->second.txBytes << "\n";
        std::cout << "  Rx Bytes: " << i->second.rxBytes << "\n";

        // Update overall statistics
        totalTxPackets += i->second.txPackets;
        totalRxPackets += i->second.rxPackets;
        totalTxBytes += i->second.txBytes;
        totalRxBytes += i->second.rxBytes;

        // Update protocol-specific statistics
        if (isWebRtcFlow)
        {
            webrtcTxPackets += i->second.txPackets;
            webrtcRxPackets += i->second.rxPackets;
            webrtcTxBytes += i->second.txBytes;
            webrtcRxBytes += i->second.rxBytes;
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
    // Print summary statistics
    std::cout << "=== OVERALL STATISTICS ===\n";
    std::cout << "Total Tx Packets: " << totalTxPackets << "\n";
    std::cout << "Total Rx Packets: " << totalRxPackets << "\n";

    if (totalTxPackets > 0)
    {
        std::cout << "Overall packet loss: "
                  << 100.0 * (totalTxPackets - totalRxPackets) / totalTxPackets << "%\n";
    }
    else
    {
        std::cout << "Overall packet loss: N/A (no transmitted packets)\n";
    }

    std::cout << "Total Tx Bytes: " << totalTxBytes << "\n";
    std::cout << "Total Rx Bytes: " << totalRxBytes << "\n";

    if (totalTxBytes > 0)
    {
        std::cout << "Average network efficiency: " << 100.0 * totalRxBytes / totalTxBytes
                  << "%\n\n";
    }
    else
    {
        std::cout << "Average network efficiency: N/A (no transmitted bytes)\n\n";
    }

    // Print WebRTC specific statistics
    std::cout << "=== WebRTC STATISTICS ===\n";
    std::cout << "WebRTC Tx Packets: " << webrtcTxPackets << "\n";
    std::cout << "WebRTC Rx Packets: " << webrtcRxPackets << "\n";
    if (webrtcTxPackets > 0)
    {
        std::cout << "WebRTC packet loss: "
                  << 100.0 * (webrtcTxPackets - webrtcRxPackets) / webrtcTxPackets << "%\n";
    }
    else
    {
        std::cout << "WebRTC packet loss: N/A (no transmitted packets)\n";
    }
    std::cout << "WebRTC Tx Bytes: " << webrtcTxBytes << "\n";
    std::cout << "WebRTC Rx Bytes: " << webrtcRxBytes << "\n";
    if (webrtcTxBytes > 0)
    {
        std::cout << "WebRTC efficiency: " << 100.0 * webrtcRxBytes / webrtcTxBytes << "%\n";
    }
    else
    {
        std::cout << "WebRTC efficiency: N/A (no transmitted bytes)\n";
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
    else
    {
        std::cout << "TCP packet loss: N/A (no transmitted packets)\n";
    }
    std::cout << "TCP Tx Bytes: " << tcpTxBytes << "\n";
    std::cout << "TCP Rx Bytes: " << tcpRxBytes << "\n";
    if (tcpTxBytes > 0)
    {
        std::cout << "TCP efficiency: " << 100.0 * tcpRxBytes / tcpTxBytes << "%\n";
    }
    else
    {
        std::cout << "TCP efficiency: N/A (no transmitted bytes)\n";
    }
    std::cout << "\n";

    Simulator::Destroy();
    return 0;
}
