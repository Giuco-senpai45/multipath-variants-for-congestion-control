#include "agg-path-nada.h"

#include "nada-header.h"

#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("AggregatePathNadaClient");
NS_OBJECT_ENSURE_REGISTERED(AggregatePathNadaClient);

TypeId
AggregatePathNadaClient::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::AggregatePathNadaClient")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<AggregatePathNadaClient>()
            .AddAttribute("PacketSize",
                          "Size of packets generated",
                          UintegerValue(1024),
                          MakeUintegerAccessor(&AggregatePathNadaClient::m_packetSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("MaxPackets",
                          "Maximum number of packets to send",
                          UintegerValue(100),
                          MakeUintegerAccessor(&AggregatePathNadaClient::m_maxPackets),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Interval",
                          "Time between packets (will be updated by NADA)",
                          TimeValue(MilliSeconds(100)),
                          MakeTimeAccessor(&AggregatePathNadaClient::m_interval),
                          MakeTimeChecker());
    return tid;
}

AggregatePathNadaClient::AggregatePathNadaClient()
    : m_packetSize(1024),
      m_maxPackets(100),
      m_totalPacketsSent(0),
      m_currentPathIndex(0),
      m_running(false),
      m_interval(MilliSeconds(100)),
      m_videoMode(false),
      m_isKeyFrame(false),
      m_totalRate(DataRate("1Mbps")),
      m_totalLinkCapacity(0)
{
    NS_LOG_FUNCTION(this);
    m_nada = CreateObject<NadaCongestionControl>();
}

AggregatePathNadaClient::~AggregatePathNadaClient()
{
    NS_LOG_FUNCTION(this);
}

bool
AggregatePathNadaClient::AddPath(uint32_t pathId, Address localAddress, Address remoteAddress)
{
    NS_LOG_FUNCTION(this << pathId << localAddress << remoteAddress);

    if (pathId < 1 || pathId > 2)
    {
        NS_LOG_ERROR("Path ID must be 1 or 2, got: " << pathId);
        return false;
    }

    if (m_paths.find(pathId) != m_paths.end())
    {
        NS_LOG_ERROR("Path " << pathId << " already exists");
        return false;
    }

    PathInfo pathInfo;
    pathInfo.socket = nullptr;
    pathInfo.localAddress = localAddress;
    pathInfo.remoteAddress = remoteAddress;
    pathInfo.packetsSent = 0;
    pathInfo.packetsAcked = 0;
    pathInfo.lastRtt = MilliSeconds(100); // Default RTT
    pathInfo.baseRtt = MilliSeconds(50);  // Default base RTT
    pathInfo.rttSamples.clear();
    pathInfo.linkCapacity = DataRate("1Mbps");

    m_paths[pathId] = pathInfo;
    NS_LOG_INFO("Added path " << pathId);
    return true;
}

void
AggregatePathNadaClient::SetPacketSize(uint32_t size)
{
    NS_LOG_FUNCTION(this << size);
    m_packetSize = size;
    NS_LOG_DEBUG("Set packet size to " << size << " bytes");
}

void
AggregatePathNadaClient::SetMaxPackets(uint32_t maxPackets)
{
    NS_LOG_FUNCTION(this << maxPackets);
    m_maxPackets = maxPackets;
}

void
AggregatePathNadaClient::SetPathCapacities(DataRate path1Capacity, DataRate path2Capacity)
{
    NS_LOG_FUNCTION(this << path1Capacity << path2Capacity);

    // Store individual path capacities
    auto path1It = m_paths.find(1);
    auto path2It = m_paths.find(2);

    if (path1It != m_paths.end())
    {
        path1It->second.linkCapacity = path1Capacity;
    }

    if (path2It != m_paths.end())
    {
        path2It->second.linkCapacity = path2Capacity;
    }

    // Calculate total link capacity
    m_totalLinkCapacity = path1Capacity.GetBitRate() + path2Capacity.GetBitRate();

    NS_LOG_INFO("Set path capacities: Path1="
                << path1Capacity.GetBitRate() / 1000000.0
                << "Mbps, Path2=" << path2Capacity.GetBitRate() / 1000000.0
                << "Mbps, Total=" << m_totalLinkCapacity / 1000000.0 << "Mbps");
}

bool
AggregatePathNadaClient::Send(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this);

    if (!m_running)
    {
        return false;
    }

    if (!IsReady())
    {
        NS_LOG_WARN("Paths not ready for sending");
        return false;
    }

    // **SIMPLIFIED: Just do round-robin path selection**
    std::vector<uint32_t> availablePaths;
    for (auto& pathPair : m_paths)
    {
        if (pathPair.second.socket)
        {
            availablePaths.push_back(pathPair.first);
        }
    }

    if (availablePaths.empty())
    {
        NS_LOG_ERROR("No available paths for sending");
        return false;
    }

    uint32_t selectedPath = availablePaths[m_currentPathIndex % availablePaths.size()];
    m_currentPathIndex++;

    bool sent = SendOnPath(selectedPath, packet);
    if (sent)
    {
        m_totalPacketsSent++;
    }

    return sent;
}

bool
AggregatePathNadaClient::SendOnPath(uint32_t pathId, Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << pathId);

    auto it = m_paths.find(pathId);
    if (it == m_paths.end() || !it->second.socket)
    {
        NS_LOG_ERROR("Invalid path or socket for path " << pathId);
        return false;
    }

    // **CRITICAL: Use IDENTICAL header format as multipath**
    NadaHeader header;
    header.SetSequenceNumber(m_totalPacketsSent);
    header.SetTimestamp(MicroSeconds(Simulator::Now().GetMicroSeconds()));

    // **IDENTICAL: Use same video frame logic as multipath**
    if (m_videoMode)
    {
        header.SetVideoFrameType(m_isKeyFrame ? 0 : 1); // 0=key, 1=delta
        header.SetVideoFrameSize(packet->GetSize());
    }

    packet->AddHeader(header);

    try
    {
        int sent = it->second.socket->Send(packet);
        if (sent > 0)
        {
            it->second.packetsSent++;
            return true;
        }
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception sending packet on path " << pathId << ": " << e.what());
    }

    return false;
}

DataRate
AggregatePathNadaClient::GetCurrentRate() const
{
    return m_nada->UpdateRate();
}

Time
AggregatePathNadaClient::GetAggregatedRtt()
{
    return CalculateAggregatedRtt();
}

bool
AggregatePathNadaClient::IsReady() const
{
    if (m_paths.size() != 2)
    {
        return false;
    }

    for (const auto& pathPair : m_paths)
    {
        if (!pathPair.second.socket)
        {
            return false;
        }
    }

    return m_running;
}

std::map<std::string, double>
AggregatePathNadaClient::GetPathStats(uint32_t pathId) const
{
    std::map<std::string, double> stats;

    auto it = m_paths.find(pathId);
    if (it != m_paths.end())
    {
        stats["packets_sent"] = it->second.packetsSent;
        stats["packets_acked"] = it->second.packetsAcked;
        stats["last_rtt_ms"] = it->second.lastRtt.GetMilliSeconds();
        stats["base_rtt_ms"] = it->second.baseRtt.GetMilliSeconds();
    }

    return stats;
}

void
AggregatePathNadaClient::DoDispose(void)
{
    NS_LOG_FUNCTION(this);

    if (m_sendEvent.IsPending())
    {
        Simulator::Cancel(m_sendEvent);
    }
    if (m_updateEvent.IsPending())
    {
        Simulator::Cancel(m_updateEvent);
    }

    for (auto& pathPair : m_paths)
    {
        if (pathPair.second.socket)
        {
            pathPair.second.socket->Close();
            pathPair.second.socket = nullptr;
        }
    }

    m_nada = nullptr;
    Application::DoDispose();
}

void
AggregatePathNadaClient::StartApplication(void)
{
    NS_LOG_FUNCTION(this);

    if (m_paths.size() != 2)
    {
        NS_LOG_ERROR("Exactly 2 paths must be configured before starting");
        return;
    }

    m_running = true;
    m_videoMode = true;

    // Initialize sockets for both paths
    for (auto& pathPair : m_paths)
    {
        InitializePathSocket(pathPair.first);
        m_nada->SetVideoMode(true);
    }

    // Initialize NADA with video mode
    if (!m_paths.empty())
    {
        m_nada->Init(m_paths.begin()->second.socket);
        m_nada->SetVideoMode(true);
    }

    NS_LOG_INFO("AggregatePathNadaClient ready - FRAME-BASED TRANSMISSION ONLY");
    std::cout << "**DEBUG: AggregatePathNadaClient - NO continuous transmission active" << std::endl;
    std::cout << "**DEBUG: No UpdateNadaMetrics scheduled" << std::endl;
    std::cout << "**DEBUG: Client will ONLY respond to SendAggregateVideoFrame calls" << std::endl;
}

void
AggregatePathNadaClient::StopApplication(void)
{
    NS_LOG_FUNCTION(this);
    m_running = false;

    if (m_sendEvent.IsPending())
    {
        Simulator::Cancel(m_sendEvent);
    }
    if (m_updateEvent.IsPending())
    {
        Simulator::Cancel(m_updateEvent);
    }
}

void
AggregatePathNadaClient::InitializePathSocket(uint32_t pathId)
{
    NS_LOG_FUNCTION(this << pathId);

    auto it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        NS_LOG_ERROR("Path " << pathId << " not found");
        return;
    }

    // Create UDP socket
    Ptr<Socket> socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());

    // Bind to local address
    if (InetSocketAddress::IsMatchingType(it->second.localAddress))
    {
        InetSocketAddress localAddr = InetSocketAddress::ConvertFrom(it->second.localAddress);
        socket->Bind(localAddr);
    }
    else if (Inet6SocketAddress::IsMatchingType(it->second.localAddress))
    {
        Inet6SocketAddress localAddr = Inet6SocketAddress::ConvertFrom(it->second.localAddress);
        socket->Bind(localAddr);
    }

    // Connect to remote address
    if (InetSocketAddress::IsMatchingType(it->second.remoteAddress))
    {
        InetSocketAddress remoteAddr = InetSocketAddress::ConvertFrom(it->second.remoteAddress);
        socket->Connect(remoteAddr);
    }
    else if (Inet6SocketAddress::IsMatchingType(it->second.remoteAddress))
    {
        Inet6SocketAddress remoteAddr = Inet6SocketAddress::ConvertFrom(it->second.remoteAddress);
        socket->Connect(remoteAddr);
    }

    // Set receive callback
    socket->SetRecvCallback(MakeCallback(&AggregatePathNadaClient::HandleRecv, this));

    // Store socket
    it->second.socket = socket;
    m_socketToPathId[socket] = pathId;

    NS_LOG_INFO("Initialized socket for path " << pathId);
}

void
AggregatePathNadaClient::HandleRecv(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    auto it = m_socketToPathId.find(socket);
    if (it == m_socketToPathId.end())
    {
        NS_LOG_WARN("Received packet from unknown socket");
        return;
    }

    uint32_t pathId = it->second;
    auto pathIt = m_paths.find(pathId);
    if (pathIt == m_paths.end())
    {
        return;
    }

    Address from;
    Ptr<Packet> packet = socket->RecvFrom(from);
    if (!packet)
    {
        return;
    }

    // Try to extract NADA header for RTT calculation
    if (packet->GetSize() >= NadaHeader::GetStaticSize())
    {
        try
        {
            NadaHeader header;
            packet->RemoveHeader(header);

            Time currentTime = Simulator::Now();
            Time rtt = currentTime - header.GetTimestamp();

            // Update path RTT information
            pathIt->second.lastRtt = rtt;
            pathIt->second.packetsAcked++;

            // Store RTT sample for aggregation calculation
            pathIt->second.rttSamples.push_back(rtt);
            if (pathIt->second.rttSamples.size() > 10) // Keep last 10 samples
            {
                pathIt->second.rttSamples.erase(pathIt->second.rttSamples.begin());
            }

            // Update base RTT (minimum observed)
            if (rtt < pathIt->second.baseRtt)
            {
                pathIt->second.baseRtt = rtt;
            }

            NS_LOG_DEBUG("Path " << pathId << " RTT: " << rtt.GetMilliSeconds() << "ms");
        }
        catch (const std::exception& e)
        {
            NS_LOG_WARN("Failed to process NADA header: " << e.what());
        }
    }
}

void
AggregatePathNadaClient::UpdateNadaMetrics()
{
    NS_LOG_FUNCTION(this);

    if (!m_running)
    {
        return;
    }

    // Calculate aggregated RTT and update NADA
    Time aggregatedRtt = CalculateAggregatedRtt();

    // Process the aggregated RTT in NADA
    m_nada->ProcessAck(Create<Packet>(0), aggregatedRtt);

    // Calculate aggregated loss rate (simplified)
    double totalLossRate = 0.0;
    uint32_t pathCount = 0;

    for (const auto& pathPair : m_paths)
    {
        if (pathPair.second.packetsSent > 0)
        {
            double pathLoss =
                1.0 - (double(pathPair.second.packetsAcked) / pathPair.second.packetsSent);
            totalLossRate += pathLoss;
            pathCount++;
        }
    }

    if (pathCount > 0)
    {
        double avgLossRate = totalLossRate / pathCount;
        m_nada->ProcessLoss(avgLossRate);
    }

    // Update cached total rate
    m_totalRate = GetTotalRate();

    NS_LOG_DEBUG("Updated NADA with aggregated RTT: "
                 << aggregatedRtt.GetMilliSeconds()
                 << "ms, loss rate: " << (pathCount > 0 ? totalLossRate / pathCount : 0.0)
                 << ", total rate: " << m_totalRate.GetBitRate() / 1000000.0 << " Mbps");

    // Schedule next update
    // m_updateEvent =
    //     Simulator::Schedule(MilliSeconds(100), &AggregatePathNadaClient::UpdateNadaMetrics, this);
}

Time
AggregatePathNadaClient::CalculateAggregatedRtt()
{
    NS_LOG_FUNCTION(this);

    if (m_paths.size() != 2)
    {
        return MilliSeconds(100); // Default value
    }

    auto path1It = m_paths.find(1);
    auto path2It = m_paths.find(2);

    if (path1It == m_paths.end() || path2It == m_paths.end())
    {
        return MilliSeconds(100);
    }

    // Calculate RTT differences for each path
    // RTT difference = current_rtt - base_rtt
    Time path1Diff = path1It->second.lastRtt - path1It->second.baseRtt;
    Time path2Diff = path2It->second.lastRtt - path2It->second.baseRtt;

    // Aggregated RTT = (path1_diff + path2_diff) / 2
    Time aggregatedRtt = (path1Diff + path2Diff) / 2;

    // Ensure we have a reasonable minimum RTT
    if (aggregatedRtt < MilliSeconds(1))
    {
        aggregatedRtt = MilliSeconds(1);
    }

    NS_LOG_DEBUG("Path1 diff: " << path1Diff.GetMilliSeconds() << "ms, "
                                << "Path2 diff: " << path2Diff.GetMilliSeconds() << "ms, "
                                << "Aggregated: " << aggregatedRtt.GetMilliSeconds() << "ms");

    return aggregatedRtt;
}

bool
AggregatePathNadaClient::IsConnected() const
{
    NS_LOG_FUNCTION(this);

    if (!m_running)
    {
        NS_LOG_DEBUG("Client not running, not connected");
        return false;
    }

    if (m_paths.empty())
    {
        NS_LOG_DEBUG("No paths configured, not connected");
        return false;
    }

    // Check if at least one path has a valid socket
    for (const auto& pathPair : m_paths)
    {
        if (pathPair.second.socket)
        {
            try
            {
                // Check if socket is in a valid state
                Socket::SocketErrno error = pathPair.second.socket->GetErrno();
                if (error == Socket::ERROR_NOTERROR)
                {
                    NS_LOG_DEBUG("Path " << pathPair.first << " is connected");
                    return true;
                }
            }
            catch (const std::exception& e)
            {
                NS_LOG_WARN("Exception checking socket for path " << pathPair.first << ": "
                                                                  << e.what());
            }
        }
    }

    NS_LOG_DEBUG("No paths are connected");
    return false;
}

DataRate
AggregatePathNadaClient::GetTotalRate() const
{
    NS_LOG_FUNCTION(this);

    if (m_paths.empty())
    {
        return DataRate("500kbps"); // Default rate
    }

    // Get current NADA rate and scale by number of active paths
    DataRate nadaRate = m_nada->UpdateRate();
    uint32_t activePaths = 0;
    uint64_t availableLinkCapacity = 0;

    for (const auto& pathPair : m_paths)
    {
        if (pathPair.second.socket)
        {
            activePaths++;
            availableLinkCapacity += pathPair.second.linkCapacity.GetBitRate();
        }
    }

    if (activePaths == 0)
    {
        return DataRate("500kbps");
    }

    // If we have valid link capacity info, use it
    uint64_t totalCapacity =
        (availableLinkCapacity > 0) ? availableLinkCapacity : m_totalLinkCapacity;

    // If still no capacity info, fall back to NADA scaling
    if (totalCapacity == 0)
    {
        uint64_t totalBitRate = nadaRate.GetBitRate() * activePaths;
        uint64_t maxBitRate = 50000000; // 50 Mbps max
        totalBitRate = std::min(totalBitRate, maxBitRate);

        DataRate totalRate(totalBitRate);
        NS_LOG_DEBUG("Using NADA scaling - Total rate: "
                     << totalRate.GetBitRate() / 1000000.0
                     << " Mbps (NADA: " << nadaRate.GetBitRate() / 1000000.0
                     << " Mbps, active paths: " << activePaths << ")");
        return totalRate;
    }

    // Apply NADA's rate control as a scaling factor to the actual link capacity
    double nadaScaling = std::min(1.0, nadaRate.GetBitRate() / (double)totalCapacity);
    uint64_t effectiveBitRate = totalCapacity * nadaScaling;
    effectiveBitRate = std::max(effectiveBitRate, 500000ULL); // 500 kbps minimum

    DataRate totalRate(effectiveBitRate);

    NS_LOG_DEBUG("Using link capacity - Total rate: "
                 << totalRate.GetBitRate() / 1000000.0 << " Mbps (Link capacity: "
                 << totalCapacity / 1000000.0 << " Mbps, NADA scaling: " << nadaScaling
                 << ", active paths: " << activePaths << ")");

    return totalRate;
}

void
AggregatePathNadaClient::SetVideoMode(bool enable)
{
    NS_LOG_FUNCTION(this << enable);
    m_videoMode = enable;

    if (m_nada) {
        m_nada->SetVideoMode(enable);
    }

    NS_LOG_INFO("Video mode " << (enable ? "enabled" : "disabled") << " for aggregate client");
}

void
AggregatePathNadaClient::SetKeyFrameStatus(bool isKeyFrame)
{
    NS_LOG_FUNCTION(this << isKeyFrame);
    m_isKeyFrame = isKeyFrame;
    NS_LOG_DEBUG("Set key frame status: " << (isKeyFrame ? "KEY" : "DELTA"));
}

bool
AggregatePathNadaClient::SendVideoFrame(uint32_t frameId,
                                       bool isKeyFrame,
                                       uint32_t frameSize,
                                       uint32_t mtu)
{
    NS_LOG_FUNCTION(this << frameId << isKeyFrame << frameSize);

    if (!m_running || !IsReady())
    {
        NS_LOG_WARN("Client not ready for video frame transmission");
        return false;
    }

    // **CRITICAL: Use EXACT same frame structure as multipath**
    uint32_t numPacketsNeeded = (frameSize + mtu - 1) / mtu;

    NS_LOG_INFO("AGG: Sending " << (isKeyFrame ? "key" : "delta")
               << " frame #" << frameId
               << " (size: " << frameSize << " bytes, packets: " << numPacketsNeeded << ")");

    // Set video mode for this frame
    SetVideoMode(true);
    SetKeyFrameStatus(isKeyFrame);
    SetPacketSize(mtu);

    uint32_t packetsSent = 0;

    // **IDENTICAL: Send all packets for this frame**
    for (uint32_t i = 0; i < numPacketsNeeded; i++)
    {
        Ptr<Packet> packet = Create<Packet>(mtu);

        // **CRITICAL: Use same round-robin as before, but frame-aware**
        bool sent = Send(packet);
        if (sent)
        {
            packetsSent++;
        }
        else
        {
            NS_LOG_WARN("Failed to send packet " << i << " of frame " << frameId);
            break;
        }
    }

    NS_LOG_INFO("AGG: Frame " << frameId << " complete: " << packetsSent
               << "/" << numPacketsNeeded << " packets sent");

    return (packetsSent == numPacketsNeeded);
}


// AggregatePathNadaClientHelper implementation
AggregatePathNadaClientHelper::AggregatePathNadaClientHelper()
{
    m_factory.SetTypeId("ns3::AggregatePathNadaClient");
}

AggregatePathNadaClientHelper::~AggregatePathNadaClientHelper()
{
}

void
AggregatePathNadaClientHelper::SetAttribute(std::string name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
AggregatePathNadaClientHelper::Install(Ptr<Node> node) const
{
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
AggregatePathNadaClientHelper::Install(NodeContainer c) const
{
    ApplicationContainer apps;
    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
    {
        apps.Add(InstallPriv(*i));
    }
    return apps;
}

Ptr<Application>
AggregatePathNadaClientHelper::InstallPriv(Ptr<Node> node) const
{
    Ptr<AggregatePathNadaClient> app = m_factory.Create<AggregatePathNadaClient>();
    node->AddApplication(app);
    return app;
}

} // namespace ns3
