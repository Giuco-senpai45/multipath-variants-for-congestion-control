#ifndef MP_FRAME_NADA_H
#define MP_FRAME_NADA_H

#include "mp-nada-base.h"

namespace ns3
{

class MultiPathNadaFrameAwareClient : public MultiPathNadaClientBase
{
public:
    static TypeId GetTypeId(void);

    MultiPathNadaFrameAwareClient();
    virtual ~MultiPathNadaFrameAwareClient();

    virtual bool Send(Ptr<Packet> packet) override;
    virtual std::string GetStrategyName() const override { return "FRAME_AWARE"; }
    virtual void UpdateWeights() override;

private:
    uint32_t GetFrameAwarePath(const std::vector<uint32_t>& readyPaths, bool isKeyFrame);
    uint32_t GetBestPathByRTT();
    uint32_t GetWeightedPath(const std::vector<uint32_t>& readyPaths);
};

} // namespace ns3

#endif /* MP_FRAME_NADA_H */
