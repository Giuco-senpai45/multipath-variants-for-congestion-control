// nada-header.h
#ifndef NADA_HEADER_H
#define NADA_HEADER_H

#include "ns3/header.h"
#include "ns3/nstime.h"

namespace ns3 {

/**
 * \brief NADA packet header for congestion control signaling
 *
 * This header is used to exchange congestion information between
 * NADA senders and receivers.
 */
class NadaHeader : public Header
{
public:
  NadaHeader();
  virtual ~NadaHeader();

  // Static type information
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  
  // Header serialization
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);
  
  // Setters
  void SetSequenceNumber(uint32_t seq);
  void SetTimestamp(Time timestamp);
  void SetReceiveTimestamp(Time timestamp);
  void SetReceiveRate(double rate);
  void SetLossRate(double lossRate);
  
  // Getters
  uint32_t GetSequenceNumber() const;
  Time GetTimestamp() const;
  Time GetReceiveTimestamp() const;
  double GetReceiveRate() const;
  double GetLossRate() const;

private:
  uint32_t m_seq;            // Packet sequence number
  uint64_t m_timestamp;      // Sender timestamp (in ns)
  uint64_t m_recvTimestamp;  // Receiver timestamp (in ns)
  double m_receiveRate;      // Current receive rate estimate (bps)
  double m_lossRate;         // Current loss rate estimate
};

} // namespace ns3

#endif /* NADA_HEADER_H */