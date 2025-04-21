/*
 * NS-3 Simulation of WebRTC without NADA Congestion Control
 * Topology: Source Router -> Intermediate Router (with additional sources) -> Destination Router
 * Using UDP transport to send WebRTC video packets directly without congestion control.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WebRtcWithoutNadaSimulation");

// Function to send WebRTC video frames
void
SendVideoFrame(Ptr<Socket> socket, uint32_t frameCount, uint32_t frameSize, Time frameInterval)
{
    if (!socket)
    {
        return;
    }

    // Create a video frame packet
    Ptr<Packet> packet = Create<Packet>(frameSize);
    socket->Send(packet);

    NS_LOG_INFO("Sent frame #" << frameCount << " of size " << frameSize << " bytes");

    // Schedule the next frame
    Simulator::Schedule(frameInterval,
                        &SendVideoFrame,
                        socket,
                        frameCount + 1,
                        frameSize,
                        frameInterval);
}

int
main(int argc, char* argv[])
{
    uint32_t packetSize = 1000;                    // bytes
    std::string dataRate = "1024Mbps";             // Higher access link capacity
    std::string bottleneckBw = "500Mbps";          // Typical home internet upload
    uint32_t delayMs = 25;                         // Reduced base delay
    uint32_t simulationTime = 60;                  // Simulation time in seconds
    uint32_t numCompetingSources = 2;              // Fewer competing sources
    uint32_t frameSize = 1200;                     // Size of video frame
    uint32_t frameRate = 15;                       // Video frame rate
    Time frameInterval = Seconds(1.0 / frameRate); // Interval between frames

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of packets to send", packetSize);
    cmd.AddValue("dataRate", "Data rate of primary source", dataRate);
    cmd.AddValue("bottleneckBw", "Bottleneck link bandwidth", bottleneckBw);
    cmd.AddValue("delayMs", "Link delay in milliseconds", delayMs);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("numCompetingSources", "Number of competing traffic sources", numCompetingSources);
    cmd.AddValue("frameSize", "Size of video frame", frameSize);
    cmd.Parse(argc, argv);

    // Configure output
    Time::SetResolution(Time::NS);
    LogComponentEnable("WebRtcWithoutNadaSimulation", LOG_LEVEL_INFO);

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
        additionalSourceIfaces[i] = ipv4.Assign(additionalSources.Get(i)->GetDevice(0));
    }

    // Create a socket for the main source
    Ptr<Socket> sourceSocket =
        Socket::CreateSocket(sourceRouter.Get(0), UdpSocketFactory::GetTypeId());
    sourceSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), 9));
    sourceSocket->Connect(InetSocketAddress(bottleneckIfaces.GetAddress(0), 9));

    // Start sending video frames
    Simulator::Schedule(Seconds(0.0), &SendVideoFrame, sourceSocket, 0, frameSize, frameInterval);

    // Run the simulation
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
