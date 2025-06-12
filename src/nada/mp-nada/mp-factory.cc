#include "mp-factory.h"
#include "mp-buffer.h"
#include "mp-frame.h"
#include "mp-best.h"
#include "mp-rr.h"
#include "mp-weighted.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MultiPathNadaClientFactory");

Ptr<MultiPathNadaClientBase>
MultiPathNadaClientFactory::Create(StrategyType strategy)
{
    NS_LOG_FUNCTION("Creating client with strategy: " << GetStrategyName(strategy));

    switch (strategy)
    {
        case WEIGHTED:
            NS_LOG_INFO("Creating WEIGHTED strategy client");
            return CreateObject<MultiPathNadaWeightedClient>();

        case BEST_PATH:
            NS_LOG_INFO("Creating BEST_PATH strategy client");
            return CreateObject<MultiPathNadaBestPathClient>();

        case BUFFER_AWARE:
            NS_LOG_INFO("Creating BUFFER_AWARE strategy client");
            return CreateObject<MultiPathNadaBufferAwareClient>();

        case EQUAL:
            NS_LOG_INFO("Creating ROUND_ROBIN strategy client");
            return CreateObject<MultiPathNadaRoundRobinClient>();

        case FRAME_AWARE:
            NS_LOG_INFO("Creating FRAME_AWARE strategy client");
            return CreateObject<MultiPathNadaFrameAwareClient>();

        case REDUNDANT:
        default:
            NS_LOG_WARN("Unknown strategy " << strategy << ", using WEIGHTED as default");
            return CreateObject<MultiPathNadaWeightedClient>();
    }
}

std::string
MultiPathNadaClientFactory::GetStrategyName(StrategyType strategy)
{
    switch (strategy)
    {
        case WEIGHTED: return "WEIGHTED";
        case BEST_PATH: return "BEST_PATH";
        case EQUAL: return "ROUND_ROBIN";
        case REDUNDANT: return "REDUNDANT";
        case FRAME_AWARE: return "FRAME_AWARE";
        case BUFFER_AWARE: return "BUFFER_AWARE";
        default: return "UNKNOWN";
    }
}

} // namespace ns3
