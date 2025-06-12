#ifndef MP_FACTORY_H
#define MP_FACTORY_H

#include "mp-nada-base.h"

namespace ns3
{

class MultiPathNadaClientFactory
{
public:
    enum StrategyType
    {
        WEIGHTED = 0,
        BEST_PATH = 1,
        EQUAL = 2,
        REDUNDANT = 3,
        FRAME_AWARE = 4,
        BUFFER_AWARE = 5
    };

    static Ptr<MultiPathNadaClientBase> Create(StrategyType strategy);
    static std::string GetStrategyName(StrategyType strategy);
};

} // namespace ns3

#endif /* MP_FACTORY_H */
