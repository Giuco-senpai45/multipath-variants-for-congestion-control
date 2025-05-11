#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

#include "ns3/applications-module.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/address.h"
#include "ns3/socket.h"
#include <deque>
#include <map>

namespace ns3
{

/**
 * \brief NADA UDP Receiver application for providing feedback to clients
 */
class UdpNadaReceiver : public Application
{
public:
  static TypeId GetTypeId (void);
  UdpNadaReceiver ();
  virtual ~UdpNadaReceiver ();

protected:
  virtual void DoDispose (void);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  /**
   * \brief Handle a packet reception.
   * \param socket The receiving socket.
   */
  void HandleRead (Ptr<Socket> socket);

  /**
   * \brief Process periodic statistics and send feedback
   */
  void ProcessStatistics();

  /**
   * \brief Calculate loss statistics for a flow
   * \param flowId The ID of the flow
   * \return The loss rate
   */
  double CalculateLossRate(uint32_t flowId);

  /**
   * \brief Calculate receive rate for a flow
   * \param flowId The ID of the flow
   * \return The receive rate in bps
   */
  double CalculateReceiveRate(uint32_t flowId);

  /**
   * \brief Send feedback to a sender
   * \param flowId The ID of the flow
   */
  void SendFeedback(uint32_t flowId);

  struct FlowStats {
    Address peerAddress;                  // Address of the sender
    std::deque<uint32_t> seqList;         // Received sequence numbers
    std::map<uint32_t, Time> arrivalTimes; // Packet arrival times
    uint32_t lastSeq;                     // Last sequence number processed
    uint32_t receivedBytes;               // Bytes received in current interval
    Time lastReceiveTime;                 // Time of last received packet
    double lossRate;                      // Current loss rate estimate
    double receiveRate;                   // Current receive rate estimate
    double delayGradient;                 // Current delay gradient
    bool ecnMarked;                       // Whether ECN marking was present
    Ptr<Socket> feedbackSocket;           // Socket for sending feedback
  };

  uint16_t m_port;                       // Port to listen on
  Ptr<Socket> m_socket;                  // Listening socket
  EventId m_statisticsEvent;             // Event to process statistics
  std::map<uint32_t, FlowStats> m_flows; // Stats for each flow
  Time m_statInterval;                   // Interval for processing statistics
};

/**
 * \brief Helper to create a UdpNadaReceiver
 */
class UdpNadaReceiverHelper
{
public:
  /**
   * \brief Constructor
   * \param port The port to listen on
   */
  UdpNadaReceiverHelper (uint16_t port);

  /**
   * \brief Record an attribute to be set in the Receiver
   * \param name The name of the attribute
   * \param value The value of the attribute
   */
  void SetAttribute (std::string name, const AttributeValue &value);

  /**
   * \brief Install a UdpNadaReceiver on a node
   * \param node The node to install on
   * \return The application container
   */
  ApplicationContainer Install (Ptr<Node> node) const;

  /**
   * \brief Install a UdpNadaReceiver on multiple nodes
   * \param c The nodes to install on
   * \return The application container
   */
  ApplicationContainer Install (NodeContainer c) const;

private:
  ObjectFactory m_factory; // Factory to create Receiver instances
};

} // namespace ns3

#endif /* UDP_RECEIVER_H */
