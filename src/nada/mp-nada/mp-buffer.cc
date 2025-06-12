#include "mp-buffer.h"
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MultiPathNadaBufferAwareClient");
NS_OBJECT_ENSURE_REGISTERED(MultiPathNadaBufferAwareClient);

TypeId
MultiPathNadaBufferAwareClient::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::MultiPathNadaBufferAwareClient")
                           .SetParent<MultiPathNadaClientBase>()
                           .SetGroupName("Applications")
                           .AddConstructor<MultiPathNadaBufferAwareClient>();
    return tid;
}

MultiPathNadaBufferAwareClient::MultiPathNadaBufferAwareClient()
    : m_targetBufferLength(3.0), m_bufferWeightFactor(0.3)
{
    NS_LOG_FUNCTION(this);
}

MultiPathNadaBufferAwareClient::~MultiPathNadaBufferAwareClient()
{
    NS_LOG_FUNCTION(this);
}

void
MultiPathNadaBufferAwareClient::UpdateWeights()
{
    NS_LOG_FUNCTION(this);

    double currentBufferMs = 0.0;
    if (m_videoReceiver)
    {
        currentBufferMs = m_videoReceiver->GetAverageBufferLength();
    }

    double targetBufferMs = m_targetBufferLength * 1000.0;
    double bufferRatio = currentBufferMs / targetBufferMs;

    NS_LOG_INFO("BUFFER_AWARE - Current buffer: " << currentBufferMs << "ms, "
               << "Target: " << targetBufferMs << "ms, "
               << "Ratio: " << bufferRatio);

    // If buffer is critically low, prioritize fastest/most reliable paths
    if (bufferRatio < 0.5)
    {
        uint32_t bestPath = GetBestPathByRTT();
        for (auto& pathPair : m_paths)
        {
            if (pathPair.first == bestPath)
            {
                pathPair.second.weight = 0.9;
                NS_LOG_WARN("BUFFER_AWARE - Emergency mode: Path " << pathPair.first
                           << " weight: 0.9 (best path)");
            }
            else
            {
                pathPair.second.weight = 0.1 / (m_paths.size() - 1);
                NS_LOG_INFO("BUFFER_AWARE - Emergency mode: Path " << pathPair.first
                           << " weight: " << pathPair.second.weight);
            }
        }
        NS_LOG_WARN("BUFFER_AWARE - Buffer critically low (" << currentBufferMs
                   << "ms), emergency mode on path " << bestPath);
    }
    else
    {
        std::map<uint32_t, double> pathMetrics;

        for (auto& pathPair : m_paths)
        {
            uint32_t pathId = pathPair.first;
            double bufferWeight = CalculateBufferWeight(currentBufferMs, pathId);

            // Calculate path quality metric
            double rttMs = pathPair.second.lastRtt.GetMilliSeconds();
            double rateMbps = pathPair.second.currentRate.GetBitRate() / 1000000.0;
            double qualityWeight = rateMbps / (rttMs + 1.0);

            // Combine buffer urgency with path quality
            double combinedWeight = m_bufferWeightFactor * bufferWeight +
                                  (1.0 - m_bufferWeightFactor) * qualityWeight;

            pathMetrics[pathId] = combinedWeight;

            NS_LOG_INFO("BUFFER_AWARE - Path " << pathId << " weights: "
                       << "buffer=" << bufferWeight << ", "
                       << "quality=" << qualityWeight << ", "
                       << "combined=" << combinedWeight);
        }

        // Normalize weights
        double totalWeight = 0.0;
        for (auto& metric : pathMetrics)
        {
            totalWeight += metric.second;
        }

        if (totalWeight > 0)
        {
            for (auto& pathPair : m_paths)
            {
                pathPair.second.weight = pathMetrics[pathPair.first] / totalWeight;
                NS_LOG_INFO("BUFFER_AWARE - Path " << pathPair.first
                           << " final weight: " << pathPair.second.weight);
            }
        }
    }
}

bool
MultiPathNadaBufferAwareClient::Send(Ptr<Packet> packet)
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

    uint32_t selectedPath = GetBufferAwarePath(readyPaths);

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

double
MultiPathNadaBufferAwareClient::CalculateBufferWeight(double currentBufferMs, uint32_t pathId)
{
    double targetBufferMs = m_targetBufferLength * 1000.0;
    double bufferDiff = currentBufferMs - targetBufferMs;

    // Get path RTT for responsiveness calculation
    auto it = m_paths.find(pathId);
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
    }
    else if (bufferDiff < -500.0) // Buffer low (< target - 0.5s)
    {
        // Medium urgency: slightly favor faster paths
        urgencyFactor = 1.5 / (1.0 + pathRttMs / 100.0);
    }
    else if (bufferDiff > 1000.0) // Buffer high (> target + 1s)
    {
        // Low urgency: can afford to use slower paths for load balancing
        urgencyFactor = 0.8 + 0.4 * (pathRttMs / 200.0);
        urgencyFactor = std::min(urgencyFactor, 1.2);
    }

    // Combine with path quality metrics
    double rttWeight = 100.0 / (pathRttMs + 10.0); // Lower RTT = higher weight
    double finalWeight = urgencyFactor * rttWeight;

    // Normalize to reasonable range [0.1, 2.0]
    finalWeight = std::max(0.1, std::min(2.0, finalWeight));

    return finalWeight;
}

uint32_t
MultiPathNadaBufferAwareClient::GetBufferAwarePath(const std::vector<uint32_t>& readyPaths)
{
    if (readyPaths.empty())
    {
        return 0;
    }

    if (readyPaths.size() == 1)
    {
        return readyPaths[0];
    }

    // Use weighted selection based on current weights
    double totalWeight = 0.0;
    for (auto pathId : readyPaths)
    {
        auto it = m_paths.find(pathId);
        if (it != m_paths.end())
        {
            totalWeight += it->second.weight;
        }
    }

    if (totalWeight <= 0.0)
    {
        return readyPaths[0];
    }

    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double r = rng->GetValue(0.0, 1.0);
    double cumulativeProb = 0.0;

    for (auto pathId : readyPaths)
    {
        auto it = m_paths.find(pathId);
        if (it != m_paths.end())
        {
            double normalizedWeight = it->second.weight / totalWeight;
            cumulativeProb += normalizedWeight;

            if (r <= cumulativeProb)
            {
                return pathId;
            }
        }
    }

    return readyPaths[0]; // Fallback
}

uint32_t
MultiPathNadaBufferAwareClient::GetBestPathByRTT()
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

void
MultiPathNadaBufferAwareClient::SetBufferAwareParameters(double targetBufferLength,
                                                         double bufferWeightFactor)
{
    m_targetBufferLength = targetBufferLength;
    m_bufferWeightFactor = std::max(0.0, std::min(1.0, bufferWeightFactor));
}

} // namespace ns3
