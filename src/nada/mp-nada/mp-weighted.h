#ifndef MP_WEIGHTED_NADA_H
#define MP_WEIGHTED_NADA_H

#include "mp-nada-base.h"

namespace ns3
{

class MultiPathNadaWeightedClient : public MultiPathNadaClientBase
{
public:
    static TypeId GetTypeId(void);

    MultiPathNadaWeightedClient();
    virtual ~MultiPathNadaWeightedClient();

    virtual bool Send(Ptr<Packet> packet) override;
    virtual std::string GetStrategyName() const override { return "WEIGHTED"; }
    virtual void UpdateWeights() override;

private:
    // Pre-calculated path selection for performance
    std::vector<uint32_t> m_pathOrder;

    void RecalculatePathOrder();
    void RecoverPath(uint32_t pathId);
    uint32_t GetWeightedPath(const std::vector<uint32_t>& readyPaths);
};

} // namespace ns3

#endif /* MP_WEIGHTED_NADA_H */
