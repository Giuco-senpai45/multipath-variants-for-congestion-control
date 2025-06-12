#include "mp-nada-client.h"

#include "nada-header.h"
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
                          MakeUintegerChecker<uint32_t>())
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
                          MakeUintegerChecker<uint32_t>(0, 5));
    return tid;
}

MultiPathNadaClient::MultiPathNadaClient()
    : m_isVideoMode(false),
      m_isKeyFrame(false),
      m_packetSize(1024),
      m_maxPackets(0),
      m_running(false),
      m_sendEvent(),
      m_updateEvent(),
      m_totalRate(DataRate("500kbps")),
      m_updateInterval(MilliSeconds(200)),
      m_pathSelectionStrategy(WEIGHTED),
      m_totalPacketsSent(0),
      m_lastUpdateTime(Seconds(0)),
      m_videoReceiver(nullptr),
      m_targetBufferLength(3.0),
      m_bufferWeightFactor(0.3)
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

void
MultiPathNadaClient::InitializePathSocket(uint32_t pathId)
{
    NS_LOG_FUNCTION(this << pathId);

    std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
    if (it == m_paths.end() || !it->second.client)
    {
        NS_LOG_ERROR("Path " << pathId << " not found or has null client");
        return;
    }

    NS_LOG_INFO("Initializing socket for path " << pathId);

    // **FIX: Create UDP socket properly**
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

    // **CRITICAL FIX: Proper address binding**
    if (InetSocketAddress::IsMatchingType(it->second.localAddress))
    {
        InetSocketAddress localAddr = InetSocketAddress::ConvertFrom(it->second.localAddress);

        // Use path-specific port to avoid conflicts
        if (localAddr.GetPort() == 0)
        {
            localAddr.SetPort(9000 + pathId);
        }

        int bindResult = socket->Bind(localAddr);
        if (bindResult != 0)
        {
            NS_LOG_ERROR("Failed to bind socket for path " << pathId << " to " << localAddr);
            return;
        }
        NS_LOG_INFO("Socket bound to " << localAddr);
    }

    // **CRITICAL FIX: Proper connection**
    if (InetSocketAddress::IsMatchingType(it->second.remoteAddress))
    {
        InetSocketAddress remoteAddr = InetSocketAddress::ConvertFrom(it->second.remoteAddress);
        int connectResult = socket->Connect(remoteAddr);
        if (connectResult != 0)
        {
            NS_LOG_ERROR("Failed to connect socket for path " << pathId << " to " << remoteAddr);
            return;
        }
        NS_LOG_INFO("Socket connected to " << remoteAddr);
    }

    // Set receive callback
    socket->SetRecvCallback(MakeCallback(&MultiPathNadaClient::HandleRecv, this));

    // **CRITICAL FIX: Store socket in client properly**
    it->second.client->SetSocket(socket);
    m_socketToPathId[socket] = pathId;

    // **CRITICAL FIX: Initialize NADA with socket**
    if (it->second.nada)
    {
        it->second.nada->Init(socket);

        // Set initial rate based on path capacity
        DataRate pathRate = it->second.currentRate;
        it->second.nada->SetInitialRate(pathRate);

        NS_LOG_INFO("NADA initialized for path " << pathId << " with rate " << pathRate);
    }

    NS_LOG_INFO("Path " << pathId << " socket initialized successfully");
}

void
MultiPathNadaClient::ValidatePathSocket(uint32_t pathId)
{
    NS_LOG_FUNCTION(this << pathId);

    std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
    if (it == m_paths.end() || !it->second.client)
    {
        return;
    }

    Ptr<Socket> socket = it->second.client->GetSocket();
    bool ready = IsSocketReady(socket);

    if (ready)
    {
        NS_LOG_INFO("Path " << pathId << " socket validated successfully");

        // Initialize NADA controller with the socket
        if (it->second.nada)
        {
            it->second.nada->Init(socket);
        }
    }
    else
    {
        NS_LOG_WARN("Path " << pathId << " socket validation failed, retrying...");

        // Retry after a delay
        Simulator::Schedule(MilliSeconds(200),
                            &MultiPathNadaClient::InitializePathSocket,
                            this,
                            pathId);
    }
}

void
MultiPathNadaClient::StartApplication(void)
{
    NS_LOG_FUNCTION(this);

    if (m_running)
    {
        NS_LOG_WARN("Application already running, ignoring StartApplication");
        return;
    }

    m_running = true;
    NS_LOG_INFO("MultiPathNadaClient starting with " << m_paths.size() << " paths");

    // Initialize socket for each path with staggered timing
    uint32_t delay = 0;
    for (auto& pathPair : m_paths)
    {
        // Schedule initialization with increasing delays to avoid race conditions
        Simulator::Schedule(MilliSeconds(delay),
                            &MultiPathNadaClient::InitializePathSocket,
                            this,
                            pathPair.first);

        // Add 50ms between each path initialization
        delay += 50;
    }

    // Schedule a validation check after all paths should be initialized
    Simulator::Schedule(MilliSeconds(delay + 500), &MultiPathNadaClient::ValidateAllSockets, this);

    // Schedule path distribution updates
    m_updateEvent =
        Simulator::Schedule(m_updateInterval, &MultiPathNadaClient::UpdatePathDistribution, this);
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

    // Fail-safe: If only one path is available, return it immediately
    if (readyPaths.size() == 1)
    {
        NS_LOG_INFO("Only one path available, returning path " << readyPaths[0]);
        return readyPaths[0];
    }

    // Instead of selecting a single best path, calculate dynamic weights
    std::map<uint32_t, double> pathMetrics;
    double totalMetric = 0.0;

    // First pass: calculate metrics for each path
    for (auto pathId : readyPaths)
    {
        std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
        if (it == m_paths.end())
        {
            NS_LOG_WARN("Path " << pathId << " found in readyPaths but not in m_paths");
            continue;
        }

        // Use a combination of RTT and current rate for the metric
        double rttMs = it->second.lastRtt.GetMilliSeconds();
        double rateMbps = it->second.currentRate.GetBitRate() / 1000000.0;

        // Avoid division by zero
        if (rttMs < 1.0)
        {
            rttMs = 1.0;
        }

        // Calculate metric that favors high rate and low RTT
        double metric = rateMbps / rttMs;

        // Apply a reasonable minimum to avoid starving lower-quality paths completely
        metric = std::max(metric, 0.01);

        pathMetrics[pathId] = metric;
        totalMetric += metric;

        NS_LOG_INFO("Path " << pathId << " metric: " << metric << " (rate=" << rateMbps
                            << "Mbps, rtt=" << rttMs << "ms)");
    }

    // Safety check - if no metrics were successfully calculated, use the first path
    if (pathMetrics.empty())
    {
        NS_LOG_WARN("No path metrics calculated, selecting first available path");
        return readyPaths[0];
    }

    // If total metric is zero or negative, use equal weights
    if (totalMetric <= 0.0)
    {
        NS_LOG_WARN("Total metric is zero or negative, using equal weights");
        for (auto pathId : readyPaths)
        {
            pathMetrics[pathId] = 1.0 / readyPaths.size();
        }
        totalMetric = 1.0;
    }

    // Normalize metrics to weights
    std::map<uint32_t, double> normalizedWeights;
    for (auto& pair : pathMetrics)
    {
        normalizedWeights[pair.first] = pair.second / totalMetric;

        // Update path weights in the path info for future reference
        std::map<uint32_t, PathInfo>::iterator pathIt = m_paths.find(pair.first);
        if (pathIt != m_paths.end())
        {
            pathIt->second.weight = normalizedWeights[pair.first];
        }
    }

    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double r = rng->GetValue(0.0, 1.0);
    double cumulativeProb = 0.0;

    NS_LOG_INFO("Random selection value: " << r);

    for (auto pathId : readyPaths)
    {
        if (normalizedWeights.find(pathId) == normalizedWeights.end())
        {
            continue; // Skip paths without calculated weights
        }

        cumulativeProb += normalizedWeights[pathId];
        if (r <= cumulativeProb)
        {
            NS_LOG_INFO("Selected path " << pathId << " (weight=" << normalizedWeights[pathId]
                                         << ")");
            return pathId;
        }
    }

    // Fallback - should rarely happen
    NS_LOG_WARN("Selection failed, returning first path");
    return readyPaths[0];
}

uint32_t
MultiPathNadaClient::GetWeightedPath(const std::vector<uint32_t>& readyPaths)
{
    if (readyPaths.empty())
    {
        std::cout << "ERROR: GetWeightedPath called with empty path list" << std::endl;
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
            std::cout << "WARN: Path " << pathId << " not found in m_paths" << std::endl;
            continue;
        }

        totalWeight += it->second.weight;
        normalizedWeights[pathId] = it->second.weight;
    }

    if (totalWeight <= 0.0)
    {
        std::cout << "WARN: Total weight is zero or negative, using equal weights" << std::endl;
        // Use equal weights
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

    try
    {
        // Create a proper random variable instance using ns-3's CreateObject
        Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
        double r = rng->GetValue(0.0, 1.0);
        // Select path based on random value
        double cumulativeProb = 0.0;
        for (auto pathId : readyPaths)
        {
            if (normalizedWeights.find(pathId) == normalizedWeights.end())
            {
                std::cout << "WARN: Path " << pathId << " has no weight, skipping" << std::endl;
                continue;
            }

            cumulativeProb += normalizedWeights[pathId];

            if (r <= cumulativeProb)
            {
                return pathId;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "EXCEPTION in random path selection: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cout << "UNKNOWN EXCEPTION in random path selection" << std::endl;
    }

    // If we got here, selection failed or hit an exception
    std::cout << "WARN: Weighted selection failed, returning first path" << std::endl;
    if (!readyPaths.empty())
    {
        return readyPaths[0];
    }

    return 0; // Invalid path ID as a last resort
}

uint32_t
MultiPathNadaClient::GetFrameAwarePath(const std::vector<uint32_t>& readyPaths, bool isKeyFrame)
{
    NS_LOG_FUNCTION(this << isKeyFrame);

    if (readyPaths.empty())
    {
        NS_LOG_ERROR("GetFrameAwarePath called with empty path list");
        return 0; // Invalid path ID
    }

    // For key frames, prioritize reliability (lowest RTT)
    if (isKeyFrame)
    {
        NS_LOG_INFO("Key frame: selecting path with lowest RTT");
        uint32_t bestPathId = readyPaths[0];
        Time lowestRtt = Time::Max();

        for (auto pathId : readyPaths)
        {
            auto it = m_paths.find(pathId);
            if (it != m_paths.end() && it->second.lastRtt < lowestRtt)
            {
                lowestRtt = it->second.lastRtt;
                bestPathId = pathId;
            }
        }

        NS_LOG_INFO("Selected path " << bestPathId << " for key frame with RTT "
                                     << (bestPathId && m_paths.find(bestPathId) != m_paths.end()
                                             ? m_paths[bestPathId].lastRtt.GetMilliSeconds()
                                             : 0)
                                     << "ms");
        return bestPathId;
    }

    // For normal frames, use weighted distribution (balance load)
    NS_LOG_INFO("Delta frame: using weighted distribution");
    return GetWeightedPath(readyPaths);
}

bool
MultiPathNadaClient::SendRedundantlyPath(const std::vector<uint32_t>& readyPaths,
                                         Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this);

    if (readyPaths.empty())
    {
        NS_LOG_ERROR("SendRedundantly called with empty path list");
        return false;
    }

    bool anySuccess = false;

    NS_LOG_INFO("Sending packet redundantly on " << readyPaths.size() << " paths");

    // Send the packet on all available paths for redundancy
    for (auto pathId : readyPaths)
    {
        // Create a fresh copy of the packet for each path
        // (important: avoid sharing the same packet across transmissions)
        Ptr<Packet> packetCopy = Create<Packet>(*packet);

        // Send on this path
        bool sent = SendPacketOnPath(pathId, packetCopy);

        if (sent)
        {
            anySuccess = true;
            NS_LOG_INFO("Successfully sent redundant packet on path " << pathId);
        }
        else
        {
            NS_LOG_WARN("Failed to send redundant packet on path " << pathId);
        }
    }

    return anySuccess;
}

double
MultiPathNadaClient::CalculateBufferWeight(double currentBufferMs, uint32_t pathId)
{
    NS_LOG_FUNCTION(this << currentBufferMs << pathId);

    double targetBufferMs = m_targetBufferLength * 1000.0;
    double bufferDiff = currentBufferMs - targetBufferMs;

    // Get path RTT for responsiveness calculation
    std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        return 0.5; // Default weight if path not found
    }

    double pathRttMs = it->second.lastRtt.GetMilliSeconds();

    // Calculate urgency based on buffer status
    double urgencyFactor = 1.0;

    if (bufferDiff < -1000.0) // Buffer very low (< target - 1s)
    {
        // High urgency: favor fast paths (low RTT, high rate)
        urgencyFactor = 2.0 / (1.0 + pathRttMs / 50.0); // Favor paths with RTT < 50ms
        NS_LOG_INFO("Buffer very low, high urgency for path "
                    << pathId << " (RTT=" << pathRttMs << "ms, urgency=" << urgencyFactor << ")");
    }
    else if (bufferDiff < -500.0) // Buffer low (< target - 0.5s)
    {
        // Medium urgency: slightly favor faster paths
        urgencyFactor = 1.5 / (1.0 + pathRttMs / 100.0);
        NS_LOG_INFO("Buffer low, medium urgency for path " << pathId);
    }
    else if (bufferDiff > 1000.0) // Buffer high (> target + 1s)
    {
        // Low urgency: can afford to use slower paths for load balancing
        urgencyFactor = 0.8 + 0.4 * (pathRttMs / 200.0);
        urgencyFactor = std::min(urgencyFactor, 1.2);
        NS_LOG_INFO("Buffer high, low urgency for path " << pathId);
    }
    else
    {
        // Normal buffer level: balanced approach
        urgencyFactor = 1.0;
    }

    // Combine with path quality metrics
    double rttWeight = 100.0 / (pathRttMs + 10.0); // Lower RTT = higher weight
    double finalWeight = urgencyFactor * rttWeight;

    // Normalize to reasonable range [0.1, 2.0]
    finalWeight = std::max(0.1, std::min(2.0, finalWeight));

    return finalWeight;
}

uint32_t
MultiPathNadaClient::GetBufferAwarePath(const std::vector<uint32_t>& readyPaths)
{
    NS_LOG_FUNCTION(this);

    if (readyPaths.empty())
    {
        NS_LOG_ERROR("GetBufferAwarePath called with empty path list");
        return 0;
    }

    if (readyPaths.size() == 1)
    {
        NS_LOG_INFO("Only one path available, returning path " << readyPaths[0]);
        return readyPaths[0];
    }

    if (!m_videoReceiver)
    {
        NS_LOG_WARN("VideoReceiver not set, using default buffer behavior");
        return GetWeightedPath(readyPaths);
    }

    // Get current buffer length from video receiver
    double currentBufferMs = 0.0;
    if (m_videoReceiver)
    {
        currentBufferMs = m_videoReceiver->GetAverageBufferLength();
    }

    NS_LOG_INFO("Current buffer length: " << currentBufferMs << "ms, target: "
                                          << (m_targetBufferLength * 1000) << "ms");

    // Calculate combined weights for each path
    std::map<uint32_t, double> pathWeights;
    double totalWeight = 0.0;

    for (auto pathId : readyPaths)
    {
        std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
        if (it == m_paths.end())
        {
            NS_LOG_WARN("Path " << pathId << " not found in m_paths");
            continue;
        }

        // Get NADA send rate for this path
        DataRate nadaRate = it->second.nada->UpdateRate();
        double rateMbps = nadaRate.GetBitRate() / 1000000.0;

        // Calculate buffer-based weight
        double bufferWeight = CalculateBufferWeight(currentBufferMs, pathId);

        // Combine rate and buffer weights
        double rateWeight = rateMbps / 10.0; // Normalize assuming max ~10 Mbps
        double combinedWeight =
            (1.0 - m_bufferWeightFactor) * rateWeight + m_bufferWeightFactor * bufferWeight;

        // Apply minimum weight to avoid starvation
        combinedWeight = std::max(combinedWeight, 0.01);

        pathWeights[pathId] = combinedWeight;
        totalWeight += combinedWeight;

        NS_LOG_INFO("Path " << pathId << " weights: rate=" << rateWeight
                            << ", buffer=" << bufferWeight << ", combined=" << combinedWeight
                            << " (NADA rate=" << rateMbps << "Mbps)");
    }

    // Safety check
    if (pathWeights.empty() || totalWeight <= 0.0)
    {
        NS_LOG_WARN("No valid path weights calculated, using first available path");
        return readyPaths[0];
    }

    // Normalize weights and select path
    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double r = rng->GetValue(0.0, 1.0);
    double cumulativeProb = 0.0;

    for (auto pathId : readyPaths)
    {
        if (pathWeights.find(pathId) == pathWeights.end())
        {
            continue;
        }

        double normalizedWeight = pathWeights[pathId] / totalWeight;
        cumulativeProb += normalizedWeight;

        // Update path weight for statistics
        std::map<uint32_t, PathInfo>::iterator pathIt = m_paths.find(pathId);
        if (pathIt != m_paths.end())
        {
            pathIt->second.weight = normalizedWeight;
        }

        if (r <= cumulativeProb)
        {
            NS_LOG_INFO("Selected path " << pathId << " (normalized weight=" << normalizedWeight
                                         << ")");
            return pathId;
        }
    }

    // Fallback
    NS_LOG_WARN("Selection failed, returning first path");
    return readyPaths[0];
}

void
MultiPathNadaClient::UpdatePathDistribution(void)
{
    NS_LOG_FUNCTION(this);

    // Collect current network metrics for all paths
    std::map<uint32_t, double> pathMetrics;
    double totalUtilization = 0.0;

    for (auto& pathPair : m_paths) {
        uint32_t pathId = pathPair.first;
        PathInfo& pathInfo = pathPair.second;

        // Update NADA rate for this path
        DataRate nadaRate = pathInfo.nada->UpdateRate();
        pathInfo.currentRate = nadaRate;

        // Calculate path utilization and quality metrics
        double pathUtilization = 0.0;
        if (pathInfo.packetsSent > 0) {
            pathUtilization = static_cast<double>(pathInfo.packetsAcked) / pathInfo.packetsSent;
        }

        // Calculate comprehensive path quality score
        double rttScore = 1.0 / (1.0 + pathInfo.lastRtt.GetMilliSeconds() / 100.0); // Normalize by 100ms
        double rateScore = nadaRate.GetBitRate() / 10000000.0; // Normalize by 10Mbps
        double utilizationScore = pathUtilization;

        // Combined quality metric
        double qualityScore = (rttScore * 0.3 + rateScore * 0.4 + utilizationScore * 0.3);
        pathMetrics[pathId] = qualityScore;
        totalUtilization += pathUtilization;

        NS_LOG_INFO("Path " << pathId << " metrics: RTT=" << pathInfo.lastRtt.GetMilliSeconds()
                   << "ms, Rate=" << nadaRate.GetBitRate()/1000000.0 << "Mbps, "
                   << "Utilization=" << pathUtilization << ", Quality=" << qualityScore);
    }

    // Update weights based on strategy and current metrics
    switch (m_pathSelectionStrategy) {
        case WEIGHTED: {
            // Dynamic weight adjustment based on quality metrics
            double totalQuality = 0.0;
            for (auto& metric : pathMetrics) {
                totalQuality += metric.second;
            }

            if (totalQuality > 0) {
                for (auto& pathPair : m_paths) {
                    uint32_t pathId = pathPair.first;
                    double normalizedWeight = pathMetrics[pathId] / totalQuality;
                    // Smooth weight changes to avoid oscillation
                    double currentWeight = pathPair.second.weight;
                    double newWeight = 0.7 * currentWeight + 0.3 * normalizedWeight;
                    pathPair.second.weight = newWeight;
                }
            }
            break;
        }

        case BEST_PATH: {
            // Find best path and give it higher weight
            uint32_t bestPath = 0;
            double bestQuality = 0.0;
            for (auto& metric : pathMetrics) {
                if (metric.second > bestQuality) {
                    bestQuality = metric.second;
                    bestPath = metric.first;
                }
            }

            // Give best path 80% weight, others share remaining 20%
            for (auto& pathPair : m_paths) {
                if (pathPair.first == bestPath) {
                    pathPair.second.weight = 0.8;
                } else {
                    pathPair.second.weight = 0.2 / (m_paths.size() - 1);
                }
            }
            break;
        }

        case BUFFER_AWARE: {
            // Adjust weights based on buffer status
            double currentBufferMs = 0.0;
            if (m_videoReceiver) {
                currentBufferMs = m_videoReceiver->GetAverageBufferLength();
            }

            double targetBufferMs = m_targetBufferLength * 1000.0;
            double bufferRatio = currentBufferMs / targetBufferMs;

            // If buffer is low, prioritize fastest/most reliable paths
            if (bufferRatio < 0.5) {
                // Emergency mode: use best path heavily
                uint32_t bestPath = GetBestPathByRTT();
                for (auto& pathPair : m_paths) {
                    if (pathPair.first == bestPath) {
                        pathPair.second.weight = 0.9;
                    } else {
                        pathPair.second.weight = 0.1 / (m_paths.size() - 1);
                    }
                }
                NS_LOG_WARN("Buffer critically low (" << currentBufferMs
                           << "ms), emergency mode on path " << bestPath);
            } else {
                // Normal mode: balance based on quality and buffer needs
                for (auto& pathPair : m_paths) {
                    uint32_t pathId = pathPair.first;
                    double bufferWeight = CalculateBufferWeight(currentBufferMs, pathId);
                    double qualityWeight = pathMetrics[pathId];

                    // Combine buffer urgency with path quality
                    double combinedWeight = m_bufferWeightFactor * bufferWeight +
                                          (1.0 - m_bufferWeightFactor) * qualityWeight;
                    pathPair.second.weight = combinedWeight;
                }

                // Normalize weights
                double totalWeight = 0.0;
                for (auto& pathPair : m_paths) {
                    totalWeight += pathPair.second.weight;
                }
                if (totalWeight > 0) {
                    for (auto& pathPair : m_paths) {
                        pathPair.second.weight /= totalWeight;
                    }
                }
            }
            break;
        }

        default:
            // Keep existing weights for other strategies
            break;
    }

    // Calculate and log total effective rate
    double totalRateBps = 0.0;
    for (auto& pathPair : m_paths) {
        totalRateBps += pathPair.second.currentRate.GetBitRate();
    }
    m_totalRate = DataRate(totalRateBps);

    NS_LOG_INFO("Updated path distribution - Total rate: "
               << totalRateBps/1000000.0 << " Mbps, Total utilization: "
               << totalUtilization/m_paths.size());

    // Schedule next update
    m_updateEvent = Simulator::Schedule(m_updateInterval,
                                       &MultiPathNadaClient::UpdatePathDistribution, this);
}

uint32_t
MultiPathNadaClient::GetBestPathByRTT()
{
    uint32_t bestPath = 0;
    Time lowestRtt = Time::Max();

    for (auto& pathPair : m_paths) {
        if (pathPair.second.lastRtt < lowestRtt) {
            lowestRtt = pathPair.second.lastRtt;
            bestPath = pathPair.first;
        }
    }

    return bestPath;
}

void
MultiPathNadaClient::SetPathSelectionStrategy(uint32_t strategy)
{
    NS_LOG_FUNCTION(this << strategy);

    if (strategy > BUFFER_AWARE)
    {
        NS_LOG_WARN("Invalid strategy value " << strategy << ", using WEIGHTED (0)");
        m_pathSelectionStrategy = WEIGHTED;
    }
    else
    {
        m_pathSelectionStrategy = strategy;
        NS_LOG_INFO("Path selection strategy changed to "
                    << GetStrategyName(m_pathSelectionStrategy));
    }

    // Update path weights immediately based on the new strategy
    UpdatePathDistribution();
}

// Helper method to convert strategy enum to string
std::string
MultiPathNadaClient::GetStrategyName(uint32_t strategy) const
{
    switch (strategy)
    {
    case WEIGHTED:
        return "WEIGHTED";
    case BEST_PATH:
        return "BEST_PATH";
    case EQUAL:
        return "EQUAL";
    case REDUNDANT:
        return "REDUNDANT";
    case FRAME_AWARE:
        return "FRAME_AWARE";
    case BUFFER_AWARE:
        return "BUFFER_AWARE";
    default:
        return "UNKNOWN";
    }
}

bool
MultiPathNadaClient::SendPacketOnPath(uint32_t pathId, Ptr<Packet> packet)
{
    if (!packet)
    {
        return false;
    }

    auto it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        return false;
    }

    if (!it->second.client)
    {
        return false;
    }

    Ptr<Socket> socket = it->second.client->GetSocket();
    if (!socket)
    {
        return false;
    }

    // More lenient socket readiness check for sending
    if (socket->GetTxAvailable() == 0)
    {
        NS_LOG_WARN("Path " << pathId << " socket has no TX buffer available, skipping");
        return false;
    }

    try
    {
        NadaHeader header;
        header.SetTimestamp(Simulator::Now());

        packet->AddHeader(header);

        // Actually send the packet
        int result = socket->Send(packet);

        if (result > 0)
        {
            it->second.packetsSent++;
            m_totalPacketsSent++;
            NS_LOG_DEBUG("Packet sent successfully on path " << pathId
                        << " (total sent: " << it->second.packetsSent << ")");
            return true;
        }
        else
        {
            NS_LOG_WARN("Failed to send packet on path " << pathId << ", result: " << result);
            return false;
        }
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception in SendPacketOnPath: " << e.what());
        return false;
    }
}

bool
MultiPathNadaClient::Send(Ptr<Packet> packet)
{
    if (!m_running)
    {
        return false;
    }

    // Check if we've sent enough packets
    if (m_maxPackets > 0 && m_totalPacketsSent >= m_maxPackets)
    {
        return false;
    }

    Ptr<Packet> newPacket = nullptr;
    try
    {
        newPacket = Create<Packet>(500);
    }
    catch (const std::exception& e)
    {
        std::cout << "EXCEPTION creating packet in Send: " << e.what() << std::endl;
        return false;
    }

    if (!newPacket)
    {
        std::cout << "ERROR: Failed to create packet in Send" << std::endl;
        return false;
    }

    // Get available paths with valid sockets
    std::vector<uint32_t> readyPaths;
    try
    {
        for (const auto& pathPair : m_paths)
        {
            try
            {
                if (pathPair.second.client && pathPair.second.client->GetSocket() &&
                    IsSocketReady(pathPair.second.client->GetSocket()))
                {
                    readyPaths.push_back(pathPair.first);
                }
            }
            catch (const std::exception& e)
            {
                std::cout << "EXCEPTION checking path " << pathPair.first << ": " << e.what()
                          << std::endl;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "EXCEPTION gathering ready paths: " << e.what() << std::endl;
    }
    // Check if we have any ready paths
    if (readyPaths.empty())
    {
        return false;
    }
    bool sent = false;
    try
    {
        // Select path using the strategy implementations
        uint32_t selectedPathId = 0;

        switch (m_pathSelectionStrategy)
        {
        case BEST_PATH: {
            selectedPathId = GetBestPath(readyPaths);
            break;
        }

        case WEIGHTED: {
            selectedPathId = GetWeightedPath(readyPaths);
            break;
        }

        case EQUAL: {
            // Simple round-robin
            static uint32_t roundRobin = 0;
            selectedPathId = readyPaths[roundRobin % readyPaths.size()];
            roundRobin++;
            break;
        }

        case REDUNDANT: {
            sent = SendRedundantlyPath(readyPaths, newPacket);
            return sent; // Early return for redundant case
        }

        case FRAME_AWARE: {
            selectedPathId = GetFrameAwarePath(readyPaths, m_isKeyFrame);
            break;
        }

        case BUFFER_AWARE: {
            selectedPathId = GetBufferAwarePath(readyPaths);
            break;
        }

        default: {
            if (!readyPaths.empty())
            {
                selectedPathId = readyPaths[0];
            }
            break;
        }
        }

        // Now send on the selected path
        if (selectedPathId > 0)
        {
            sent = SendPacketOnPath(selectedPathId, newPacket);
        }
        else
        {
            std::cout << "ERROR: Invalid path ID: " << selectedPathId << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "EXCEPTION in path selection: " << e.what() << std::endl;
        return false;
    }
    catch (...)
    {
        std::cout << "UNKNOWN EXCEPTION in Send method" << std::endl;
        return false;
    }
    return sent;
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
    it->second.lastDelay = delay;
    it->second.lastRtt = delay * 2; // Simplified RTT calculation
}

void
MultiPathNadaClient::HandleRecv(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    if (!socket)
    {
        NS_LOG_ERROR("HandleRecv called with null socket");
        return;
    }

    // Find the path ID associated with this socket
    std::map<Ptr<Socket>, uint32_t>::iterator it = m_socketToPathId.find(socket);
    if (it == m_socketToPathId.end())
    {
        NS_LOG_WARN("Received packet for unknown socket");
        return;
    }

    uint32_t pathId = it->second;
    NS_LOG_DEBUG("HandleRecv for path " << pathId);

    // Use a try-catch block for the entire receive operation
    try
    {
        // Receive the packet
        Address from;
        Ptr<Packet> packet = socket->RecvFrom(from);

        // Check if we received a valid packet
        if (!packet)
        {
            NS_LOG_WARN("Null packet received on path " << pathId);
            return;
        }

        uint32_t originalSize = packet->GetSize();
        if (originalSize == 0)
        {
            NS_LOG_WARN("Empty packet received on path " << pathId);
            return;
        }

        // Get path info
        std::map<uint32_t, PathInfo>::iterator pathIt = m_paths.find(pathId);
        if (pathIt == m_paths.end())
        {
            NS_LOG_ERROR("Path " << pathId << " not found in paths map");
            return;
        }

        // Update path statistics regardless of header processing
        pathIt->second.packetsAcked++;

        // Default delay value if we can't extract it from header
        Time delay = MilliSeconds(50);

        // SAFER APPROACH: Check size AND ONLY try to deserialize if the size is correct
        if (packet->GetSize() >= NadaHeader::GetStaticSize())
        {
            // Create a copy of the packet for inspection
            Ptr<Packet> copy = packet->Copy();

            // Use a try-catch specifically for the header operations
            try
            {
                NadaHeader header;
                if (copy->RemoveHeader(header))
                {
                    // Only use timestamp if it seems valid
                    if (header.GetTimestamp() > Time::Min())
                    {
                        delay = Simulator::Now() - header.GetTimestamp();
                        pathIt->second.lastDelay = delay;
                    }
                }
            }
            catch (const std::exception& e)
            {
                NS_LOG_WARN("Exception during header processing: " << e.what());
                // Continue with default delay values
            }
        }
        else
        {
            NS_LOG_DEBUG("Packet too small for NADA header ("
                         << packet->GetSize() << " bytes), skipping header processing");
        }

        // Process the acknowledgment
        HandleAck(pathId, packet, delay);

        NS_LOG_INFO("Packet acknowledged on path "
                    << pathId << " (acked: " << pathIt->second.packetsAcked << ", sent: "
                    << pathIt->second.packetsSent << ", size: " << originalSize << " bytes)");
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception in HandleRecv for path " << pathId << ": " << e.what());
    }
}

void
MultiPathNadaClient::ValidateAllSockets(void)
{
    NS_LOG_FUNCTION(this);

    uint32_t readyCount = 0;
    uint32_t totalCount = 0;

    for (auto& pathPair : m_paths)
    {
        uint32_t pathId = pathPair.first;
        totalCount++;

        try
        {
            if (!pathPair.second.client)
            {
                NS_LOG_WARN("Path " << pathId << " has null client, reinitializing...");
                Simulator::Schedule(MilliSeconds(100),
                                  &MultiPathNadaClient::InitializePathSocket,
                                  this, pathId);
                continue;
            }

            Ptr<Socket> socket = pathPair.second.client->GetSocket();
            if (!socket)
            {
                NS_LOG_WARN("Path " << pathId << " has null socket, reinitializing...");
                Simulator::Schedule(MilliSeconds(100),
                                  &MultiPathNadaClient::InitializePathSocket,
                                  this, pathId);
                continue;
            }

            bool ready = IsSocketReady(socket);
            if (ready)
            {
                readyCount++;
                NS_LOG_DEBUG("Path " << pathId << " socket is ready");

                // **FIX: Initialize NADA here if not done**
                if (pathPair.second.nada && !pathPair.second.nada->IsInitialized())
                {
                    pathPair.second.nada->Init(socket);
                    pathPair.second.nada->SetInitialRate(pathPair.second.currentRate);
                }
            }
            else
            {
                NS_LOG_WARN("Path " << pathId << " socket not ready");
            }
        }
        catch (const std::exception& e)
        {
            NS_LOG_ERROR("Exception checking socket for path " << pathId << ": " << e.what());
        }
    }

    // **FIX: More lenient validation**
    if (readyCount == 0 && totalCount > 0)
    {
        NS_LOG_WARN("No sockets are ready (" << readyCount << "/" << totalCount
                   << "), scheduling validation retry in 1s");
        Simulator::Schedule(Seconds(1.0), &MultiPathNadaClient::ValidateAllSockets, this);
        return;
    }

    NS_LOG_INFO("Socket validation: " << readyCount << "/" << totalCount << " ready");

    // Schedule periodic status reports
    Simulator::Schedule(MilliSeconds(500), &MultiPathNadaClient::ReportSocketStatus, this);
}


void
MultiPathNadaClient::GetAvailablePaths(std::vector<uint32_t>& outPaths) const
{
    NS_LOG_FUNCTION(this);
    outPaths.clear();

    for (const auto& pathPair : m_paths)
    {
        if (pathPair.second.client != nullptr)
        {
            try
            {
                Ptr<Socket> socket = pathPair.second.client->GetSocket();
                if (socket && socket->GetErrno() == Socket::ERROR_NOTERROR)
                {
                    outPaths.push_back(pathPair.first);
                }
            }
            catch (const std::exception& e)
            {
                NS_LOG_ERROR("Exception checking path: " << e.what());
            }
        }
    }

    NS_LOG_INFO("Found " << outPaths.size() << " available paths");
}

void
MultiPathNadaClient::SetNadaAdaptability(uint32_t pathId,
                                         DataRate minRate,
                                         DataRate maxRate,
                                         Time rttMax)
{
    std::map<uint32_t, PathInfo>::iterator it = m_paths.find(pathId);
    if (it == m_paths.end() || !it->second.nada)
    {
        NS_LOG_ERROR("Cannot set NADA parameters for path " << pathId);
        return;
    }

    it->second.nada->SetMinRate(minRate);
    it->second.nada->SetMaxRate(maxRate);
    it->second.nada->SetRttMax(rttMax);

    // Calculate and set reference rate (suggested approach: 0.5 * maxRate)
    it->second.nada->SetXRef(DataRate(maxRate.GetBitRate() * 0.5));

    NS_LOG_INFO("Set NADA parameters for path " << pathId << ": "
                                                << "minRate=" << minRate << ", "
                                                << "maxRate=" << maxRate << ", "
                                                << "rttMax=" << rttMax.GetMilliSeconds() << "ms");
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
MultiPathNadaClient::SetKeyFrameStatus(bool isKeyFrame)
{
    NS_LOG_FUNCTION(this << isKeyFrame);
    m_isKeyFrame = isKeyFrame;
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

void
MultiPathNadaClient::SetVideoReceiver(Ptr<VideoReceiver> receiver)
{
    NS_LOG_FUNCTION(this << receiver);
    m_videoReceiver = receiver;
}

void
MultiPathNadaClient::SetBufferAwareParameters(double targetBufferLength, double bufferWeightFactor)
{
    NS_LOG_FUNCTION(this << targetBufferLength << bufferWeightFactor);
    m_targetBufferLength = targetBufferLength;
    m_bufferWeightFactor = std::max(0.0, std::min(1.0, bufferWeightFactor)); // Clamp to [0,1]
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

    return false; // Let the regular method handle it
}

void
MultiPathNadaClient::ReportSocketStatus()
{
    if (!m_running)
    {
        return;
    }

    NS_LOG_INFO("=== Socket Status Report at " << Simulator::Now().GetSeconds() << "s ===");

    for (std::map<uint32_t, PathInfo>::iterator it = m_paths.begin(); it != m_paths.end(); ++it)
    {
        bool hasClient = (it->second.client != nullptr);
        bool hasSocket = false;
        bool socketValid = false;
        uint32_t availableForSend = 0;

        if (hasClient)
        {
            Ptr<Socket> socket = it->second.client->GetSocket();
            hasSocket = (socket != nullptr);
            if (hasSocket)
            {
                // Check if socket is valid (not errored)
                Socket::SocketErrno error = socket->GetErrno();
                socketValid = (error == Socket::ERROR_NOTERROR);
                availableForSend = socket->GetTxAvailable();
            }
        }

        NS_LOG_INFO("Path " << it->first << ": " << (hasClient ? "Has client" : "NO CLIENT") << ", "
                            << (hasSocket ? "Has socket" : "NO SOCKET") << ", "
                            << (socketValid ? "Socket valid" : "Socket NOT valid") << ", "
                            << "TxAvail=" << availableForSend << ", "
                            << "sent=" << it->second.packetsSent
                            << ", acked=" << it->second.packetsAcked);
    }

    // Schedule next report
    Simulator::Schedule(MilliSeconds(1000), &MultiPathNadaClient::ReportSocketStatus, this);
}

uint32_t
MultiPathNadaClient::GetPacketSize(void) const
{
    return m_packetSize;
}

bool
MultiPathNadaClient::IsReady(void) const
{
    NS_LOG_FUNCTION(this);

    if (!m_running)
    {
        NS_LOG_DEBUG("Client not running, not ready");
        return false;
    }

    if (m_paths.empty())
    {
        NS_LOG_DEBUG("No paths configured, not ready");
        return false;
    }

    // Check for at least one ready path - with more debugging
    uint32_t readyCount = 0;
    uint32_t totalCount = 0;

    for (const auto& pathPair : m_paths)
    {
        totalCount++;
        if (pathPair.second.client != nullptr)
        {
            try
            {
                Ptr<Socket> socket = pathPair.second.client->GetSocket();
                if (socket && socket->GetErrno() == Socket::ERROR_NOTERROR)
                {
                    readyCount++;
                    NS_LOG_DEBUG("Path " << pathPair.first << " is ready");
                }
            }
            catch (const std::exception& e)
            {
                NS_LOG_ERROR("Exception checking readiness: " << e.what());
            }
        }
    }

    NS_LOG_INFO("Ready paths: " << readyCount << "/" << totalCount);
    return (readyCount > 0); // At least one path is ready
}

bool
MultiPathNadaClient::IsSocketReady(Ptr<Socket> socket)
{
    if (!socket)
    {
        return false;
    }

    // Check if socket is connected
    if (socket->GetSocketType() == Socket::NS3_SOCK_DGRAM)
    {
        // For UDP sockets, check if they're bound and have available buffer space
        return socket->GetTxAvailable() > 0;
    }
    else
    {
        // For TCP sockets, check connection state and buffer availability
        return (socket->GetSocketType() != Socket::NS3_SOCK_STREAM ||
                socket->GetErrno() == Socket::ERROR_NOTERROR) &&
               socket->GetTxAvailable() > 0;
    }
}

bool
MultiPathNadaClient::IsValidNadaHeader(Ptr<Packet> packet) const
{
    if (!packet || packet->GetSize() < NadaHeader::GetStaticSize())
    {
        NS_LOG_WARN("Packet too small to contain NADA header: " << (packet ? packet->GetSize() : 0)
                                                                << " bytes, required: "
                                                                << NadaHeader::GetStaticSize());
        return false;
    }

    Ptr<Packet> packetCopy = packet->Copy();
    try
    {
        // Try to peek the header without removing it
        NadaHeader header;
        packetCopy->PeekHeader(header);

        Time timestamp = header.GetTimestamp();
        if (timestamp == Time::Min() || timestamp > Simulator::Now() + Seconds(1))
        {
            NS_LOG_WARN("Invalid timestamp in NADA header");
            return false;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception in header validation: " << e.what());
        return false;
    }
    catch (...)
    {
        NS_LOG_ERROR("Unknown exception in header validation");
        return false;
    }
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
