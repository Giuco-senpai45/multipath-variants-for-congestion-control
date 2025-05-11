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
                          MakeUintegerChecker<uint32_t>(0, 4));
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
      m_lastUpdateTime(Seconds(0))
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
    if (it == m_paths.end())
    {
        NS_LOG_ERROR("Path " << pathId << " not found in InitializePathSocket");
        return;
    }

    if (!it->second.client)
    {
        NS_LOG_ERROR("Path " << pathId << " has null client");
        return;
    }

    // More aggressive socket creation and initialization
    NS_LOG_INFO("Initializing socket for path " << pathId);

    // Get the node and create a UDP socket
    Ptr<Node> node = GetNode();
    Ptr<Socket> socket = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());

    // Set receive buffer size
    socket->SetAttribute("RcvBufSize", UintegerValue(1000000));

    // Bind to the local address - use any port if not specified
    if (InetSocketAddress::IsMatchingType(it->second.localAddress))
    {
        InetSocketAddress localAddr = InetSocketAddress::ConvertFrom(it->second.localAddress);
        // Ensure we have a valid port
        if (localAddr.GetPort() == 0)
        {
            localAddr = InetSocketAddress(localAddr.GetIpv4(), 0); // Use any port
        }
        socket->Bind(localAddr);
    }
    else if (Inet6SocketAddress::IsMatchingType(it->second.localAddress))
    {
        Inet6SocketAddress localAddr = Inet6SocketAddress::ConvertFrom(it->second.localAddress);
        if (localAddr.GetPort() == 0)
        {
            localAddr = Inet6SocketAddress(localAddr.GetIpv6(), 0); // Use any port
        }
        socket->Bind(localAddr);
    }

    // Connect to the remote address
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

    // Set up receive callback
    socket->SetRecvCallback(MakeCallback(&MultiPathNadaClient::HandleRecv, this));

    // Store the socket in the client
    it->second.client->SetSocket(socket);

    // Map the socket to the path
    m_socketToPathId[socket] = pathId;

    // Send a test packet that won't be mistaken for a NADA packet
    Ptr<Packet> testPacket = Create<Packet>(32); // Smaller size
    try
    {
        socket->Send(testPacket);
        NS_LOG_INFO("Test packet sent on path " << pathId << " during initialization");
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Failed to send test packet: " << e.what());
    }

    // Schedule a follow-up check to validate socket
    Simulator::Schedule(MilliSeconds(100), &MultiPathNadaClient::ValidatePathSocket, this, pathId);
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

void
MultiPathNadaClient::UpdatePathDistribution(void)
{
    NS_LOG_FUNCTION(this);

    // First handle any unavailable paths
    for (auto pathIt = m_paths.begin(); pathIt != m_paths.end();)
    {
        if (!IsSocketReady(pathIt->second.client->GetSocket()))
        {
            // Mark path as temporarily unavailable
            pathIt->second.weight *= 0.5; // Reduce weight of problematic paths
        }
        ++pathIt;
    }

    // Update weights based on current strategy
    std::vector<uint32_t> readyPaths;
    for (const auto& pathPair : m_paths)
    {
        if (IsSocketReady(pathPair.second.client->GetSocket()))
        {
            readyPaths.push_back(pathPair.first);
        }
    }

    // If we have ready paths, update weights based on the strategy
    if (!readyPaths.empty())
    {
        switch (m_pathSelectionStrategy)
        {
            case EQUAL:
            {
                // Set equal weights for all ready paths
                double equalWeight = 1.0 / readyPaths.size();
                NS_LOG_INFO("EQUAL strategy: Setting weight " << equalWeight << " for all paths");

                for (uint32_t pathId : readyPaths)
                {
                    m_paths[pathId].weight = equalWeight;
                }
                break;
            }

            case BEST_PATH:
            {
                // GetBestPath already updates the weights, just call it to update
                // but use a separate random draw to avoid affecting the actual path selection
                NS_LOG_INFO("BEST_PATH strategy: Updating path weights based on metrics");
                GetBestPath(readyPaths);
                break;
            }

            case WEIGHTED:
                // Weights are already set based on configuration
                NS_LOG_INFO("WEIGHTED strategy: Using configured weights");
                break;

            case REDUNDANT:
                // For redundant, normalize weights to sum to 1 for statistics
                {
                    double totalWeight = 0.0;
                    for (uint32_t pathId : readyPaths)
                    {
                        totalWeight += m_paths[pathId].weight;
                    }

                    if (totalWeight > 0)
                    {
                        for (uint32_t pathId : readyPaths)
                        {
                            m_paths[pathId].weight /= totalWeight;
                        }
                    }
                    else
                    {
                        // If total weight is 0, use equal weights
                        double equalWeight = 1.0 / readyPaths.size();
                        for (uint32_t pathId : readyPaths)
                        {
                            m_paths[pathId].weight = equalWeight;
                        }
                    }
                    NS_LOG_INFO("REDUNDANT strategy: Normalized weights for statistics");
                }
                break;

            case FRAME_AWARE:
                // Frame-aware strategy depends on the current frame type
                // Let's use GetFrameAwarePath to update the weights for the current frame type
                NS_LOG_INFO("FRAME_AWARE strategy: Updating weights based on current frame type");
                if (m_isKeyFrame)
                {
                    // For key frames, prioritize the path with lowest RTT
                    uint32_t bestPathId = 0;
                    Time lowestRtt = Time::Max();

                    for (uint32_t pathId : readyPaths)
                    {
                        if (m_paths[pathId].lastRtt < lowestRtt)
                        {
                            lowestRtt = m_paths[pathId].lastRtt;
                            bestPathId = pathId;
                        }
                    }

                    // Give higher weight to the best path
                    if (bestPathId > 0)
                    {
                        for (uint32_t pathId : readyPaths)
                        {
                            m_paths[pathId].weight = (pathId == bestPathId) ? 0.8 : 0.2 / (readyPaths.size() - 1);
                        }
                    }
                }
                else
                {
                    // For delta frames, use weighted distribution
                    // Just normalize existing weights
                    double totalWeight = 0.0;
                    for (uint32_t pathId : readyPaths)
                    {
                        totalWeight += m_paths[pathId].weight;
                    }

                    if (totalWeight > 0)
                    {
                        for (uint32_t pathId : readyPaths)
                        {
                            m_paths[pathId].weight /= totalWeight;
                        }
                    }
                }
                break;

            default:
                // Leave weights as they are
                NS_LOG_WARN("Unknown strategy " << m_pathSelectionStrategy << ", using existing weights");
                break;
        }
    }

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

void
MultiPathNadaClient::SetPathSelectionStrategy(uint32_t strategy)
{
    NS_LOG_FUNCTION(this << strategy);

    if (strategy > FRAME_AWARE)
    {
        NS_LOG_WARN("Invalid strategy value " << strategy << ", using WEIGHTED (0)");
        m_pathSelectionStrategy = WEIGHTED;
    }
    else
    {
        m_pathSelectionStrategy = strategy;
        NS_LOG_INFO("Path selection strategy changed to " << GetStrategyName(m_pathSelectionStrategy));
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
        default:
            return "UNKNOWN";
    }
}

bool
MultiPathNadaClient::SendPacketOnPath(uint32_t pathId, Ptr<Packet> packet)
{
    if (!packet)
    {
        std::cout << "ERROR: Cannot send null packet" << std::endl;
        return false;
    }

    // Find the path info
    auto it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        std::cout << "ERROR: Path " << pathId << " not found" << std::endl;
        return false;
    }

    if (!it->second.client)
    {
        std::cout << "ERROR: Client is null for path " << pathId << std::endl;
        return false;
    }

    // Get the socket
    Ptr<Socket> socket = nullptr;
    try
    {
        socket = it->second.client->GetSocket();
    }
    catch (const std::exception& e)
    {
        std::cout << "EXCEPTION getting socket: " << e.what() << std::endl;
        return false;
    }

    if (!socket)
    {
        std::cout << "ERROR: Socket is null for path " << pathId << std::endl;
        return false;
    }

    // Check socket validity
    Socket::SocketErrno error = Socket::ERROR_NOTERROR;
    try
    {
        error = socket->GetErrno();

        if (error != Socket::ERROR_NOTERROR)
        {
            std::cout << "ERROR: Socket has error " << error << std::endl;
            return false;
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "EXCEPTION checking socket error: " << e.what() << std::endl;
        return false;
    }

    // Try to send the packet
    try
    {
        Ptr<Packet> packetToSend = Create<Packet>(500);
        if (!packetToSend)
        {
            std::cout << "ERROR: Failed to create send packet" << std::endl;
            return false;
        }
        int sent = socket->Send(packetToSend);

        if (sent > 0)
        {
            // Update statistics
            it->second.packetsSent++;
            m_totalPacketsSent++;
            return true;
        }
        else
        {
            std::cout << "ERROR: Send failed, error: " << socket->GetErrno() << std::endl;
            return false;
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "EXCEPTION in socket send: " << e.what() << std::endl;
        return false;
    }
    catch (...)
    {
        std::cout << "UNKNOWN EXCEPTION in socket send" << std::endl;
        return false;
    }
}

void
MultiPathNadaClient::SendPackets(void)
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
        NS_LOG_INFO("Max packets reached (" << m_totalPacketsSent << "/" << m_maxPackets << ")");
        return;
    }

    // Get available paths with valid sockets - use safer validation
    std::vector<uint32_t> readyPaths;

    try
    {
        for (auto& pathPair : m_paths)
        {
            // More robust path validation
            if (pathPair.second.client != nullptr)
            {
                Ptr<Socket> socket = nullptr;

                // Safe socket retrieval with try/catch
                try
                {
                    socket = pathPair.second.client->GetSocket();

                    if (socket != nullptr && socket->GetErrno() == Socket::ERROR_NOTERROR)
                    {
                        readyPaths.push_back(pathPair.first);
                        NS_LOG_INFO("Path " << pathPair.first << " is ready for sending");
                    }
                    else
                    {
                        NS_LOG_WARN("Path " << pathPair.first << " has invalid socket, skipping");
                    }
                }
                catch (const std::exception& e)
                {
                    NS_LOG_ERROR("Exception checking socket for path " << pathPair.first << ": "
                                                                       << e.what());
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception gathering ready paths: " << e.what());
    }

    // Safety check - if no paths are ready, reschedule and return
    if (readyPaths.empty())
    {
        NS_LOG_WARN("No paths with valid sockets, retrying in 500ms");
        if (m_sendEvent.IsPending())
        {
            Simulator::Cancel(m_sendEvent);
        }
        m_sendEvent =
            Simulator::Schedule(MilliSeconds(500), &MultiPathNadaClient::SendPackets, this);
        return;
    }

    // Create packet with defensive error handling
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    if (!packet)
    {
        NS_LOG_ERROR("Failed to create packet");
        m_sendEvent =
            Simulator::Schedule(MilliSeconds(100), &MultiPathNadaClient::SendPackets, this);
        return;
    }

    // Select path based on strategy with robust error handling
    uint32_t selectedPathId = 0;

    try
    {
        switch (m_pathSelectionStrategy)
        {
        case BEST_PATH: {
            // Find path with lowest RTT as a simple metric
            Time lowestRtt = Time::Max();

            for (uint32_t pathId : readyPaths)
            {
                auto it = m_paths.find(pathId);
                if (it != m_paths.end() && it->second.lastRtt < lowestRtt)
                {
                    lowestRtt = it->second.lastRtt;
                    selectedPathId = pathId;
                }
            }

            // If we couldn't find a path with valid RTT data, default to first path
            if (selectedPathId == 0 && !readyPaths.empty())
            {
                selectedPathId = readyPaths[0];
                NS_LOG_WARN("Using first available path as fallback in best path strategy");
            }
            break;
        }

        case WEIGHTED:
            selectedPathId = GetWeightedPath(readyPaths);
            break;

        case EQUAL: {
            // Simple round-robin implementation with static counter
            static uint32_t roundRobin = 0;
            if (!readyPaths.empty())
            {
                selectedPathId = readyPaths[roundRobin % readyPaths.size()];
                roundRobin++;
            }
            break;
        }

        case REDUNDANT: {
            // Send on all paths and then schedule next transmission
            bool anySuccess = false;

            try
            {
                anySuccess = SendRedundantlyPath(readyPaths, packet);
            }
            catch (const std::exception& e)
            {
                NS_LOG_ERROR("Exception in redundant send: " << e.what());
            }

            // Schedule next transmission regardless of success
            Time nextTime = anySuccess ? Seconds((m_packetSize * 8.0) / m_totalRate.GetBitRate())
                                       : MilliSeconds(50);

            if (m_sendEvent.IsPending())
            {
                Simulator::Cancel(m_sendEvent);
            }

            m_sendEvent = Simulator::Schedule(nextTime, &MultiPathNadaClient::SendPackets, this);
            return; // We're done with redundant case, important to return here
        }

        case FRAME_AWARE:
            try
            {
                selectedPathId = GetFrameAwarePath(readyPaths, m_isKeyFrame);
            }
            catch (const std::exception& e)
            {
                NS_LOG_ERROR("Exception in frame-aware path selection: " << e.what());
                if (!readyPaths.empty())
                {
                    selectedPathId = readyPaths[0]; // Fallback
                }
            }
            break;

        default:
            // Default to first path for unknown strategies
            NS_LOG_WARN("Unknown path selection strategy: " << m_pathSelectionStrategy);
            if (!readyPaths.empty())
            {
                selectedPathId = readyPaths[0];
            }
            break;
        }
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception in path selection: " << e.what() << ", using fallback");
        // Use first path as fallback
        if (!readyPaths.empty())
        {
            selectedPathId = readyPaths[0];
        }
    }

    // Final validation - ensure the path exists and we actually selected one
    if (selectedPathId == 0 || m_paths.find(selectedPathId) == m_paths.end())
    {
        NS_LOG_WARN("Path selection failed, trying first available path");

        // Try to use the first path as a fallback
        if (!readyPaths.empty())
        {
            selectedPathId = readyPaths[0];
        }
        else
        {
            // No valid path found, reschedule and try again
            NS_LOG_ERROR("No valid path available for sending");
            if (m_sendEvent.IsPending())
            {
                Simulator::Cancel(m_sendEvent);
            }
            m_sendEvent =
                Simulator::Schedule(MilliSeconds(100), &MultiPathNadaClient::SendPackets, this);
            return;
        }
    }

    // Send packet on the selected path
    bool sent = false;

    try
    {
        sent = SendPacketOnPath(selectedPathId, packet);
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception sending packet on path " << selectedPathId << ": " << e.what());
    }

    if (sent)
    {
        NS_LOG_INFO("Sent packet on path " << selectedPathId);

        // Schedule next packet with appropriate pacing
        double interval = (m_packetSize * 8.0) / m_totalRate.GetBitRate();
        Time nextSendTime = Seconds(std::max(0.001, interval)); // At least 1ms

        if (m_sendEvent.IsPending())
        {
            Simulator::Cancel(m_sendEvent);
        }
        m_sendEvent = Simulator::Schedule(nextSendTime, &MultiPathNadaClient::SendPackets, this);
    }
    else
    {
        NS_LOG_WARN("Failed to send packet on path " << selectedPathId);
        // Retry sooner if send failed
        if (m_sendEvent.IsPending())
        {
            Simulator::Cancel(m_sendEvent);
        }
        m_sendEvent =
            Simulator::Schedule(MilliSeconds(50), &MultiPathNadaClient::SendPackets, this);
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
    bool allReady = true;
    bool anyReady = false;
    uint32_t readyCount = 0;
    uint32_t totalCount = 0;

    for (auto& pathPair : m_paths)
    {
        uint32_t pathId = pathPair.first;
        totalCount++;

        Ptr<Socket> socket = nullptr;

        try
        {
            if (pathPair.second.client != nullptr)
            {
                socket = pathPair.second.client->GetSocket();
                bool ready = IsSocketReady(socket);

                if (ready)
                {
                    anyReady = true;
                    readyCount++;
                    NS_LOG_INFO("Path " << pathId << " socket ready");

                    // Initialize NADA controller with the socket
                    if (pathPair.second.nada)
                    {
                        try
                        {
                            pathPair.second.nada->Init(socket);
                            NS_LOG_INFO("NADA controller initialized for path " << pathId);
                        }
                        catch (const std::exception& e)
                        {
                            NS_LOG_ERROR("Failed to initialize NADA for path " << pathId << ": "
                                                                               << e.what());
                        }
                    }
                }
                else
                {
                    allReady = false;
                    NS_LOG_WARN("Path " << pathId << " socket not ready (errno="
                                        << (socket ? socket->GetErrno() : -1) << ")");

                    // Try to reinitialize the socket
                    Simulator::Schedule(MilliSeconds(100),
                                        &MultiPathNadaClient::InitializePathSocket,
                                        this,
                                        pathId);
                }
            }
            else
            {
                allReady = false;
                NS_LOG_WARN("Path " << pathId << " client is null");
            }
        }
        catch (const std::exception& e)
        {
            allReady = false;
            NS_LOG_ERROR("Exception checking socket for path " << pathId << ": " << e.what());
        }
    }

    // Report validation results
    if (!anyReady && totalCount > 0)
    {
        NS_LOG_WARN("No sockets are ready (" << readyCount << "/" << totalCount
                                             << "), scheduling validation retry in 1s");
        Simulator::Schedule(Seconds(1.0), &MultiPathNadaClient::ValidateAllSockets, this);
        return;
    }

    if (!allReady)
    {
        NS_LOG_INFO("Some paths are not ready (" << readyCount << "/" << totalCount
                                                 << "), but we can proceed with those that are");
    }
    else
    {
        NS_LOG_INFO("All paths (" << totalCount << ") validated successfully");
    }

    // Schedule a status report to get more details about sockets
    Simulator::Schedule(MilliSeconds(500), &MultiPathNadaClient::ReportSocketStatus, this);

    NS_LOG_INFO("Socket validation complete, ready to start sending");
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
MultiPathNadaClient::IsSocketReady(Ptr<Socket> socket) const
{
    if (!socket)
    {
        return false;
    }
    try
    {
        Socket::SocketErrno error = socket->GetErrno();

        return (error == Socket::ERROR_NOTERROR);
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception checking socket: " << e.what());
        return false;
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
