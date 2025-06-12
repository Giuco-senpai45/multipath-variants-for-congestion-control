#include "mp-rr.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MultiPathNadaRoundRobinClient");
NS_OBJECT_ENSURE_REGISTERED(MultiPathNadaRoundRobinClient);

TypeId
MultiPathNadaRoundRobinClient::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::MultiPathNadaRoundRobinClient")
                           .SetParent<MultiPathNadaClientBase>()
                           .SetGroupName("Applications")
                           .AddConstructor<MultiPathNadaRoundRobinClient>();
    return tid;
}

MultiPathNadaRoundRobinClient::MultiPathNadaRoundRobinClient()
    : m_currentPathIndex(0)
{
    NS_LOG_FUNCTION(this);
}

MultiPathNadaRoundRobinClient::~MultiPathNadaRoundRobinClient()
{
    NS_LOG_FUNCTION(this);
}

void
MultiPathNadaRoundRobinClient::UpdateWeights()
{
    NS_LOG_FUNCTION(this);

    // **ROUND_ROBIN STRATEGY: Equal weights for all paths**
    if (m_paths.empty())
    {
        return;
    }

    double equalWeight = 1.0 / m_paths.size();

    for (auto& pathPair : m_paths)
    {
        pathPair.second.weight = equalWeight;

        NS_LOG_DEBUG("ROUND_ROBIN - Path " << pathPair.first
                    << " weight: " << equalWeight);
    }

    // Update path order for round-robin selection
    UpdatePathOrder();

    NS_LOG_INFO("ROUND_ROBIN - Updated " << m_paths.size()
               << " paths with equal weights (" << equalWeight << " each)");
}

bool
MultiPathNadaRoundRobinClient::Send(Ptr<Packet> packet)
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
        NS_LOG_WARN("ROUND_ROBIN - No ready paths available");
        return false;
    }

    // **ULTRA-FAST ROUND-ROBIN: Simple modulo selection**
    uint32_t selectedPath = readyPaths[m_currentPathIndex % readyPaths.size()];
    m_currentPathIndex++;

    // Prevent overflow
    if (m_currentPathIndex > 10000)
    {
        m_currentPathIndex = 0;
    }

    if (!packet)
    {
        packet = Create<Packet>(m_packetSize);
    }

    bool sent = SendPacketOnPath(selectedPath, packet);
    if (sent)
    {
        m_totalPacketsSent++;

        NS_LOG_DEBUG("ROUND_ROBIN - Sent packet on path " << selectedPath
                    << " (index: " << (m_currentPathIndex - 1) % readyPaths.size()
                    << "/" << readyPaths.size() - 1 << ")");
    }

    return sent;
}

void
MultiPathNadaRoundRobinClient::UpdatePathOrder()
{
    m_pathOrder.clear();

    // Simple ordered list of path IDs
    for (const auto& pathPair : m_paths)
    {
        m_pathOrder.push_back(pathPair.first);
    }

    // Sort for consistent ordering
    std::sort(m_pathOrder.begin(), m_pathOrder.end());

    NS_LOG_DEBUG("ROUND_ROBIN - Path order updated with " << m_pathOrder.size() << " paths");
}

} // namespace ns3
