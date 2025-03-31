#ifndef NADA_UDP_CLIENT_H
#define NADA_UDP_CLIENT_H

#include "ns3/applications-module.h"
#include "ns3/socket.h"

namespace ns3
{

class NadaCongestionControl;

// UDP client implementation that uses NADA congestion control
class UdpNadaClient : public Application
{
  public:
    static TypeId GetTypeId(void);
    UdpNadaClient();
    virtual ~UdpNadaClient();

    void SetRemote(Address ip, uint16_t port);
    void SetRemote(Address addr);

    Ptr<Socket> GetSocket() const;
    void SendPacket(Ptr<Packet> packet);

  protected:
    virtual void DoDispose(void);

  private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

    void Send(void);
    void HandleRead(Ptr<Socket> socket);

    uint32_t m_packetSize;  // Size of each packet sent
    uint32_t m_numPackets;  // Number of packets to send
    Time m_interval;        // Initial packet interval
    bool m_running;         // Is the application running?
    uint32_t m_packetsSent; // Count of sent packets

    Ptr<Socket> m_socket;              // Socket for sending data
    Address m_peer;                    // Destination address
    EventId m_sendEvent;               // Event to send next packet
    Ptr<NadaCongestionControl> m_nada; // NADA congestion control
};

/**
 * \brief Helper to create UdpNadaClient applications
 */
class UdpNadaClientHelper
{
  public:
    UdpNadaClientHelper(Address address, uint16_t port);
    UdpNadaClientHelper(Address address);

    void SetAttribute(std::string name, const AttributeValue& value);
    ApplicationContainer Install(Ptr<Node> node) const;
    ApplicationContainer Install(std::string nodeName) const;
    ApplicationContainer Install(NodeContainer c) const;

  private:
    ObjectFactory m_factory;
    Ptr<Application> InstallPriv(Ptr<Node> node) const;
};

} // namespace ns3

#endif // NADA_UDP_CLIENT_H
