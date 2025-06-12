#ifndef MP_BEST_PATH_NADA_H
#define MP_BEST_PATH_NADA_H

#include "mp-nada-base.h"

namespace ns3
{

class MultiPathNadaBestPathClient : public MultiPathNadaClientBase
{
public:
    static TypeId GetTypeId(void);

    MultiPathNadaBestPathClient();
    virtual ~MultiPathNadaBestPathClient();

    virtual bool Send(Ptr<Packet> packet) override;
    virtual std::string GetStrategyName() const override { return "BEST_PATH"; }
    virtual void UpdateWeights() override;

private:
    uint32_t m_bestPathId;
    uint32_t m_bestPathReCheckCounter;
    static const uint32_t RECHECK_INTERVAL = 50; // Check best path every 50 packets

    uint32_t FindBestPath();
};

} // namespace ns3

#endif /* MP_BEST_PATH_NADA_H */
