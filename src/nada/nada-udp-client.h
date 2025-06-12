#ifndef NADA_UDP_CLIENT_H
#define NADA_UDP_CLIENT_H

#include "ns3/applications-module.h"
#include "ns3/socket.h"

#include <map>

namespace ns3
{

class NadaCongestionControl;

/**
 * \brief Video frame type enumeration
 */
enum VideoFrameType
{
    KEY_FRAME = 0,  // I-frame
    DELTA_FRAME = 1 // P-frame or B-frame
};

/**
 * \brief UDP client implementation that uses NADA congestion control
 */
class UdpNadaClient : public Application
{
  public:
    /**
     * \brief Get the TypeId
     * \return The TypeId for this class
     */
    static TypeId GetTypeId(void);

    /**
     * \brief Constructor
     */
    UdpNadaClient();

    /**
     * \brief Destructor
     */
    virtual ~UdpNadaClient();

    /**
     * \brief Set the remote address and port
     * \param ip The remote IP address
     * \param port The remote port
     */
    void SetRemote(Address ip, uint16_t port);

    /**
     * \brief Set the remote address (containing IP and port)
     * \param addr The remote address
     */
    void SetRemote(Address addr);

    /**
     * \brief Get the socket
     * \return The socket
     */
    Ptr<Socket> GetSocket() const;

    /**
     * \brief Send a packet
     * \param packet The packet to send
     */
    void SendPacket(Ptr<Packet> packet);

    /**
     * \brief Enable video mode
     * \param enable Whether to enable video mode
     */
    void SetVideoMode(bool enable);

    /**
     * \brief Set the current video frame size
     * \param size The frame size in bytes
     */
    void SetVideoFrameSize(uint32_t size);

    /**
     * \brief Set the current video frame type
     * \param type The frame type (KEY_FRAME or DELTA_FRAME)
     */
    void SetVideoFrameType(VideoFrameType type);

    /**
     * \brief Set the overhead ratio for protocol overhead
     * \param ratio The overhead ratio (1.0 = no overhead)
     */
    void SetOverheadRatio(double ratio);

    /**
     * \brief Set the overhead factor for protocol overhead
     * \param factor The overhead factor (1.0 = no overhead)
     */
    void SetOverheadFactor(double factor);

    /**
     * \brief Set the socket directly (for manual socket creation)
     * \param socket The socket to use
     */
    void SetSocket(Ptr<Socket> socket);

    /**
     * \brief Clean up resources
     */
    virtual void DoDispose(void);

    /**
     * \brief Start the application
     */
    virtual void StartApplication(void);

    /**
     * \brief Stop the application
     */
    virtual void StopApplication(void);

    /**
     * \brief Send a packet and schedule the next one
     */
    void Send(void);

    /**
     * \brief Handle received packets (feedback)
     * \param socket The socket that received data
     */
    void HandleRead(Ptr<Socket> socket);

    /**
     * Set the packet size
     * \param size The packet size in bytes
     */
    void SetPacketSize(uint32_t size);

    /**
     * Set the maximum number of packets to send
     * \param maxPackets The maximum number of packets
     */
    void SetMaxPackets(uint32_t maxPackets);

    /**
     * Get the current packet size
     * \return The packet size in bytes
     */
    uint32_t GetPacketSize() const;

    /**
     * Get the maximum number of packets
     * \return The maximum number of packets
     */
    uint32_t GetMaxPackets() const;

  private:
    uint32_t m_packetSize;  // Size of each packet sent
    uint32_t m_numPackets;  // Number of packets to send
    Time m_interval;        // Initial packet interval
    bool m_running;         // Is the application running?
    uint32_t m_packetsSent; // Count of sent packets
    uint32_t m_sequence;    // Current sequence number

    Ptr<Socket> m_socket;              // Socket for sending data
    Address m_peer;                    // Destination address
    EventId m_sendEvent;               // Event to send next packet
    Ptr<NadaCongestionControl> m_nada; // NADA congestion control

    // Video mode parameters
    bool m_videoMode;                  // Whether video mode is enabled
    uint32_t m_currentFrameSize;       // Current video frame size
    VideoFrameType m_currentFrameType; // Current video frame type
    double m_overheadRatio;            // Protocol overhead ratio

    // RTT tracking
    std::map<uint32_t, Time> m_sentPackets; // Sent packets for RTT calculation
};

/**
 * \brief Helper to create UdpNadaClient applications
 */
class UdpNadaClientHelper
{
  public:
    /**
     * \brief Constructor with address and port
     * \param address The destination address
     * \param port The destination port
     */
    UdpNadaClientHelper(Address address, uint16_t port);

    /**
     * \brief Constructor with address (containing port)
     * \param address The destination address
     */
    UdpNadaClientHelper(Address address);

    /**
     * \brief Set an attribute
     * \param name The attribute name
     * \param value The attribute value
     */
    void SetAttribute(std::string name, const AttributeValue& value);

    /**
     * \brief Install on a node
     * \param node The node
     * \return The created applications
     */
    ApplicationContainer Install(Ptr<Node> node) const;

    /**
     * \brief Install on a node by name
     * \param nodeName The node name
     * \return The created applications
     */
    ApplicationContainer Install(std::string nodeName) const;

    /**
     * \brief Install on multiple nodes
     * \param c The nodes
     * \return The created applications
     */
    ApplicationContainer Install(NodeContainer c) const;

  private:
    ObjectFactory m_factory; // Object factory

    /**
     * \brief Install on a node (private implementation)
     * \param node The node
     * \return The created application
     */
    Ptr<Application> InstallPriv(Ptr<Node> node) const;
};

} // namespace ns3

#endif // NADA_UDP_CLIENT_H
