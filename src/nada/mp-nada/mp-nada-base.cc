#include "mp-nada-base.h"
#include "ns3/nada-header.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MultiPathNadaClientBase");
NS_OBJECT_ENSURE_REGISTERED(MultiPathNadaClientBase);

TypeId
MultiPathNadaClientBase::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::MultiPathNadaClientBase")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddAttribute("PacketSize",
                          "Size of packets generated",
                          UintegerValue(1024),
                          MakeUintegerAccessor(&MultiPathNadaClientBase::m_packetSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("MaxPackets",
                          "The maximum number of packets the app will send",
                          UintegerValue(0),
                          MakeUintegerAccessor(&MultiPathNadaClientBase::m_maxPackets),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Interval",
                          "Time between path distribution updates",
                          TimeValue(MilliSeconds(200)),
                          MakeTimeAccessor(&MultiPathNadaClientBase::m_updateInterval),
                          MakeTimeChecker());
    return tid;
}

MultiPathNadaClientBase::MultiPathNadaClientBase()
    : m_packetSize(1024),
      m_maxPackets(0),
      m_running(false),
      m_videoMode(false),
      m_isKeyFrame(false),
      m_totalRate(DataRate("500kbps")),
      m_totalPacketsSent(0),
      m_updateInterval(MilliSeconds(1000)),
      m_videoReceiver(nullptr),
      m_isVideoMode(false)
{
    NS_LOG_FUNCTION(this);
}

MultiPathNadaClientBase::~MultiPathNadaClientBase()
{
    NS_LOG_FUNCTION(this);
}

bool
MultiPathNadaClientBase::AddPath(Address localAddress,
                                 Address remoteAddress,
                                 uint32_t pathId,
                                 double weight,
                                 DataRate initialRate)
{
    NS_LOG_FUNCTION(this << pathId << weight << initialRate);

    if (m_paths.find(pathId) != m_paths.end())
    {
        NS_LOG_ERROR("Path with ID " << pathId << " already exists");
        return false;
    }

    PathInfo pathInfo;
    pathInfo.client = CreateObject<UdpNadaClient>();
    pathInfo.nada = CreateObject<NadaCongestionControl>();
    pathInfo.weight = weight;
    pathInfo.currentRate = initialRate;
    pathInfo.packetsSent = 0;
    pathInfo.packetsAcked = 0;
    pathInfo.lastRtt = MilliSeconds(100);
    pathInfo.lastDelay = MilliSeconds(50);
    pathInfo.localAddress = localAddress;
    pathInfo.remoteAddress = remoteAddress;

    pathInfo.client->SetAttribute("PacketSize", UintegerValue(m_packetSize));
    pathInfo.client->SetAttribute("MaxPackets", UintegerValue(m_maxPackets));

    double interval = (m_packetSize * 8.0) / initialRate.GetBitRate();
    pathInfo.client->SetAttribute("Interval", TimeValue(Seconds(interval)));

    if (InetSocketAddress::IsMatchingType(remoteAddress))
    {
        InetSocketAddress addr = InetSocketAddress::ConvertFrom(remoteAddress);
        pathInfo.client->SetRemote(addr.GetIpv4(), addr.GetPort());
    }
    else if (Inet6SocketAddress::IsMatchingType(remoteAddress))
    {
        Inet6SocketAddress addr = Inet6SocketAddress::ConvertFrom(remoteAddress);
        pathInfo.client->SetRemote(addr.GetIpv6(), addr.GetPort());
    }
    else
    {
        NS_LOG_ERROR("Remote address is not an inet socket address");
        return false;
    }

    m_paths[pathId] = pathInfo;
    NS_LOG_DEBUG("Added path " << pathId << " successfully");
    return true;
}

bool
MultiPathNadaClientBase::RemovePath(uint32_t pathId)
{
    NS_LOG_FUNCTION(this << pathId);

    auto it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        NS_LOG_ERROR("Path with ID " << pathId << " does not exist");
        return false;
    }

    if (m_running && it->second.client)
    {
        it->second.client->SetStopTime(Simulator::Now());
    }

    // Remove socket mapping
    if (it->second.client)
    {
        Ptr<Socket> socket = it->second.client->GetSocket();
        if (socket)
        {
            m_socketToPathId.erase(socket);
        }
    }

    m_paths.erase(it);
    return true;
}

void
MultiPathNadaClientBase::SetPacketSize(uint32_t size)
{
    NS_LOG_FUNCTION(this << size);
    m_packetSize = size;

    for (auto& pathPair : m_paths)
    {
        pathPair.second.client->SetAttribute("PacketSize", UintegerValue(size));
    }
}

void
MultiPathNadaClientBase::SetMaxPackets(uint32_t numPackets)
{
    NS_LOG_FUNCTION(this << numPackets);
    m_maxPackets = numPackets;

    for (auto& pathPair : m_paths)
    {
        pathPair.second.client->SetAttribute("MaxPackets", UintegerValue(numPackets));
    }
}

void
MultiPathNadaClientBase::SetVideoMode(bool enable)
{
    NS_LOG_FUNCTION(this << enable);
    m_videoMode = enable;
    m_isVideoMode = enable;

    for (auto& pathPair : m_paths)
    {
        if (pathPair.second.nada)
        {
            pathPair.second.nada->SetVideoMode(enable);
        }
    }
}

void
MultiPathNadaClientBase::SetKeyFrameStatus(bool isKeyFrame)
{
    NS_LOG_FUNCTION(this << isKeyFrame);
    m_isKeyFrame = isKeyFrame;
}

bool
MultiPathNadaClientBase::IsReady(void) const
{
    if (!m_running || m_paths.empty())
    {
        return false;
    }

    for (const auto& pathPair : m_paths)
    {
        if (pathPair.second.client && pathPair.second.client->GetSocket())
        {
            return true; // At least one path is available
        }
    }

    return false;
}

DataRate
MultiPathNadaClientBase::GetTotalRate(void) const
{
    return m_totalRate;
}

uint32_t
MultiPathNadaClientBase::GetNumPaths(void) const
{
    return m_paths.size();
}

std::map<std::string, double>
MultiPathNadaClientBase::GetPathStats(uint32_t pathId) const
{
    std::map<std::string, double> stats;

    auto it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        NS_LOG_ERROR("Path with ID " << pathId << " does not exist");
        return stats;
    }

    stats["weight"] = it->second.weight;
    stats["rate_bps"] = it->second.currentRate.GetBitRate();
    stats["packets_sent"] = it->second.packetsSent;
    stats["packets_acked"] = it->second.packetsAcked;
    stats["rtt_ms"] = it->second.lastRtt.GetMilliSeconds();
    stats["delay_ms"] = it->second.lastDelay.GetMilliSeconds();

    return stats;
}

bool
MultiPathNadaClientBase::SendPacketOnPath(uint32_t pathId, Ptr<Packet> packet)
{
    if (!packet)
    {
        NS_LOG_ERROR("Cannot send null packet");
        return false;
    }

    auto it = m_paths.find(pathId);
    if (it == m_paths.end() || !it->second.client)
    {
        NS_LOG_ERROR("Path " << pathId << " not found or client is null");
        return false;
    }

    Ptr<Socket> socket = it->second.client->GetSocket();
    if (!socket)
    {
        NS_LOG_WARN("Socket not available for path " << pathId);
        return false;
    }

    // Just try to send - don't recreate sockets on failure

    try
    {
        // Add proper NADA header
        NadaHeader header;
        header.SetSequenceNumber(m_totalPacketsSent);
        header.SetTimestamp(MicroSeconds(Simulator::Now().GetMicroSeconds()));

        if (m_isVideoMode)
        {
            header.SetVideoFrameType(m_isKeyFrame ? 0 : 1);
            header.SetVideoFrameSize(packet->GetSize());
        }

        packet->AddHeader(header);

        int sent = socket->Send(packet);
        if (sent > 0)
        {
            it->second.packetsSent++;
            m_totalPacketsSent++;
            return true;
        }
        else
        {
            NS_LOG_DEBUG("Send failed for path " << pathId << ", but keeping socket");
            return false;
        }
    }
    catch (const std::exception& e)
    {
        NS_LOG_DEBUG("Exception in SendPacketOnPath for path " << pathId << ": " << e.what());
        return false;
    }
}

void
MultiPathNadaClientBase::SetNadaAdaptability(uint32_t pathId,
                                             DataRate minRate,
                                             DataRate maxRate,
                                             Time rttMax)
{
    auto it = m_paths.find(pathId);
    if (it == m_paths.end() || !it->second.nada)
    {
        NS_LOG_ERROR("Cannot set NADA parameters for path " << pathId);
        return;
    }

    it->second.nada->SetMinRate(minRate);
    it->second.nada->SetMaxRate(maxRate);
    it->second.nada->SetRttMax(rttMax);
    it->second.nada->SetXRef(DataRate(maxRate.GetBitRate() * 0.5));

    NS_LOG_INFO("Set NADA parameters for path " << pathId);
}

bool
MultiPathNadaClientBase::SendVideoFrame(uint32_t frameId,
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

    uint32_t numPacketsNeeded = (frameSize + mtu - 1) / mtu;

    NS_LOG_INFO("MP: Sending " << (isKeyFrame ? "key" : "delta")
               << " frame #" << frameId
               << " (size: " << frameSize << " bytes, packets: " << numPacketsNeeded << ")");

    // Set video mode for this frame
    SetVideoMode(true);
    SetKeyFrameStatus(isKeyFrame);
    SetPacketSize(mtu);

    uint32_t packetsSent = 0;

    for (uint32_t i = 0; i < numPacketsNeeded; i++)
    {
        Ptr<Packet> packet = Create<Packet>(mtu);

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

    NS_LOG_INFO("MP: Frame " << frameId << " complete: " << packetsSent
               << "/" << numPacketsNeeded << " packets sent");

    return (packetsSent == numPacketsNeeded);
}

bool
MultiPathNadaClientBase::Send(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this);

    if (!m_running || m_totalPacketsSent >= m_maxPackets)
    {
        return false;
    }

    if (!IsReady())
    {
        NS_LOG_WARN("Client not ready for sending");
        return false;
    }

    std::vector<uint32_t> availablePaths;
    for (const auto& pathPair : m_paths)
    {
        if (pathPair.second.client && pathPair.second.client->GetSocket())
        {
            availablePaths.push_back(pathPair.first);
        }
    }

    if (availablePaths.empty())
    {
        NS_LOG_ERROR("No available paths for sending");
        return false;
    }

    // Simple round-robin for base class
    static uint32_t currentPathIndex = 0;
    uint32_t selectedPath = availablePaths[currentPathIndex % availablePaths.size()];
    currentPathIndex++;

    bool sent = SendPacketOnPath(selectedPath, packet);
    if (sent)
    {
        m_totalPacketsSent++;
    }

    return sent;
}

void
MultiPathNadaClientBase::SetVideoReceiver(Ptr<VideoReceiver> receiver)
{
    NS_LOG_FUNCTION(this << receiver);
    m_videoReceiver = receiver;
}

void
MultiPathNadaClientBase::ValidateAllSockets(void)
{
    NS_LOG_FUNCTION(this);

    static bool hasValidated = false;
    if (hasValidated)
    {
        NS_LOG_DEBUG("Sockets already validated, skipping re-validation");
        return;
    }

    uint32_t readyCount = 0;
    uint32_t totalCount = 0;

    for (auto& pathPair : m_paths)
    {
        uint32_t pathId = pathPair.first;
        totalCount++;

        if (pathPair.second.client)
        {
            Ptr<Socket> socket = pathPair.second.client->GetSocket();
            if (socket)
            {
                readyCount++;
                NS_LOG_DEBUG("Path " << pathId << " socket exists");
            }
        }
    }

    if (readyCount == totalCount && readyCount > 0)
    {
        hasValidated = true;
        NS_LOG_INFO("All sockets validated successfully - will not re-validate");

        for (auto& pathPair : m_paths)
        {
            uint32_t pathId = pathPair.first;
            if (pathPair.second.nada && pathPair.second.client)
            {
                Ptr<Socket> socket = pathPair.second.client->GetSocket();
                if (socket)
                {
                    pathPair.second.nada->Init(socket);
                    pathPair.second.nada->SetInitialRate(pathPair.second.currentRate);
                    NS_LOG_INFO("NADA initialized for path " << pathId);
                }
            }
        }
    }
    else
    {
        NS_LOG_WARN("Socket validation failed: " << readyCount << "/" << totalCount << " ready");
    }
}

void
MultiPathNadaClientBase::ReportSocketStatus()
{
    if (!m_running)
    {
        return;
    }
}

uint32_t
MultiPathNadaClientBase::GetPacketSize(void) const
{
    return m_packetSize;
}

void
MultiPathNadaClientBase::StartApplication(void)
{
    NS_LOG_FUNCTION(this);

    if (m_running)
    {
        return;
    }

    m_running = true;
    NS_LOG_INFO("MultiPathNadaClientBase starting with " << m_paths.size() << " paths");

    // Initialize sockets for each path - ONCE ONLY
    uint32_t delay = 0;
    for (auto& pathPair : m_paths)
    {
        Simulator::Schedule(MilliSeconds(delay),
                            &MultiPathNadaClientBase::InitializePathSocket,
                            this,
                            pathPair.first);
        delay += 50;
    }

    // Schedule socket validation ONCE
    Simulator::Schedule(MilliSeconds(delay + 500),
                        &MultiPathNadaClientBase::ValidateAllSockets, this);

    // Simulator::Schedule(Seconds(5.0), &MultiPathNadaClientBase::PeriodicHealthCheck, this);

    // Schedule path distribution updates with much longer intervals
    m_updateEvent = Simulator::Schedule(Seconds(10.0),
                                       &MultiPathNadaClientBase::UpdatePathDistribution, this);
}

void
MultiPathNadaClientBase::PeriodicHealthCheck(void)
{
    if (!m_running)
    {
        return;
    }

    NS_LOG_DEBUG("Performing periodic health check");

    uint32_t healthyPaths = 0;
    for (const auto& pathPair : m_paths)
    {
        uint32_t pathId = pathPair.first;
        if (pathPair.second.client)
        {
            Ptr<Socket> socket = pathPair.second.client->GetSocket();
            if (socket && IsSocketReady(socket))
            {
                healthyPaths++;
            }
            else
            {
                NS_LOG_WARN("Health check failed for path " << pathId << ", reinitializing");
                Simulator::Schedule(MilliSeconds(100),
                                  &MultiPathNadaClientBase::InitializePathSocket,
                                  this,
                                  pathId);
            }
        }
    }

    NS_LOG_INFO("Health check: " << healthyPaths << "/" << m_paths.size() << " paths healthy");

    // Schedule next health check
    Simulator::Schedule(Seconds(10.0),
                        &MultiPathNadaClientBase::PeriodicHealthCheck, this);
}

void
MultiPathNadaClientBase::StopApplication(void)
{
    NS_LOG_FUNCTION(this);
    m_running = false;

    // Stop all path clients
    for (auto& pathPair : m_paths)
    {
        if (pathPair.second.client)
        {
            pathPair.second.client->SetStopTime(Simulator::Now());
        }
    }

    // Cancel pending events
    if (m_updateEvent.IsPending())
    {
        Simulator::Cancel(m_updateEvent);
    }
}

void
MultiPathNadaClientBase::DoDispose(void)
{
    NS_LOG_FUNCTION(this);

    // Clean up all paths
    for (auto& pathPair : m_paths)
    {
        pathPair.second.client = nullptr;
        pathPair.second.nada = nullptr;
    }

    m_socketToPathId.clear();

    // Cancel any pending events
    if (m_updateEvent.IsPending())
    {
        Simulator::Cancel(m_updateEvent);
    }

    Application::DoDispose();
}

void
MultiPathNadaClientBase::InitializePathSocket(uint32_t pathId)
{
    NS_LOG_FUNCTION(this << pathId);

    auto it = m_paths.find(pathId);
    if (it == m_paths.end() || !it->second.client)
    {
        NS_LOG_ERROR("Path " << pathId << " not found or has null client");
        return;
    }

    NS_LOG_INFO("Initializing socket for path " << pathId);

    Ptr<Node> node = GetNode();
    if (!node)
    {
        NS_LOG_ERROR("Node is null for path " << pathId);
        return;
    }

    Ptr<Socket> socket = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());
    if (!socket)
    {
        NS_LOG_ERROR("Failed to create socket for path " << pathId);
        return;
    }

    // UDP sockets don't have SndBufSize/RcvBufSize attributes
    // These are only available for TCP sockets

    try
    {
        // Set UDP socket to allow broadcast (optional)
        socket->SetAllowBroadcast(false);

        // Set IP TTL if needed
        socket->SetIpTtl(64);

        NS_LOG_DEBUG("UDP socket attributes set successfully for path " << pathId);
    }
    catch (const std::exception& e)
    {
        NS_LOG_WARN("Exception setting UDP socket attributes: " << e.what());
        // Continue anyway - these are not critical for functionality
    }

    if (InetSocketAddress::IsMatchingType(it->second.localAddress))
    {
        InetSocketAddress localAddr = InetSocketAddress::ConvertFrom(it->second.localAddress);

        uint16_t basePort = localAddr.GetPort();
        if (basePort == 0) {
            basePort = 9000;
        }

        // Use unique port per path
        uint16_t uniquePort = basePort + pathId;
        localAddr.SetPort(uniquePort);

        for (const auto& existingPath : m_paths)
        {
            if (existingPath.first != pathId && existingPath.second.client)
            {
                Ptr<Socket> existingSocket = existingPath.second.client->GetSocket();
                if (existingSocket)
                {
                    Address existingLocal;
                    if (existingSocket->GetSockName(existingLocal) == 0)
                    {
                        if (InetSocketAddress::IsMatchingType(existingLocal))
                        {
                            InetSocketAddress existingAddr = InetSocketAddress::ConvertFrom(existingLocal);
                            if (existingAddr.GetPort() == uniquePort)
                            {
                                uniquePort += 100; // Use different port
                                localAddr.SetPort(uniquePort);
                                break;
                            }
                        }
                    }
                }
            }
        }

        int bindResult = socket->Bind(localAddr);
        if (bindResult != 0)
        {
            NS_LOG_ERROR("Failed to bind socket for path " << pathId << " to port " << uniquePort
                        << ", error: " << bindResult);

            bool boundSuccessfully = false;
            for (uint16_t tryPort = uniquePort + 1; tryPort < uniquePort + 100; tryPort++)
            {
                localAddr.SetPort(tryPort);
                if (socket->Bind(localAddr) == 0)
                {
                    NS_LOG_INFO("Successfully bound to alternative port " << tryPort);
                    boundSuccessfully = true;
                    break;
                }
            }

            if (!boundSuccessfully)
            {
                NS_LOG_ERROR("Unable to bind socket for path " << pathId << " to any port");
                return;
            }
        }
        else
        {
            NS_LOG_INFO("Socket bound to " << localAddr);
        }
    }

    if (InetSocketAddress::IsMatchingType(it->second.remoteAddress))
    {
        InetSocketAddress remoteAddr = InetSocketAddress::ConvertFrom(it->second.remoteAddress);

        if (remoteAddr.GetIpv4() == Ipv4Address::GetAny() || remoteAddr.GetPort() == 0)
        {
            NS_LOG_ERROR("Invalid remote address for path " << pathId << ": " << remoteAddr);
            return;
        }

        int connectResult = socket->Connect(remoteAddr);
        if (connectResult != 0)
        {
            NS_LOG_ERROR("Failed to connect socket for path " << pathId << " to " << remoteAddr
                        << ", error: " << connectResult);
            return;
        }

        Address peerAddr;
        if (socket->GetPeerName(peerAddr) != 0)
        {
            NS_LOG_ERROR("Socket connected but cannot get peer name for path " << pathId);
            return;
        }

        NS_LOG_INFO("Socket connected to " << remoteAddr);
    }

    socket->SetRecvCallback(MakeCallback(&MultiPathNadaClientBase::HandleRecv, this));

    socket->SetCloseCallbacks(
        MakeCallback(&MultiPathNadaClientBase::HandleSocketClose, this),
        MakeCallback(&MultiPathNadaClientBase::HandleSocketError, this)
    );

    it->second.client->SetSocket(socket);

    Ptr<Socket> verifySocket = it->second.client->GetSocket();
    if (!verifySocket || verifySocket != socket)
    {
        NS_LOG_ERROR("Socket was not properly set in UdpNadaClient for path " << pathId);
        return;
    }

    it->second.client->SetNode(GetNode());

    // Store socket mapping AFTER verification
    m_socketToPathId[socket] = pathId;

    if (it->second.nada)
    {
        // Check if NADA is already initialized to avoid double initialization
        static std::set<std::pair<Ptr<NadaCongestionControl>, Ptr<Socket>>> initializedPairs;
        std::pair<Ptr<NadaCongestionControl>, Ptr<Socket>> nadaSocketPair =
            std::make_pair(it->second.nada, socket);

        if (initializedPairs.find(nadaSocketPair) == initializedPairs.end())
        {
            it->second.nada->Init(socket);

            // Set initial rate based on path capacity
            DataRate pathRate = it->second.currentRate;
            it->second.nada->SetInitialRate(pathRate);

            if (m_videoMode)
            {
                it->second.nada->SetVideoMode(true);
            }

            initializedPairs.insert(nadaSocketPair);
            NS_LOG_INFO("NADA initialized for path " << pathId << " with rate " << pathRate);
        }
        else
        {
            NS_LOG_DEBUG("NADA already initialized for path " << pathId);
        }
    }

    if (!IsSocketReady(socket))
    {
        NS_LOG_WARN("Socket for path " << pathId << " not ready after initialization");
        // Don't return here - socket might become ready shortly
    }

    NS_LOG_INFO("Path " << pathId << " socket initialized successfully");

    Simulator::Schedule(MilliSeconds(100),
                       &MultiPathNadaClientBase::ValidatePathSocket,
                       this,
                       pathId);
}


void
MultiPathNadaClientBase::ValidatePathSocket(uint32_t pathId)
{
    NS_LOG_FUNCTION(this << pathId);

    auto it = m_paths.find(pathId);
    if (it == m_paths.end() || !it->second.client)
    {
        return;
    }

    Ptr<Socket> socket = it->second.client->GetSocket();
    bool ready = IsSocketReady(socket);

    if (ready)
    {
        NS_LOG_INFO("Path " << pathId << " socket validated successfully");
        if (it->second.nada)
        {
            it->second.nada->Init(socket);
        }
    }
    else
    {
        NS_LOG_WARN("Path " << pathId << " socket validation failed, retrying...");
        Simulator::Schedule(MilliSeconds(200),
                            &MultiPathNadaClientBase::InitializePathSocket,
                            this,
                            pathId);
    }
}

void
MultiPathNadaClientBase::HandleRecv(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    if (!socket)
    {
        NS_LOG_ERROR("HandleRecv called with null socket");
        return;
    }

    auto it = m_socketToPathId.find(socket);
    if (it == m_socketToPathId.end())
    {
        NS_LOG_WARN("Received packet for unknown socket");
        return;
    }

    uint32_t pathId = it->second;

    try
    {
        Address from;
        Ptr<Packet> packet = socket->RecvFrom(from);

        if (!packet || packet->GetSize() == 0)
        {
            NS_LOG_WARN("Null or empty packet received on path " << pathId);
            return;
        }

        auto pathIt = m_paths.find(pathId);
        if (pathIt == m_paths.end())
        {
            NS_LOG_ERROR("Path " << pathId << " not found");
            return;
        }

        // Update statistics
        pathIt->second.packetsAcked++;

        // Extract delay from header if possible
        Time delay = MilliSeconds(50); // Default

        if (packet->GetSize() >= NadaHeader::GetStaticSize())
        {
            try
            {
                Ptr<Packet> copy = packet->Copy();
                NadaHeader header;
                if (copy->RemoveHeader(header))
                {
                    if (header.GetTimestamp() > Time::Min())
                    {
                        delay = Simulator::Now() - header.GetTimestamp();
                        pathIt->second.lastDelay = delay;
                        pathIt->second.lastRtt = delay * 2; // Simplified RTT
                    }
                }
            }
            catch (const std::exception& e)
            {
                NS_LOG_WARN("Exception processing header: " << e.what());
            }
        }

        HandleAck(pathId, packet, delay);

        NS_LOG_DEBUG("Packet acknowledged on path " << pathId
                    << " (acked: " << pathIt->second.packetsAcked
                    << ", sent: " << pathIt->second.packetsSent << ")");
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception in HandleRecv for path " << pathId << ": " << e.what());
    }
}

void
MultiPathNadaClientBase::HandleAck(uint32_t pathId, Ptr<Packet> packet, Time delay)
{
    NS_LOG_FUNCTION(this << pathId << delay);

    auto it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        NS_LOG_ERROR("Received ACK for unknown path ID " << pathId);
        return;
    }

    // Update path statistics
    it->second.lastDelay = delay;
    it->second.lastRtt = delay * 2; // Simplified RTT calculation
}

bool
MultiPathNadaClientBase::IsSocketReady(Ptr<Socket> socket) const
{
    if (!socket)
    {
        return false;
    }

    static std::map<Ptr<Socket>, std::pair<bool, Time>> socketStatusCache;
    static const Time CACHE_DURATION = MilliSeconds(100); // Cache for 100ms

    Time now = Simulator::Now();
    auto cacheIt = socketStatusCache.find(socket);

    if (cacheIt != socketStatusCache.end())
    {
        if ((now - cacheIt->second.second) < CACHE_DURATION)
        {
            // Return cached result
            return cacheIt->second.first;
        }
    }

    try
    {
        Socket::SocketErrno error = socket->GetErrno();
        bool isReady = (error == Socket::ERROR_NOTERROR);

        if (isReady)
        {
            Address peerAddr;
            isReady = (socket->GetPeerName(peerAddr) == 0);
        }
        socketStatusCache[socket] = std::make_pair(isReady, now);

        return isReady;
    }
    catch (const std::exception& e)
    {
        NS_LOG_DEBUG("Exception checking socket: " << e.what());
        socketStatusCache[socket] = std::make_pair(false, now);
        return false;
    }
}


void
MultiPathNadaClientBase::UpdatePathDistribution()
{
    NS_LOG_FUNCTION(this);

    // Update NADA rates for all paths
    double totalRateBps = 0.0;
    for (auto& pathPair : m_paths)
    {
        if (pathPair.second.nada)
        {
            DataRate nadaRate = pathPair.second.nada->UpdateRate();
            pathPair.second.currentRate = nadaRate;
            totalRateBps += nadaRate.GetBitRate();
        }
    }
    m_totalRate = DataRate(totalRateBps);

    UpdateWeights();

    Time nextUpdate;
    if (totalRateBps >= 10e9) { // 10Gbps+
        nextUpdate = Seconds(5.0); 
    } else if (totalRateBps >= 1e9) { // 1Gbps+
        nextUpdate = Seconds(3.0); 
    } else if (totalRateBps >= 100e6) { // 100Mbps+
        nextUpdate = Seconds(1.0);
    } else {
        nextUpdate = MilliSeconds(500);
    }

    // Schedule next update
    m_updateEvent = Simulator::Schedule(nextUpdate,
                                       &MultiPathNadaClientBase::UpdatePathDistribution, this);

    NS_LOG_DEBUG("Updated path distribution, next update in "
                << nextUpdate.GetMilliSeconds() << "ms");
}

void
MultiPathNadaClientBase::HandleSocketClose(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    auto it = m_socketToPathId.find(socket);
    if (it != m_socketToPathId.end())
    {
        uint32_t pathId = it->second;
        NS_LOG_WARN("Socket closed for path " << pathId << ", will reinitialize");

        // Schedule reinitialization
        Simulator::Schedule(MilliSeconds(500),
                          &MultiPathNadaClientBase::InitializePathSocket,
                          this,
                          pathId);
    }
}

void
MultiPathNadaClientBase::HandleSocketError(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    auto it = m_socketToPathId.find(socket);
    if (it != m_socketToPathId.end())
    {
        uint32_t pathId = it->second;
        NS_LOG_ERROR("Socket error for path " << pathId << ", will reinitialize");

        // Clean up and reinitialize
        m_socketToPathId.erase(it);
        Simulator::Schedule(MilliSeconds(1000),
                          &MultiPathNadaClientBase::InitializePathSocket,
                          this,
                          pathId);
    }
}

} // namespace ns3
