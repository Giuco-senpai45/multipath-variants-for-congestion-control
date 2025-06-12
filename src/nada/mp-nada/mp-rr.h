#ifndef MP_RR_NADA_H
#define MP_RR_NADA_H

#include "mp-nada-base.h"

namespace ns3
{

class MultiPathNadaRoundRobinClient : public MultiPathNadaClientBase
{
public:
    static TypeId GetTypeId(void);

    MultiPathNadaRoundRobinClient();
    virtual ~MultiPathNadaRoundRobinClient();

    virtual bool Send(Ptr<Packet> packet) override;
    virtual std::string GetStrategyName() const override { return "ROUND_ROBIN"; }
    virtual void UpdateWeights() override;

private:
    uint32_t m_currentPathIndex;
    std::vector<uint32_t> m_pathOrder;

    void UpdatePathOrder();
};

} // namespace ns3

#endif /* MP_RR_NADA_H */
