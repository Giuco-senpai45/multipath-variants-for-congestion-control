#ifndef NADA_MODULE_H
#define NADA_MODULE_H

#include "ns3/object.h"
#include "ns3/socket.h"
#include "ns3/data-rate.h"
#include "ns3/packet.h"
#include "ns3/nstime.h"
#include "ns3/applications-module.h"

namespace ns3 {

/**
 * \brief Implementation of the NADA (Network-Assisted Dynamic Adaptation)
 * congestion control algorithm as described in IETF RFC 8698
 */
class NadaCongestionControl : public Object
{
public:
  static TypeId GetTypeId (void);

  NadaCongestionControl ();
  virtual ~NadaCongestionControl ();

  /**
   * \brief Initialize the congestion control algorithm
   * \param socket Pointer to the socket to be controlled
   */
  void Init (Ptr<Socket> socket);

  /**
   * \brief Process acknowledgment received from receiver
   * \param ack The acknowledgment packet
   * \param delay The estimated one-way delay
   */
  void ProcessAck (Ptr<Packet> ack, Time delay);

  /**
   * \brief Process packet loss event
   * \param lossRate Estimated packet loss rate
   */
  void ProcessLoss (double lossRate);

  /**
   * \brief Update sending rate
   * \return The new calculated sending rate
   */
  DataRate UpdateRate ();

  /**
   * \brief Process periodic updates
   */
  void PeriodicUpdate ();

private:
  // Key NADA algorithm parameters (based on RFC 8698)
  double m_alpha;              // Smoothing factor for delay-based congestion detection
  double m_beta;               // Multiplicative decrease factor
  double m_gamma;              // Additive increase factor
  double m_delta;              // Multiplicative increase factor
  double m_minRate;            // Minimum sending rate (bps)
  double m_maxRate;            // Maximum sending rate (bps)
  double m_currentRate;        // Current sending rate (bps)

  double m_baseDelay;          // Baseline delay estimate
  double m_currentDelay;       // Current delay measurement
  double m_lossRate;           // Current loss rate estimate

  Time m_rtt;                  // Estimated round-trip time
  Time m_lastUpdateTime;       // Time of last rate update

  Ptr<Socket> m_socket;        // Socket being controlled
  EventId m_updateEvent;       // Timer for periodic updates

  // Calculate the congestion score (NADA-specific)
  double CalculateScore ();

  // Helper methods
  double EstimateBaseDelay (Time delay);
  double EstimateQueueingDelay ();
};

/**
 * \brief Helper class to install NADA on nodes
 */
class NadaCongestionControlHelper
{
public:
  NadaCongestionControlHelper ();

  /**
   * \brief Install NADA congestion control on a node
   * \param node The node to install NADA on
   * \return Container with the created NADA object
   */
  ApplicationContainer Install (Ptr<Node> node) const;
};

} // namespace ns3

#endif /* NADA_MODULE_H */
