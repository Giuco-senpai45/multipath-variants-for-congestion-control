#include "mp-frame.h"
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MultiPathNadaFrameAwareClient");
NS_OBJECT_ENSURE_REGISTERED(MultiPathNadaFrameAwareClient);

TypeId
MultiPathNadaFrameAwareClient::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::MultiPathNadaFrameAwareClient")
                           .SetParent<MultiPathNadaClientBase>()
                           .SetGroupName("Applications")
                           .AddConstructor<MultiPathNadaFrameAwareClient>();
    return tid;
}

MultiPathNadaFrameAwareClient::MultiPathNadaFrameAwareClient()
{
    NS_LOG_FUNCTION(this);
}

MultiPathNadaFrameAwareClient::~MultiPathNadaFrameAwareClient()
{
    NS_LOG_FUNCTION(this);
}

void
MultiPathNadaFrameAwareClient::UpdateWeights()
{
    NS_LOG_FUNCTION(this);


    // For frame-aware strategy, we maintain two sets of weights:
    // 1. Reliability weights (for key frames) - favor low RTT and high reliability
    // 2. Throughput weights (for delta frames) - favor high bandwidth utilization

    std::map<uint32_t, double> reliabilityWeights;
    std::map<uint32_t, double> throughputWeights;

    double totalReliabilityScore = 0.0;
    double totalThroughputScore = 0.0;

    for (auto& pathPair : m_paths)
    {
        uint32_t pathId = pathPair.first;
        PathInfo& pathInfo = pathPair.second;

        // Calculate reliability score (prioritizes RTT and packet success rate)
        double pathUtilization = 0.0;
        if (pathInfo.packetsSent > 0)
        {
            pathUtilization = static_cast<double>(pathInfo.packetsAcked) / pathInfo.packetsSent;
        }

        double rttMs = pathInfo.lastRtt.GetMilliSeconds();
        double rateMbps = pathInfo.currentRate.GetBitRate() / 1000000.0;

        // Reliability score: heavily weighted towards low RTT and high success rate
        double rttScore = 100.0 / (rttMs + 10.0); // Lower RTT = higher score
        double reliabilityScore = (rttScore * 0.6 + pathUtilization * 0.4);

        // Throughput score: weighted towards high bandwidth
        double bandwidthScore = rateMbps / 10.0; // Normalize by 10Mbps
        double throughputScore = (bandwidthScore * 0.7 + pathUtilization * 0.3);

        reliabilityWeights[pathId] = reliabilityScore;
        throughputWeights[pathId] = throughputScore;

        totalReliabilityScore += reliabilityScore;
        totalThroughputScore += throughputScore;

        NS_LOG_INFO("FRAME_AWARE - Path " << pathId
                   << " RTT=" << rttMs << "ms"
                   << ", Rate=" << rateMbps << "Mbps"
                   << ", Util=" << pathUtilization
                   << ", ReliabilityScore=" << reliabilityScore
                   << ", ThroughputScore=" << throughputScore);
    }

    // Set primary weights based on balanced approach (will be overridden per frame type)
    if (totalReliabilityScore > 0 && totalThroughputScore > 0)
    {
        for (auto& pathPair : m_paths)
        {
            uint32_t pathId = pathPair.first;

            // Use a balanced weight as default (50% reliability, 50% throughput)
            double balancedWeight = 0.5 * (reliabilityWeights[pathId] / totalReliabilityScore) +
                                   0.5 * (throughputWeights[pathId] / totalThroughputScore);

            pathPair.second.weight = balancedWeight;

            NS_LOG_INFO("FRAME_AWARE - Path " << pathId
                       << " balanced weight: " << balancedWeight);
        }
    }
    else
    {
        // Fallback to equal weights
        double equalWeight = 1.0 / m_paths.size();
        for (auto& pathPair : m_paths)
        {
            pathPair.second.weight = equalWeight;
        }
    }
}

bool
MultiPathNadaFrameAwareClient::Send(Ptr<Packet> packet)
{
    if (!m_running || m_totalPacketsSent >= m_maxPackets)
    {
        return false;
    }

    // Get ready paths
    std::vector<uint32_t> readyPaths;
    for (const auto& pathPair : m_paths)
    {
        if (pathPair.second.client && pathPair.second.client->GetSocket() &&
            IsSocketReady(pathPair.second.client->GetSocket()))
        {
            readyPaths.push_back(pathPair.first);
        }
    }

    if (readyPaths.empty())
    {
        return false;
    }

    uint32_t selectedPath = GetFrameAwarePath(readyPaths, m_isKeyFrame);

    if (!packet)
    {
        packet = Create<Packet>(m_packetSize);
    }

    bool sent = SendPacketOnPath(selectedPath, packet);
    if (sent)
    {
        m_totalPacketsSent++;

        NS_LOG_DEBUG("FRAME_AWARE - Sent " << (m_isKeyFrame ? "KEY" : "DELTA")
                    << " frame packet on path " << selectedPath);
    }

    return sent;
}

uint32_t
MultiPathNadaFrameAwareClient::GetFrameAwarePath(const std::vector<uint32_t>& readyPaths, bool isKeyFrame)
{
    NS_LOG_FUNCTION(this << isKeyFrame);

    if (readyPaths.empty())
    {
        NS_LOG_ERROR("GetFrameAwarePath called with empty path list");
        return 0;
    }

    if (readyPaths.size() == 1)
    {
        return readyPaths[0];
    }

    if (isKeyFrame)
    {
        NS_LOG_INFO("FRAME_AWARE - Key frame: selecting most reliable path");

        uint32_t bestPathId = readyPaths[0];
        double bestReliabilityScore = 0.0;

        for (auto pathId : readyPaths)
        {
            auto it = m_paths.find(pathId);
            if (it != m_paths.end())
            {
                double pathUtilization = 0.0;
                if (it->second.packetsSent > 0)
                {
                    pathUtilization = static_cast<double>(it->second.packetsAcked) / it->second.packetsSent;
                }

                double rttMs = it->second.lastRtt.GetMilliSeconds();

                // Reliability score: heavily favor low RTT and high success rate
                double rttScore = 100.0 / (rttMs + 10.0);
                double reliabilityScore = (rttScore * 0.7 + pathUtilization * 0.3);

                if (reliabilityScore > bestReliabilityScore)
                {
                    bestReliabilityScore = reliabilityScore;
                    bestPathId = pathId;
                }
            }
        }

        NS_LOG_INFO("FRAME_AWARE - Selected path " << bestPathId
                   << " for key frame (reliability score: " << bestReliabilityScore << ")");
        return bestPathId;
    }
    else
    {
        NS_LOG_DEBUG("FRAME_AWARE - Delta frame: using weighted distribution");
        return GetWeightedPath(readyPaths);
    }
}

uint32_t
MultiPathNadaFrameAwareClient::GetBestPathByRTT()
{
    if (m_paths.empty())
    {
        return 0;
    }

    uint32_t bestPath = m_paths.begin()->first;
    Time lowestRtt = Time::Max();

    for (auto& pathPair : m_paths)
    {
        if (pathPair.second.lastRtt < lowestRtt)
        {
            lowestRtt = pathPair.second.lastRtt;
            bestPath = pathPair.first;
        }
    }

    return bestPath;
}

uint32_t
MultiPathNadaFrameAwareClient::GetWeightedPath(const std::vector<uint32_t>& readyPaths)
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
