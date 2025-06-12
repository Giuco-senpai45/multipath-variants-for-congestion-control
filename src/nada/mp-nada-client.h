#ifndef MULTIPATH_NADA_CLIENT_H
#define MULTIPATH_NADA_CLIENT_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/nada-improved.h"
#include "ns3/nada-udp-client.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"
#include "ns3/traced-callback.h"
#include "ns3/video-receiver.h"

#include <map>
#include <vector>

namespace ns3
{

/**
 * \brief Multipath NADA client application
 *
 * This class manages multiple NADA UDP clients to implement multipath
 * transmission to a single destination using multiple paths.
 */
class MultiPathNadaClient : public Application
{
  public:
    enum PathSelectionStrategy
    {
        WEIGHTED = 0,    // Default weighted distribution based on path quality
        BEST_PATH = 1,   // Best path based on metrics
        EQUAL = 2,       // Equal distribution (round-robin)
        REDUNDANT = 3,   // Redundant transmission on all paths
        FRAME_AWARE = 4, // Frame type aware (I-frames on best path)
        BUFFER_AWARE = 5 // Buffer-aware path selection
    };

    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    /**
     * \brief Constructor
     */
    MultiPathNadaClient();

    /**
     * \brief Destructor
     */
    virtual ~MultiPathNadaClient();

    /**
     * \brief Add a new path to the multipath client
     * \param localAddress The local address to bind to
     * \param remoteAddress The remote address to send to
     * \param pathId The ID for this path (must be unique)
     * \param weight The weight for this path in the overall distribution
     * \param initialRate The initial sending rate for this path
     * \return true if the path was added successfully
     */
    bool AddPath(Address localAddress,
                 Address remoteAddress,
                 uint32_t pathId,
                 double weight,
                 DataRate initialRate);

    /**
     * \brief Remove a path from the multipath client
     * \param pathId The ID of the path to remove
     * \return true if the path was removed successfully
     */
    bool RemovePath(uint32_t pathId);

    /**
     * \brief Get the number of active paths
     * \return The number of active paths
     */
    uint32_t GetNumPaths(void) const;

    /**
     * \brief Get the total sending rate across all paths
     * \return The total sending rate in bits per second
     */
    DataRate GetTotalRate(void) const;

    /**
     * \brief Set the packet size for all paths
     * \param size The packet size in bytes
     */
    void SetPacketSize(uint32_t size);

    /**
     * \brief Set the maximum number of packets for all paths
     * \param numPackets The maximum number of packets
     */
    void SetMaxPackets(uint32_t numPackets);

    /**
     * \brief Set the path weight (for load balancing)
     * \param pathId The ID of the path
     * \param weight The new weight for the path
     * \return true if the weight was updated successfully
     */
    bool SetPathWeight(uint32_t pathId, double weight);

    /**
     * \brief Get the path statistics
     * \param pathId The ID of the path
     * \return A map of statistics for the specified path
     */
    std::map<std::string, double> GetPathStats(uint32_t pathId) const;

    /**
     * \brief Start the application
     */
    virtual void StartApplication(void);

    /**
     * \brief Stop the application
     */
    virtual void StopApplication(void);

    /**
     * \brief Struct to hold path information
     */
    struct PathInfo
    {
        Ptr<UdpNadaClient> client;       // The NADA UDP client for this path
        Ptr<NadaCongestionControl> nada; // The NADA congestion control for this path
        double weight;                   // The weight for this path in load balancing
        DataRate currentRate;            // Current sending rate for this path
        uint32_t packetsSent;            // Number of packets sent on this path
        uint32_t packetsAcked;           // Number of packets acknowledged on this path
        Time lastRtt;                    // Last measured RTT for this path
        Time lastDelay;                  // Last measured one-way delay for this path
        Address localAddress;            // Local address for this path
        Address remoteAddress;           // Remote address for this path
    };

    /**
     * \brief Update the path distribution based on current network conditions
     */
    void UpdatePathDistribution(void);

    /**
     * \brief Send packets on all paths
     */
    void SendPackets(void);

    /**
     * \brief Handle packet acknowledgments
     * \param pathId The ID of the path that received the acknowledgment
     * \param packet The packet that was acknowledged
     * \param delay The one-way delay measured
     */
    void HandleAck(uint32_t pathId, Ptr<Packet> packet, Time delay);

    /**
     * \brief Handle received packets
     * \param socket The socket that received the packet
     */
    void HandleRecv(Ptr<Socket> socket);
    void VideoFrameAcked(uint32_t pathId, bool isKeyFrame, uint32_t frameSize);
    bool SelectPathForFrame(bool isKeyFrame, uint32_t& pathId);
    bool IsReady(void) const;
    uint32_t GetPacketSize(void) const;
    void SetNadaAdaptability(uint32_t pathId, DataRate minRate, DataRate maxRate, Time rttMax);
    void SetVideoMode(bool enable);
    void SetKeyFrameStatus(bool isKeyFrame);

    void InitializePathSocket(uint32_t pathId);
    void ReportSocketStatus();

    void ValidateAllSockets(void);
    void GetAvailablePaths(std::vector<uint32_t>& outPaths) const;
    bool Send(Ptr<Packet> packet = nullptr);

    void SetPathSelectionStrategy(uint32_t strategy);
    std::string GetStrategyName(uint32_t strategy) const;
    void SetVideoReceiver(Ptr<VideoReceiver> receiver);
    void SetBufferAwareParameters(double targetBufferLength, double bufferWeightFactor);
    uint32_t GetBufferAwarePath(const std::vector<uint32_t>& readyPaths);
    double CalculateBufferWeight(double currentBufferMs, uint32_t pathId);
    uint32_t GetBestPathByRTT();

  protected:
    virtual void DoDispose(void);
    bool IsSocketReady(Ptr<Socket> socket);
    void ValidatePathSocket(uint32_t pathId);

    uint32_t GetBestPath(const std::vector<uint32_t>& readyPaths);
    uint32_t GetWeightedPath(const std::vector<uint32_t>& readyPaths);
    uint32_t GetFrameAwarePath(const std::vector<uint32_t>& readyPaths, bool isKeyFrame);
    bool SendRedundantlyPath(const std::vector<uint32_t>& readyPaths, Ptr<Packet> packet);
    bool IsValidNadaHeader(Ptr<Packet> packet) const;

    bool SendPacketOnPath(uint32_t pathId, Ptr<Packet> packet);
    bool m_isVideoMode;
    bool m_isKeyFrame;

  private:
    std::map<uint32_t, PathInfo> m_paths;             // Map of path IDs to path information
    std::map<Ptr<Socket>, uint32_t> m_socketToPathId; // Map of sockets to path IDs
    uint32_t m_packetSize;                            // Size of packets to send
    uint32_t m_maxPackets;                            // Maximum number of packets to send
    bool m_running;                                   // Whether the application is running
    EventId m_sendEvent;                              // Event for sending packets
    EventId m_updateEvent;                            // Event for updating path distribution
    DataRate m_totalRate;                             // Total sending rate across all paths
    Time m_updateInterval;                            // Interval for updating path distribution
    uint32_t m_pathSelectionStrategy;                 // Path selection strategy

    // Statistics
    uint32_t m_totalPacketsSent; // Total number of packets sent
    Time m_lastUpdateTime;       // Time of last update

    bool m_videoMode; // Whether video mode is enabled

    Ptr<VideoReceiver> m_videoReceiver;
    double m_targetBufferLength;  // Target buffer length in seconds
    double m_bufferWeightFactor;  // How much buffer status affects path selection
};

/**
 * \brief Helper to make it easier to instantiate MultiPathNadaClient applications
 */
class MultiPathNadaClientHelper
{
  public:
    /**
     * \brief Constructor
     */
    MultiPathNadaClientHelper();

    /**
     * \brief Destructor
     */
    virtual ~MultiPathNadaClientHelper();

    /**
     * \brief Set an attribute value for the client
     * \param name The name of the attribute
     * \param value The value of the attribute
     */
    void SetAttribute(std::string name, const AttributeValue& value);

    /**
     * \brief Install the client on a node
     * \param node The node to install on
     * \return The created application container
     */
    ApplicationContainer Install(Ptr<Node> node) const;

    /**
     * \brief Install the client on a node container
     * \param c The node container to install on
     * \return The created application container
     */
    ApplicationContainer Install(NodeContainer c) const;

  private:
    /**
     * \brief Do the actual installation
     * \param node The node to install on
     * \return The created application
     */
    Ptr<Application> InstallPriv(Ptr<Node> node) const;

    ObjectFactory m_factory; // Factory to create the application
};

} // namespace ns3

#endif /* MULTIPATH_NADA_CLIENT_H */
