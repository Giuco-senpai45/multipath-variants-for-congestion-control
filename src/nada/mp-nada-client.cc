#include "mp-nada-client.h"

#include "nada-udp-client.h"

#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/names.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "nada-header.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MultiPathNadaClient");
NS_OBJECT_ENSURE_REGISTERED(MultiPathNadaClient);

TypeId
MultiPathNadaClient::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::MultiPathNadaClient")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<MultiPathNadaClient>()
            .AddAttribute("PacketSize",
                          "Size of packets generated",
                          UintegerValue(1024),
                          MakeUintegerAccessor(&MultiPathNadaClient::m_packetSize),
                          MakeUintegerChecker<uint32_t>(1, 1500))
            .AddAttribute("MaxPackets",
                          "The maximum number of packets the app will send",
                          UintegerValue(0),
                          MakeUintegerAccessor(&MultiPathNadaClient::m_maxPackets),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Interval",
                          "Time between path distribution updates",
                          TimeValue(MilliSeconds(200)),
                          MakeTimeAccessor(&MultiPathNadaClient::m_updateInterval),
                          MakeTimeChecker())
            .AddAttribute("PathSelectionStrategy",
                          "Strategy for path selection (0=weighted, 1=best path, 2=equal)",
                          UintegerValue(0),
                          MakeUintegerAccessor(&MultiPathNadaClient::m_pathSelectionStrategy),
                          MakeUintegerChecker<uint32_t>(0, 2));
    return tid;
}

MultiPathNadaClient::MultiPathNadaClient()
    : m_packetSize(1024),
      m_maxPackets(0),
      m_running(false),
      m_totalRate(DataRate("500kbps")),
      m_updateInterval(MilliSeconds(200)),
      m_totalPacketsSent(0),
      m_pathSelectionStrategy(0)
{
    NS_LOG_FUNCTION(this);
}

MultiPathNadaClient::~MultiPathNadaClient()
{
    NS_LOG_FUNCTION(this);
}

bool
MultiPathNadaClient::AddPath(Address localAddress,
                             Address remoteAddress,
                             uint32_t pathId,
                             double weight,
                             DataRate initialRate)
{
    NS_LOG_FUNCTION(this << pathId << weight << initialRate);

    // Check if the path already exists
    if (m_paths.find(pathId) != m_paths.end())
    {
        NS_LOG_ERROR("Path with ID " << pathId << " already exists");
        return false;
    }

    // Create the path info
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

    // Set up the client with some basic attributes
    NS_LOG_DEBUG("Setting up client for path " << pathId);
    pathInfo.client->SetAttribute("PacketSize", UintegerValue(m_packetSize));
    pathInfo.client->SetAttribute("MaxPackets", UintegerValue(m_maxPackets));

    double interval = (m_packetSize * 8.0) / initialRate.GetBitRate();
    pathInfo.client->SetAttribute("Interval", TimeValue(Seconds(interval)));

    if (InetSocketAddress::IsMatchingType(remoteAddress))
    {
        InetSocketAddress addr = InetSocketAddress::ConvertFrom(remoteAddress);
        NS_LOG_DEBUG("Setting remote to IPv4: " << addr.GetIpv4() << ":" << addr.GetPort());
        pathInfo.client->SetRemote(addr.GetIpv4(), addr.GetPort());
    }
    else if (Inet6SocketAddress::IsMatchingType(remoteAddress))
    {
        Inet6SocketAddress addr = Inet6SocketAddress::ConvertFrom(remoteAddress);
        NS_LOG_DEBUG("Setting remote to IPv6: " << addr.GetIpv6() << ":" << addr.GetPort());
        pathInfo.client->SetRemote(addr.GetIpv6(), addr.GetPort());
    }
    else
    {
        NS_LOG_ERROR("Remote address is not an inet socket address");
        return false;
    }

    // Store the path
    m_paths[pathId] = pathInfo;
    NS_LOG_DEBUG("Added path " << pathId << " successfully");

    return true;
}

bool
MultiPathNadaClient::RemovePath(uint32_t pathId)
{
    NS_LOG_FUNCTION(this << pathId);

    // Check if the path exists
    std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        NS_LOG_ERROR("Path with ID " << pathId << " does not exist");
        return false;
    }

    // Stop the client if it's running
    if (m_running)
    {
        it->second.client->SetStopTime(Simulator::Now());
    }

    // Remove the path
    m_paths.erase(it);

    return true;
}

uint32_t
MultiPathNadaClient::GetNumPaths(void) const
{
    return m_paths.size();
}

DataRate
MultiPathNadaClient::GetTotalRate(void) const
{
    return m_totalRate;
}

void
MultiPathNadaClient::SetPacketSize(uint32_t size)
{
    NS_LOG_FUNCTION(this << size);
    m_packetSize = size;

    // Update all paths
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        it->second.client->SetAttribute("PacketSize", UintegerValue(size));
    }
}

void
MultiPathNadaClient::SetMaxPackets(uint32_t numPackets)
{
    NS_LOG_FUNCTION(this << numPackets);
    m_maxPackets = numPackets;

    // Update all paths
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        it->second.client->SetAttribute("MaxPackets", UintegerValue(numPackets));
    }
}

bool
MultiPathNadaClient::SetPathWeight(uint32_t pathId, double weight)
{
    NS_LOG_FUNCTION(this << pathId << weight);

    // Check if the path exists
    std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        NS_LOG_ERROR("Path with ID " << pathId << " does not exist");
        return false;
    }

    // Update the weight
    it->second.weight = weight;

    // Update the path distribution
    UpdatePathDistribution();

    return true;
}

std::map<std::string, double>
MultiPathNadaClient::GetPathStats(uint32_t pathId) const
{
    std::map<std::string, double> stats;

    // Check if the path exists
    std::map<uint32_t, PathInfo>::const_iterator it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        NS_LOG_ERROR("Path with ID " << pathId << " does not exist");
        return stats;
    }

    // Fill in the stats
    stats["weight"] = it->second.weight;
    stats["rate_bps"] = it->second.currentRate.GetBitRate();
    stats["packets_sent"] = it->second.packetsSent;
    stats["packets_acked"] = it->second.packetsAcked;
    stats["rtt_ms"] = it->second.lastRtt.GetMilliSeconds();
    stats["delay_ms"] = it->second.lastDelay.GetMilliSeconds();

    return stats;
}

void
MultiPathNadaClient::DoDispose(void)
{
    NS_LOG_FUNCTION(this);

    // Clean up all paths
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        it->second.client = 0;
        it->second.nada = 0;
    }

    // Cancel any pending events
    if (m_sendEvent.IsPending())
    {
        Simulator::Cancel(m_sendEvent);
    }

    if (m_updateEvent.IsPending())
    {
        Simulator::Cancel(m_updateEvent);
    }

    Application::DoDispose();
}

void MultiPathNadaClient::InitializePathSocket(uint32_t pathId) {
    std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
    if (it == m_paths.end() || !it->second.client) {
        NS_LOG_ERROR("Path " << pathId << " not found or invalid client");
        return;
    }

    // First check if the client has a valid socket
    Ptr<Socket> socket = nullptr;

    try {
        socket = it->second.client->GetSocket();
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Exception getting socket: " << e.what());
        Simulator::Schedule(MilliSeconds(100), &MultiPathNadaClient::InitializePathSocket, this, pathId);
        return;
    }

    // If socket is null, reschedule initialization
    if (!socket) {
        NS_LOG_WARN("Socket for path " << pathId << " not ready, retrying in 100ms");
        Simulator::Schedule(MilliSeconds(100), &MultiPathNadaClient::InitializePathSocket, this, pathId);
        return;
    }

    // Check if the socket is already associated with this path
    if (m_socketToPathId.find(socket) != m_socketToPathId.end()) {
        NS_LOG_INFO("Socket for path " << pathId << " already initialized");
        return;
    }

    NS_LOG_INFO("Socket for path " << pathId << " initialized successfully");
    try {
        m_socketToPathId[socket] = pathId;

        // Initialize NADA congestion control
        if (it->second.nada) {
            it->second.nada->Init(socket);
            NS_LOG_INFO("NADA initialized for path " << pathId);
        } else {
            NS_LOG_ERROR("NADA object is null for path " << pathId);
        }

        // Set up receive callback
        socket->SetRecvCallback(MakeCallback(&MultiPathNadaClient::HandleRecv, this));
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Exception initializing socket for path " << pathId << ": " << e.what());
    }
}


void MultiPathNadaClient::StartApplication(void)
{
    NS_LOG_FUNCTION(this);
    m_running = true;

    NS_LOG_INFO("Starting MultiPathNadaClient with " << m_paths.size() << " paths");

    // Start all path clients
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        NS_LOG_INFO("Starting client for path " << it->first);

        // Set client parameters just before starting
        it->second.client->SetAttribute("PacketSize", UintegerValue(m_packetSize));
        it->second.client->SetAttribute("MaxPackets", UintegerValue(m_maxPackets));

        // Install the client on the node and start it
        GetNode()->AddApplication(it->second.client);
        it->second.client->SetStartTime(Seconds(0.1));
        it->second.client->SetStopTime(Simulator::GetMaximumSimulationTime());
    }

    // Schedule socket initialization attempts with increasing delays
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        for (uint32_t delay = 500; delay <= 3000; delay += 500)  // Try over a longer period
        {
            Simulator::Schedule(MilliSeconds(delay),
                &MultiPathNadaClient::InitializePathSocket,
                this,
                it->first);
        }
    }

    // Schedule regular status reports
    Simulator::Schedule(MilliSeconds(500), &MultiPathNadaClient::ReportSocketStatus, this);

    // Schedule path distribution updates
    NS_LOG_INFO("Scheduling path distribution updates");
    m_lastUpdateTime = Simulator::Now();
    m_updateEvent = Simulator::Schedule(m_updateInterval,
        &MultiPathNadaClient::UpdatePathDistribution, this);

    // Schedule the first packet transmission with a longer delay for socket setup
    NS_LOG_INFO("Scheduling initial packet sending with 3000ms delay");
    m_sendEvent = Simulator::Schedule(MilliSeconds(3000),
        &MultiPathNadaClient::SendPackets, this);
}

void
MultiPathNadaClient::StopApplication(void)
{
    NS_LOG_FUNCTION(this);
    m_running = false;

    // Stop all path clients
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        it->second.client->SetStopTime(Simulator::Now());
    }

    // Cancel any pending events
    if (m_sendEvent.IsPending())
    {
        Simulator::Cancel(m_sendEvent);
    }

    if (m_updateEvent.IsPending())
    {
        Simulator::Cancel(m_updateEvent);
    }
}

uint32_t
MultiPathNadaClient::GetBestPath(const std::vector<uint32_t>& readyPaths)
{
    NS_LOG_FUNCTION(this);

    if (readyPaths.empty())
    {
        NS_LOG_ERROR("GetBestPath called with empty path list");
        return 0; // Invalid path ID
    }

    uint32_t bestPathId = readyPaths[0];
    double bestMetric = -1.0;

    for (auto pathId : readyPaths)
    {
        std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
        if (it == m_paths.end())
        {
            NS_LOG_WARN("Path " << pathId << " found in readyPaths but not in m_paths");
            continue;
        }

        // Use a combination of RTT and current rate as the metric
        // Lower RTT and higher rate are better
        double rttMs = it->second.lastRtt.GetMilliSeconds();
        double rateMbps = it->second.currentRate.GetBitRate() / 1000000.0;

        // Avoid division by zero
        if (rttMs < 1.0)
        {
            rttMs = 1.0;
        }

        // Compute a metric that increases with rate and decreases with RTT
        double metric = rateMbps / rttMs;

        NS_LOG_INFO("Path " << pathId << " metric: " << metric << " (rate=" << rateMbps
                            << "Mbps, rtt=" << rttMs << "ms)");

        if (metric > bestMetric)
        {
            bestMetric = metric;
            bestPathId = pathId;
        }
    }

    NS_LOG_INFO("Selected best path: " << bestPathId << " with metric " << bestMetric);
    return bestPathId;
}

uint32_t
MultiPathNadaClient::GetWeightedPath(const std::vector<uint32_t>& readyPaths)
{
    NS_LOG_FUNCTION(this);

    if (readyPaths.empty())
    {
        NS_LOG_ERROR("GetWeightedPath called with empty path list");
        return 0; // Invalid path ID
    }

    // Calculate total weight of ready paths
    double totalWeight = 0.0;
    std::map<uint32_t, double> normalizedWeights;

    for (auto pathId : readyPaths)
    {
        std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
        if (it == m_paths.end())
        {
            NS_LOG_WARN("Path " << pathId << " found in readyPaths but not in m_paths");
            continue;
        }

        totalWeight += it->second.weight;
        normalizedWeights[pathId] = it->second.weight;
    }

    if (totalWeight <= 0.0)
    {
        NS_LOG_WARN("Total weight is zero or negative, using equal weights");
        // Use equal weights if total is not positive
        for (auto pathId : readyPaths)
        {
            normalizedWeights[pathId] = 1.0 / readyPaths.size();
        }
    }
    else
    {
        // Normalize weights
        for (auto& pair : normalizedWeights)
        {
            pair.second /= totalWeight;
        }
    }

    // Select a path based on weights using a random draw
    double r = UniformRandomVariable().GetValue(0.0, 1.0);
    double cumulativeProb = 0.0;

    NS_LOG_INFO("Random value for weighted selection: " << r);

    for (auto pathId : readyPaths)
    {
        cumulativeProb += normalizedWeights[pathId];
        NS_LOG_INFO("Path " << pathId << " weight: " << normalizedWeights[pathId]
                            << ", cumulative: " << cumulativeProb);

        if (r <= cumulativeProb)
        {
            NS_LOG_INFO("Selected path " << pathId << " using weighted distribution");
            return pathId;
        }
    }

    // Fallback - should rarely happen due to floating point precision
    NS_LOG_WARN("Weighted selection failed, returning last path");
    return readyPaths.back();
}

void
MultiPathNadaClient::UpdatePathDistribution(void)
{
    NS_LOG_FUNCTION(this);

    // Calculate the total weight
    double totalWeight = 0.0;
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        totalWeight += it->second.weight;
    }

    if (totalWeight <= 0.0)
    {
        NS_LOG_WARN("Total path weight is zero or negative, using equal distribution");
        totalWeight = m_paths.size();
        for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
        {
            it->second.weight = 1.0;
        }
    }

    // Update the total sending rate
    double totalRateBps = 0.0;
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        DataRate nadaRate = it->second.nada->UpdateRate();
        if (nadaRate.GetBitRate() < 100000)
        { // Less than 100 kbps
            NS_LOG_WARN("NADA rate too low for path " << it->first << ", using minimum rate");
            nadaRate = DataRate("100kbps");
        }
        it->second.currentRate = nadaRate;
        totalRateBps += nadaRate.GetBitRate();
    }

    // Set a minimum total rate
    if (totalRateBps < 500000)
    { // Less than 500 kbps
        NS_LOG_WARN("Total rate too low (" << totalRateBps / 1000 << " kbps), using minimum rate");
        totalRateBps = 500000; // 500 kbps minimum
    }

    m_totalRate = DataRate(totalRateBps);

    // Log the current path distribution
    NS_LOG_INFO("Path distribution update at " << Simulator::Now().GetSeconds() << "s:");
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        double pathShare = it->second.weight / totalWeight;
        NS_LOG_INFO("  Path " << it->first << ": weight=" << it->second.weight << " ("
                              << pathShare * 100.0 << "%), rate="
                              << it->second.currentRate.GetBitRate() / 1000000.0 << " Mbps");
    }

    // Schedule next update using the configured interval
    m_updateEvent =
        Simulator::Schedule(m_updateInterval, &MultiPathNadaClient::UpdatePathDistribution, this);
}

bool
MultiPathNadaClient::SendPacketOnPath(uint32_t pathId, Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << pathId << packet);

    // Find the path info
    std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        NS_LOG_ERROR("Cannot send on path " << pathId << ": path not found");
        return false;
    }

    if (!it->second.client)
    {
        NS_LOG_ERROR("Cannot send on path " << pathId << ": client is null");
        return false;
    }

    Ptr<Socket> socket = it->second.client->GetSocket();
    if (!socket)
    {
        NS_LOG_WARN("Cannot send on path " << pathId << ": socket not ready");
        return false;
    }

    // Try to send the packet using the client
    try
    {
        // Create a header with a sequence number for tracking
        static uint32_t seqNumber = 0;
        seqNumber++;

        // Add sequence number to packet for tracking
        NadaHeader header;
        header.SetSequenceNumber(seqNumber); // Assuming SetSequenceNumber is the correct method
        packet->AddHeader(header);

        // Send the packet directly using the socket
        int bytes = socket->Send(packet);

        if (bytes > 0)
        {
            // Successfully sent packet
            it->second.packetsSent++;
            m_totalPacketsSent++;
            NS_LOG_INFO("Sent packet #" << seqNumber << " on path "
                      << pathId << " (total sent on this path: " << it->second.packetsSent
                      << ", total overall: " << m_totalPacketsSent << ")");
            return true;
        }
        else
        {
            NS_LOG_ERROR("Failed to send packet on path " << pathId << ": socket returned " << bytes);
            return false;
        }
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception while sending on path " << pathId << ": " << e.what());
        return false;
    }
}

void MultiPathNadaClient::SendPackets(void)
{
    NS_LOG_FUNCTION(this);

    if (!m_running)
    {
        NS_LOG_WARN("SendPackets called but client is not running");
        return;
    }

    // Check if we've sent enough packets
    if (m_maxPackets > 0 && m_totalPacketsSent >= m_maxPackets)
    {
        NS_LOG_INFO("Max packets reached (" << m_totalPacketsSent << " >= " << m_maxPackets << ")");
        return;
    }

    // Get available paths with valid sockets
    std::vector<uint32_t> readyPaths;
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        if (it->second.client && it->second.client->GetSocket() &&
            it->second.client->GetSocket() != nullptr)
        {
            readyPaths.push_back(it->first);
        }
    }

    if (readyPaths.empty())
    {
        NS_LOG_WARN("No paths with valid sockets, retrying in 500ms");
        if (!m_sendEvent.IsExpired()) {
            Simulator::Cancel(m_sendEvent);
        }
        m_sendEvent = Simulator::Schedule(MilliSeconds(500),
            &MultiPathNadaClient::SendPackets, this);
        return;
    }

    // Select path based on strategy
    uint32_t selectedPathId = 0;

    try {
        if (m_pathSelectionStrategy == 1) { // Best path
            selectedPathId = GetBestPath(readyPaths);
        } else if (m_pathSelectionStrategy == 2) { // Equal distribution
            static uint32_t roundRobin = 0;
            selectedPathId = readyPaths[roundRobin % readyPaths.size()];
            roundRobin++;
        } else { // Weighted distribution (default)
            selectedPathId = GetWeightedPath(readyPaths);
        }
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Exception in path selection: " << e.what());
        selectedPathId = readyPaths[0]; // Fallback to first available path
    }

    if (selectedPathId == 0) {
        NS_LOG_ERROR("Failed to select a valid path");
        if (!m_sendEvent.IsExpired()) {
            Simulator::Cancel(m_sendEvent);
        }
        m_sendEvent = Simulator::Schedule(MilliSeconds(100),
            &MultiPathNadaClient::SendPackets, this);
        return;
    }

    // Create and send packet
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    bool sent = SendPacketOnPath(selectedPathId, packet);

    if (sent) {
        NS_LOG_INFO("Successfully sent packet on path " << selectedPathId);
        // Schedule next packet with appropriate pacing
        double interval = (m_packetSize * 8.0) / m_totalRate.GetBitRate();
        Time nextSendTime = Seconds(std::max(0.001, interval)); // At least 1ms

        if (!m_sendEvent.IsExpired()) {
            Simulator::Cancel(m_sendEvent);
        }
        m_sendEvent = Simulator::Schedule(nextSendTime,
            &MultiPathNadaClient::SendPackets, this);
    } else {
        NS_LOG_WARN("Failed to send packet on path " << selectedPathId);
        // Retry sooner if send failed
        if (!m_sendEvent.IsExpired()) {
            Simulator::Cancel(m_sendEvent);
        }
        m_sendEvent = Simulator::Schedule(MilliSeconds(50),
            &MultiPathNadaClient::SendPackets, this);
    }
}

void
MultiPathNadaClient::HandleAck(uint32_t pathId, Ptr<Packet> packet, Time delay)
{
    NS_LOG_FUNCTION(this << pathId << packet << delay);

    // Check if the path exists
    std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        NS_LOG_ERROR("Received ACK for unknown path ID " << pathId);
        return;
    }

    // Update path statistics
    it->second.packetsAcked++;
    it->second.lastDelay = delay;
    it->second.lastRtt = delay * 2; // Simplified RTT calculation

    // Process the acknowledgment in NADA
    it->second.nada->ProcessAck(packet, delay);
}

void MultiPathNadaClient::HandleRecv(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    if (!socket) {
        NS_LOG_ERROR("HandleRecv called with null socket");
        return;
    }

    // Find the path ID associated with this socket
    std::map<Ptr<Socket>, uint32_t>::iterator it = m_socketToPathId.find(socket);
    if (it == m_socketToPathId.end())
    {
        NS_LOG_WARN("Received packet for unknown socket, adding to map");
        // Try to find which path this socket belongs to
        for (std::map<uint32_t, PathInfo>::iterator pathIt = m_paths.begin();
             pathIt != m_paths.end(); ++pathIt)
        {
            if (pathIt->second.client && pathIt->second.client->GetSocket() == socket)
            {
                // Found matching socket
                m_socketToPathId[socket] = pathIt->first;
                NS_LOG_INFO("Added socket to path mapping: " << pathIt->first);
                break;
            }
        }

        // Check again if we found the mapping
        it = m_socketToPathId.find(socket);
        if (it == m_socketToPathId.end()) {
            NS_LOG_ERROR("Could not find path for socket");
            return;
        }
    }

    uint32_t pathId = it->second;

    try {
        // Receive the packet
        Address from;
        Ptr<Packet> packet = socket->RecvFrom(from);
        if (!packet || packet->GetSize() == 0)
        {
            NS_LOG_WARN("Empty packet received on path " << pathId);
            return;
        }

        // Calculate delay (in a real implementation, this would use timestamps in the packet)
        // For simulation purposes, we use path-specific delay
        Time delay;

        std::map<uint32_t, PathInfo>::iterator pathIt = m_paths.find(pathId);
        if (pathIt != m_paths.end())
        {
            // Use the configured delay for this path
            delay = pathIt->second.lastDelay;

            // Update path statistics - IMPORTANT FIX
            pathIt->second.packetsAcked++;
            NS_LOG_INFO("Packet acknowledged on path " << pathId <<
                       " (acked: " << pathIt->second.packetsAcked <<
                       ", sent: " << pathIt->second.packetsSent << ")");
        }
        else
        {
            // Fallback delay
            delay = MilliSeconds(50);
        }

        // Process the acknowledgment
        HandleAck(pathId, packet, delay);

    } catch (const std::exception& e) {
        NS_LOG_ERROR("Exception in HandleRecv for path " << pathId << ": " << e.what());
    }
}

void
MultiPathNadaClient::SetVideoMode(bool enable)
{
    NS_LOG_FUNCTION(this << enable);
    m_videoMode = enable;

    // Enable video mode for each path's NADA controller
    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        Ptr<NadaCongestionControl> nada = it->second.nada;
        if (nada)
        {
            nada->SetVideoMode(enable);
        }
    }
}

void
MultiPathNadaClient::VideoFrameAcked(uint32_t pathId, bool isKeyFrame, uint32_t frameSize)
{
    NS_LOG_FUNCTION(this << pathId << isKeyFrame << frameSize);

    // Update video frame stats for the specific path
    std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
    if (it != m_paths.end())
    {
        // Update the NADA controller with frame info
        Time frameInterval = Seconds(1.0 / 30.0); // Assuming 30 fps
        it->second.nada->UpdateVideoFrameInfo(frameSize, isKeyFrame, frameInterval);
    }
}

bool
MultiPathNadaClient::SelectPathForFrame(bool isKeyFrame, uint32_t& pathId)
{
    // Select path based on the current path selection strategy and frame type
    if (m_paths.empty())
    {
        return false;
    }

    if (m_pathSelectionStrategy == 1) // Best path strategy
    {
        // Find path with lowest RTT or highest rate
        uint32_t bestPathId = 0;
        Time lowestRtt = Time::Max();

        for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
        {
            if (it->second.lastRtt < lowestRtt)
            {
                lowestRtt = it->second.lastRtt;
                bestPathId = it->first;
            }
        }

        if (bestPathId > 0)
        {
            pathId = bestPathId;
            return true;
        }
    }
    else if (m_pathSelectionStrategy == 2) // Equal (use all paths)
    {
        // For key frames, use all paths with appropriate weights
        // For simplicity, we'll just return a different path for each call
        static uint32_t counter = 0;
        counter++;

        if (m_paths.size() > 0)
        {
            pathId = (counter % m_paths.size()) + 1; // Assuming path IDs start at 1
            return true;
        }
    }

    // Default: weighted strategy
    // The existing SendPackets method already handles this
    return false; // Let the regular method handle it
}

void MultiPathNadaClient::ReportSocketStatus()
{
    if (!m_running) return;

    NS_LOG_INFO("=== Socket Status Report at " << Simulator::Now().GetSeconds() << "s ===");

    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        bool hasClient = (it->second.client != nullptr);
        bool hasSocket = false;
        bool socketValid = false;

        if (hasClient) {
            Ptr<Socket> socket = it->second.client->GetSocket();
            hasSocket = (socket != nullptr);
            if (hasSocket) {
                // Check if socket is valid (not errored)
                Socket::SocketErrno error = socket->GetErrno();
                socketValid = (error == Socket::ERROR_NOTERROR);
            }
        }

        NS_LOG_INFO("Path " << it->first << ": "
            << (hasClient ? "Has client" : "NO CLIENT") << ", "
            << (hasSocket ? "Has socket" : "NO SOCKET") << ", "
            << (socketValid ? "Socket valid" : "Socket NOT valid") << ", "
            << "sent=" << it->second.packetsSent
            << ", acked=" << it->second.packetsAcked);
    }

    // Schedule next report
    Simulator::Schedule(MilliSeconds(1000), &MultiPathNadaClient::ReportSocketStatus, this);
}

uint32_t MultiPathNadaClient::GetPacketSize(void) const
{
    return m_packetSize;
}

bool
MultiPathNadaClient::IsReady(void) const
{
    NS_LOG_FUNCTION(this);

    if (!m_running)
    {
        NS_LOG_INFO("Client not running yet");
        return false;
    }

    if (m_paths.empty())
    {
        NS_LOG_INFO("No paths configured");
        return false;
    }

    // Check if at least one path has a valid socket
    bool anyPathReady = false;
    for (std::map<uint32_t, PathInfo>::const_iterator it = m_paths.begin(); it != m_paths.end();
         ++it)
    {
        if (it->second.client && it->second.client->GetSocket() != nullptr)
        {
            anyPathReady = true;
            break;
        }
    }

    if (!anyPathReady)
    {
        NS_LOG_INFO("No paths have valid sockets yet");
    }

    return anyPathReady;
}

// MultiPathNadaClientHelper implementation
MultiPathNadaClientHelper::MultiPathNadaClientHelper()
{
    m_factory.SetTypeId("ns3::MultiPathNadaClient");
}

MultiPathNadaClientHelper::~MultiPathNadaClientHelper()
{
}

void
MultiPathNadaClientHelper::SetAttribute(std::string name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
MultiPathNadaClientHelper::Install(Ptr<Node> node) const
{
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
MultiPathNadaClientHelper::Install(NodeContainer c) const
{
    ApplicationContainer apps;
    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
    {
        apps.Add(InstallPriv(*i));
    }
    return apps;
}

Ptr<Application>
MultiPathNadaClientHelper::InstallPriv(Ptr<Node> node) const
{
    Ptr<MultiPathNadaClient> app = m_factory.Create<MultiPathNadaClient>();
    node->AddApplication(app);
    return app;
}

} // namespace ns3
