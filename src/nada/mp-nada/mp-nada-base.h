#ifndef MP_NADA_BASE_H
#define MP_NADA_BASE_H

#include "ns3/application.h"
#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/nada-improved.h"
#include "ns3/nada-udp-client.h"
#include "ns3/socket.h"
#include "ns3/video-receiver.h"
#include <map>
#include <vector>

namespace ns3
{

struct PathInfo
{
    Ptr<UdpNadaClient> client;
    Ptr<NadaCongestionControl> nada;
    double weight;
    DataRate currentRate;
    uint32_t packetsSent;
    uint32_t packetsAcked;
    Time lastRtt;
    Time lastDelay;
    Address localAddress;
    Address remoteAddress;
};

class MultiPathNadaClientBase : public Application
{
public:
    static TypeId GetTypeId(void);

    MultiPathNadaClientBase();
    virtual ~MultiPathNadaClientBase();

    // Core functionality (same for all strategies)
    bool AddPath(Address localAddress,
                 Address remoteAddress,
                 uint32_t pathId,
                 double weight = 1.0,
                 DataRate initialRate = DataRate("1Mbps"));
    bool RemovePath(uint32_t pathId);
    void SetPacketSize(uint32_t size);
    void SetMaxPackets(uint32_t numPackets);
    void SetVideoMode(bool enable);
    void SetKeyFrameStatus(bool isKeyFrame);
    bool IsReady(void) const;
    DataRate GetTotalRate(void) const;
    uint32_t GetNumPaths(void) const;
    std::map<std::string, double> GetPathStats(uint32_t pathId) const;
    bool SendPacketOnPath(uint32_t pathId, Ptr<Packet> packet);
    void SetNadaAdaptability(uint32_t pathId, DataRate minRate, DataRate maxRate, Time rttMax);
    void SetVideoReceiver(Ptr<VideoReceiver> receiver);
    void ValidateAllSockets(void);
    void ReportSocketStatus();
    void HandleSocketClose(Ptr<Socket> socket);
    void HandleSocketError(Ptr<Socket> socket);

    uint32_t GetPacketSize(void) const;

    // Strategy-specific methods (pure virtual)
    bool SendVideoFrame(uint32_t frameId, bool isKeyFrame, uint32_t frameSize,uint32_t mtu);
    virtual bool Send(Ptr<Packet> packet);
    virtual std::string GetStrategyName() const = 0;
    virtual void UpdateWeights() = 0;

protected:
    // Core implementation
    virtual void StartApplication(void) override;
    virtual void StopApplication(void) override;
    virtual void DoDispose(void) override;

    void PeriodicHealthCheck(void);
    void InitializePathSocket(uint32_t pathId);
    void ValidatePathSocket(uint32_t pathId);
    void HandleRecv(Ptr<Socket> socket);
    void HandleAck(uint32_t pathId, Ptr<Packet> packet, Time delay);
    bool IsSocketReady(Ptr<Socket> socket) const;
    void UpdatePathDistribution();

    // Shared data
    std::map<uint32_t, PathInfo> m_paths;
    std::map<Ptr<Socket>, uint32_t> m_socketToPathId;

    uint32_t m_packetSize;
    uint32_t m_maxPackets;
    bool m_running;
    bool m_videoMode;
    bool m_isKeyFrame;
    DataRate m_totalRate;
    uint32_t m_totalPacketsSent;

    EventId m_updateEvent;
    Time m_updateInterval;
    Ptr<VideoReceiver> m_videoReceiver;

private:
    bool m_isVideoMode;
};

} // namespace ns3

#endif /* MP_NADA_BASE_H */
