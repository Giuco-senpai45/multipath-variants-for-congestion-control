#ifndef MP_BUFFER_NADA_H
#define MP_BUFFER_NADA_H

#include "mp-nada-base.h"

namespace ns3
{

class MultiPathNadaBufferAwareClient : public MultiPathNadaClientBase
{
public:
    static TypeId GetTypeId(void);

    MultiPathNadaBufferAwareClient();
    virtual ~MultiPathNadaBufferAwareClient();

    virtual bool Send(Ptr<Packet> packet) override;
    virtual std::string GetStrategyName() const override { return "BUFFER_AWARE"; }
    virtual void UpdateWeights() override;

    void SetBufferAwareParameters(double targetBufferLength, double bufferWeightFactor);

private:
    double m_targetBufferLength;
    double m_bufferWeightFactor;

    double CalculateBufferWeight(double currentBufferMs, uint32_t pathId);
    uint32_t GetBufferAwarePath(const std::vector<uint32_t>& readyPaths);
    uint32_t GetBestPathByRTT();
};

} // namespace ns3

#endif /* MP_BUFFER_NADA_H */
