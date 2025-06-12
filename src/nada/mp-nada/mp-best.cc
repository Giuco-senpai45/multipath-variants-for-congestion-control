#include "mp-best.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MultiPathNadaBestPathClient");
NS_OBJECT_ENSURE_REGISTERED(MultiPathNadaBestPathClient);

TypeId
MultiPathNadaBestPathClient::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::MultiPathNadaBestPathClient")
                           .SetParent<MultiPathNadaClientBase>()
                           .SetGroupName("Applications")
                           .AddConstructor<MultiPathNadaBestPathClient>();
    return tid;
}

MultiPathNadaBestPathClient::MultiPathNadaBestPathClient()
    : m_bestPathId(1), m_bestPathReCheckCounter(0)
{
    NS_LOG_FUNCTION(this);
}

MultiPathNadaBestPathClient::~MultiPathNadaBestPathClient()
{
    NS_LOG_FUNCTION(this);
}

void
MultiPathNadaBestPathClient::UpdateWeights()
{
    NS_LOG_FUNCTION(this);

    uint32_t bestPath = FindBestPath();

    // Calculate quality for logging
    for (auto& pathPair : m_paths)
    {
        uint32_t pathId = pathPair.first;
        PathInfo& pathInfo = pathPair.second;

        // Use combined RTT and rate metric
        double rttMs = pathInfo.lastRtt.GetMilliSeconds();
        double rateMbps = pathInfo.currentRate.GetBitRate() / 1000000.0;
        double quality = rateMbps / (rttMs + 1.0); // Avoid division by zero

        NS_LOG_INFO("BEST_PATH - Path " << pathId << " quality: " << quality
                   << " (RTT=" << rttMs << "ms, Rate=" << rateMbps << "Mbps)"
                   << (pathId == bestPath ? " [BEST]" : ""));
    }

    // Give best path 80% weight, others share remaining 20%
    for (auto& pathPair : m_paths)
    {
        if (pathPair.first == bestPath)
        {
            pathPair.second.weight = 0.8;
            NS_LOG_INFO("BEST_PATH - Path " << pathPair.first << " (best) weight: 0.8");
        }
        else
        {
            double otherWeight = 0.2 / (m_paths.size() - 1);
            pathPair.second.weight = otherWeight;
            NS_LOG_INFO("BEST_PATH - Path " << pathPair.first << " weight: " << otherWeight);
        }
    }

    m_bestPathId = bestPath;
}

bool
MultiPathNadaBestPathClient::Send(Ptr<Packet> packet)
{
    if (!m_running || m_totalPacketsSent >= m_maxPackets)
    {
        return false;
    }

    if (++m_bestPathReCheckCounter >= RECHECK_INTERVAL)
    {
        m_bestPathId = FindBestPath();
        m_bestPathReCheckCounter = 0;
        NS_LOG_DEBUG("BEST_PATH - Rechecked best path: " << m_bestPathId);
    }

    if (!packet)
    {
        packet = Create<Packet>(m_packetSize);
    }

    bool sent = SendPacketOnPath(m_bestPathId, packet);
    if (sent)
    {
        m_totalPacketsSent++;
    }

    return sent;
}

uint32_t
MultiPathNadaBestPathClient::FindBestPath()
{
    if (m_paths.empty())
    {
        return 0;
    }

    uint32_t bestPathId = m_paths.begin()->first;
    double bestMetric = 0.0;

    for (const auto& pathPair : m_paths)
    {
        uint32_t pathId = pathPair.first;
        const PathInfo& pathInfo = pathPair.second;

        // Combined metric: rate/RTT ratio for best performance
        double rttMs = pathInfo.lastRtt.GetMilliSeconds();
        double rateMbps = pathInfo.currentRate.GetBitRate() / 1000000.0;

        // Avoid division by zero
        if (rttMs < 1.0) rttMs = 1.0;

        double metric = rateMbps / rttMs;

        if (metric > bestMetric)
        {
            bestMetric = metric;
            bestPathId = pathId;
        }
    }

    return bestPathId;
}

} // namespace ns3
