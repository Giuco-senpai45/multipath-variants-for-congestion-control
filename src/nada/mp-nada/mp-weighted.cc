#include "mp-weighted.h"
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MultiPathNadaWeightedClient");
NS_OBJECT_ENSURE_REGISTERED(MultiPathNadaWeightedClient);

TypeId
MultiPathNadaWeightedClient::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::MultiPathNadaWeightedClient")
                           .SetParent<MultiPathNadaClientBase>()
                           .SetGroupName("Applications")
                           .AddConstructor<MultiPathNadaWeightedClient>();
    return tid;
}

MultiPathNadaWeightedClient::MultiPathNadaWeightedClient()
{
    NS_LOG_FUNCTION(this);
}

MultiPathNadaWeightedClient::~MultiPathNadaWeightedClient()
{
    NS_LOG_FUNCTION(this);
}

void
MultiPathNadaWeightedClient::UpdateWeights()
{
    NS_LOG_FUNCTION(this);

    // **WEIGHTED STRATEGY: Dynamic weight adjustment based on quality metrics**
    std::map<uint32_t, double> pathMetrics;
    double totalQuality = 0.0;

    // Calculate quality metrics for each path
    for (auto& pathPair : m_paths)
    {
        uint32_t pathId = pathPair.first;
        PathInfo& pathInfo = pathPair.second;

        // Calculate path utilization
        double pathUtilization = 0.0;
        if (pathInfo.packetsSent > 0)
        {
            pathUtilization = static_cast<double>(pathInfo.packetsAcked) / pathInfo.packetsSent;
        }

        // Calculate comprehensive quality score
        double rttScore = 1.0 / (1.0 + pathInfo.lastRtt.GetMilliSeconds() / 100.0);
        double rateScore = pathInfo.currentRate.GetBitRate() / 10000000.0; // Normalize by 10Mbps
        double utilizationScore = pathUtilization;

        // Combined quality metric (weighted combination)
        double qualityScore = (rttScore * 0.3 + rateScore * 0.4 + utilizationScore * 0.3);
        pathMetrics[pathId] = qualityScore;
        totalQuality += qualityScore;

        NS_LOG_INFO("WEIGHTED - Path " << pathId << " quality: " << qualityScore
                   << " (RTT=" << pathInfo.lastRtt.GetMilliSeconds() << "ms"
                   << ", Rate=" << pathInfo.currentRate.GetBitRate()/1000000.0 << "Mbps"
                   << ", Util=" << pathUtilization << ")");
    }

    // Update weights based on quality metrics
    if (totalQuality > 0)
    {
        for (auto& pathPair : m_paths)
        {
            uint32_t pathId = pathPair.first;
            double normalizedWeight = pathMetrics[pathId] / totalQuality;

            // Smooth weight changes to avoid oscillation (70% old + 30% new)
            double currentWeight = pathPair.second.weight;
            double newWeight = 0.7 * currentWeight + 0.3 * normalizedWeight;
            pathPair.second.weight = newWeight;

            NS_LOG_INFO("WEIGHTED - Path " << pathId << " weight updated: "
                       << currentWeight << " -> " << newWeight);
        }
    }

    // Recalculate path order for performance optimization
    RecalculatePathOrder();
}

bool
MultiPathNadaWeightedClient::Send(Ptr<Packet> packet)
{
    if (!m_running || m_totalPacketsSent >= m_maxPackets)
    {
        return false;
    }

    // **SIMPLIFIED: Use same logic as AggregatePathNadaClient**
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
        NS_LOG_WARN("WEIGHTED - No available paths");
        return false;
    }

    // **WEIGHTED: Use weighted path selection (but simplified)**
    uint32_t selectedPath = GetWeightedPath(availablePaths);

    if (!packet)
    {
        packet = Create<Packet>(m_packetSize);
    }

    bool sent = SendPacketOnPath(selectedPath, packet);
    if (sent)
    {
        m_totalPacketsSent++;
    }

    return sent;
}

void
MultiPathNadaWeightedClient::RecoverPath(uint32_t pathId)
{
    NS_LOG_FUNCTION(this << pathId);

    auto it = m_paths.find(pathId);
    if (it == m_paths.end())
    {
        return;
    }

    NS_LOG_INFO("WEIGHTED - Attempting to recover path " << pathId);

    // Check if path needs reinitialization
    if (!it->second.client)
    {
        NS_LOG_WARN("WEIGHTED - Path " << pathId << " has null client");
        return;
    }

    Ptr<Socket> socket = it->second.client->GetSocket();
    if (!socket)
    {
        NS_LOG_INFO("WEIGHTED - Reinitializing socket for path " << pathId);
        InitializePathSocket(pathId);
        return;
    }

    // Test socket with a simple operation
    try
    {
        Address peerAddr;
        if (socket->GetPeerName(peerAddr) != 0)
        {
            NS_LOG_INFO("WEIGHTED - Socket disconnected for path " << pathId << ", reinitializing");
            InitializePathSocket(pathId);
        }
        else
        {
            NS_LOG_DEBUG("WEIGHTED - Path " << pathId << " socket appears healthy");
        }
    }
    catch (const std::exception& e)
    {
        NS_LOG_WARN("WEIGHTED - Exception checking path " << pathId << ": " << e.what());
        InitializePathSocket(pathId);
    }
}

void
MultiPathNadaWeightedClient::RecalculatePathOrder()
{
    m_pathOrder.clear();

    if (m_paths.empty())
    {
        return;
    }

    // **PERFORMANCE: Pre-calculate weighted round-robin sequence**
    const uint32_t SEQUENCE_LENGTH = 100; // Process 100 packets per sequence

    // Calculate total weight
    double totalWeight = 0.0;
    for (const auto& pathPair : m_paths)
    {
        totalWeight += pathPair.second.weight;
    }

    if (totalWeight <= 0.0)
    {
        // Equal distribution fallback
        for (const auto& pathPair : m_paths)
        {
            for (uint32_t i = 0; i < SEQUENCE_LENGTH / m_paths.size(); i++)
            {
                m_pathOrder.push_back(pathPair.first);
            }
        }
        return;
    }

    // Create weighted sequence
    for (const auto& pathPair : m_paths)
    {
        uint32_t pathPackets = static_cast<uint32_t>(
            (pathPair.second.weight / totalWeight) * SEQUENCE_LENGTH);
        pathPackets = std::max(1U, pathPackets); // At least 1 packet per path

        for (uint32_t i = 0; i < pathPackets; i++)
        {
            m_pathOrder.push_back(pathPair.first);
        }
    }

    // Shuffle for better distribution
    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    for (uint32_t i = m_pathOrder.size() - 1; i > 0; i--)
    {
        uint32_t j = rng->GetInteger(0, i);
        std::swap(m_pathOrder[i], m_pathOrder[j]);
    }

    NS_LOG_DEBUG("WEIGHTED - Recalculated path order with " << m_pathOrder.size() << " entries");
}

uint32_t
MultiPathNadaWeightedClient::GetWeightedPath(const std::vector<uint32_t>& readyPaths)
{
    if (readyPaths.empty())
    {
        return 0;
    }

    // Calculate total weight of ready paths
    double totalWeight = 0.0;
    std::map<uint32_t, double> normalizedWeights;

    for (auto pathId : readyPaths)
    {
        auto it = m_paths.find(pathId);
        if (it != m_paths.end())
        {
            totalWeight += it->second.weight;
            normalizedWeights[pathId] = it->second.weight;
        }
    }

    if (totalWeight <= 0.0)
    {
        // Use equal weights
        for (auto pathId : readyPaths)
        {
            normalizedWeights[pathId] = 1.0 / readyPaths.size();
        }
        totalWeight = 1.0;
    }
    else
    {
        // Normalize weights
        for (auto& pair : normalizedWeights)
        {
            pair.second /= totalWeight;
        }
    }

    // Select path based on random value
    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double r = rng->GetValue(0.0, 1.0);
    double cumulativeProb = 0.0;

    for (auto pathId : readyPaths)
    {
        cumulativeProb += normalizedWeights[pathId];
        if (r <= cumulativeProb)
        {
            return pathId;
        }
    }

    return readyPaths[0]; // Fallback
}

} // namespace ns3
