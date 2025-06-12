#ifndef DUAL_PATH_NADA_CLIENT_H
#define DUAL_PATH_NADA_CLIENT_H

#include "nada-improved.h"
#include "nada-udp-client.h"

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/data-rate.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"

#include <map>
#include <vector>

namespace ns3
{

/**
 * \brief A agg-path NADA client that aggregates metrics from two paths
 *
 * This client maintains two separate paths and uses round-robin sending,
 * while the NADA algorithm uses aggregated RTT measurements from both paths.
 */
class AggregatePathNadaClient : public Application
{
  public:

    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    /**
     * \brief Constructor
     */
    AggregatePathNadaClient();

    /**
     * \brief Destructor
     */
    virtual ~AggregatePathNadaClient();

    /**
     * \brief Add a path to the client
     * \param pathId Path identifier (1 or 2)
     * \param localAddress Local address for this path
     * \param remoteAddress Remote address for this path
     * \return true if path was added successfully
     */
    bool AddPath(uint32_t pathId, Address localAddress, Address remoteAddress);

    /**
     * \brief Set packet size for both paths
     * \param size Packet size in bytes
     */
    void SetPacketSize(uint32_t size);

    /**
     * \brief Set maximum number of packets to send
     * \param maxPackets Maximum packets
     */
    void SetMaxPackets(uint32_t maxPackets);

    /**
     * \brief Send a packet using round-robin path selection
     * \param packet Packet to send (optional, will create one if null)
     * \return true if packet was sent successfully
     */
    bool Send(Ptr<Packet> packet = nullptr);

    /**
     * \brief Get current NADA sending rate
     * \return Current data rate
     */
    DataRate GetCurrentRate() const;

    /**
     * \brief Get aggregated RTT from both paths
     * \return Aggregated RTT
     */
    Time GetAggregatedRtt();

    /**
     * \brief Check if both paths are ready
     * \return true if both paths have valid sockets
     */
    bool IsReady() const;

    /**
     * \brief Get statistics for a specific path
     * \param pathId Path identifier
     * \return Map of statistics
     */
    std::map<std::string, double> GetPathStats(uint32_t pathId) const;

    /**
     * \brief Check if the client is connected and ready to send
     * \return true if at least one path is connected
     */
    bool IsConnected() const;

    /**
     * \brief Get total rate across all paths
     * \return Combined data rate from all paths
     */
    DataRate GetTotalRate() const;

    /**
     * \brief Enable/disable video mode
     * \param enable True to enable video mode
     */
    void SetVideoMode(bool enable);

    /**
     * \brief Set whether current frame is a key frame
     * \param isKeyFrame True if current frame is a key frame
     */
    void SetKeyFrameStatus(bool isKeyFrame);

    void SetPathCapacities(DataRate path1Capacity, DataRate path2Capacity);

  protected:
    virtual void DoDispose(void);

  private:
    struct PathInfo
    {
        Ptr<Socket> socket;
        Address localAddress;
        Address remoteAddress;
        uint32_t packetsSent;
        uint32_t packetsAcked;
        Time lastRtt;
        Time baseRtt;
        std::vector<Time> rttSamples; // For calculating RTT differences
        DataRate linkCapacity;
    };

    // Application lifecycle
    virtual void StartApplication(void);
    virtual void StopApplication(void);

    /**
     * \brief Initialize socket for a specific path
     * \param pathId Path identifier
     */
    void InitializePathSocket(uint32_t pathId);

    /**
     * \brief Handle received packets
     * \param socket Socket that received the packet
     */
    void HandleRecv(Ptr<Socket> socket);

    /**
     * \brief Update NADA algorithm with aggregated metrics
     */
    void UpdateNadaMetrics();

    /**
     * \brief Calculate aggregated RTT from both paths
     * \return Aggregated RTT value
     */
    Time CalculateAggregatedRtt();

    /**
     * \brief Send packet on specific path
     * \param pathId Path to use
     * \param packet Packet to send
     * \return true if sent successfully
     */
    bool SendOnPath(uint32_t pathId, Ptr<Packet> packet);

    /**
     * \brief Send a video frame using NADA algorithm
     * \param frameId Frame identifier
     * \param isKeyFrame True if this is a key frame
     * \param frameSize Size of the video frame in bytes
     * \param mtu Maximum transmission unit for the path
     * \return true if the frame was sent successfully
     */
    bool SendVideoFrame(uint32_t frameId,bool isKeyFrame,uint32_t frameSize,uint32_t mtu);

    // Member variables
    std::map<uint32_t, PathInfo> m_paths;
    std::map<Ptr<Socket>, uint32_t> m_socketToPathId;

    Ptr<NadaCongestionControl> m_nada;

    uint32_t m_packetSize;
    uint32_t m_maxPackets;
    uint32_t m_totalPacketsSent;
    uint32_t m_currentPathIndex; // For round-robin

    bool m_running;
    EventId m_sendEvent;
    EventId m_updateEvent;

    Time m_interval; // Sending interval based on NADA rate

    bool m_videoMode;     // Video mode flag
    bool m_isKeyFrame;    // Current frame type
    DataRate m_totalRate; // Cached total rate

    uint64_t m_totalLinkCapacity;
};

/**
 * \brief Helper class for AggregatePathNadaClient
 */
class AggregatePathNadaClientHelper
{
  public:
    /**
     * \brief Constructor
     */
    AggregatePathNadaClientHelper();

    /**
     * \brief Destructor
     */
    ~AggregatePathNadaClientHelper();

    /**
     * \brief Set an attribute for the application
     * \param name Attribute name
     * \param value Attribute value
     */
    void SetAttribute(std::string name, const AttributeValue& value);

    /**
     * \brief Install the application on a node
     * \param node Node to install on
     * \return Application container
     */
    ApplicationContainer Install(Ptr<Node> node) const;

    /**
     * \brief Install the application on multiple nodes
     * \param c Node container
     * \return Application container
     */
    ApplicationContainer Install(NodeContainer c) const;

  private:
    /**
     * \brief Install on a single node
     * \param node Node to install on
     * \return Application pointer
     */
    Ptr<Application> InstallPriv(Ptr<Node> node) const;

    ObjectFactory m_factory;
};

} // namespace ns3

#endif /* DUAL_PATH_NADA_CLIENT_H */
