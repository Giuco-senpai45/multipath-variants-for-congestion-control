#ifndef NADA_HEADER_H
#define NADA_HEADER_H

#include "ns3/header.h"
#include "ns3/nstime.h"

namespace ns3 {

/**
 * \ingroup internet
 * \brief Header for NADA (Network-Assisted Dynamic Adaptation) protocol
 *
 * This header follows RFC 8698 Section 5 specifications
 */
class NadaHeader : public Header
{
public:
  /**
   * \brief Constructor
   */
  NadaHeader();
  virtual ~NadaHeader();

  // Inherited from Header
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
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
  void SetEcnMarked(bool ecnMarked);
  void SetOverheadFactor(double factor);
  void SetDelayGradient(double gradient);
  void SetPacketSize(uint32_t size);
  void SetVideoFrameSize(uint32_t size);
  void SetVideoFrameType(uint8_t frameType);
  void SetArrivalTimeOffset(int64_t offset);
  void SetReferenceDelta(double referenceDelta);

  // Getters
  uint32_t GetSequenceNumber() const;
  Time GetTimestamp() const;
  Time GetReceiveTimestamp() const;
  double GetReceiveRate() const;
  double GetLossRate() const;
  bool GetEcnMarked() const;
  double GetOverheadFactor() const;
  double GetDelayGradient() const;
  uint32_t GetPacketSize() const;
  uint32_t GetVideoFrameSize() const;
  uint8_t GetVideoFrameType() const;
  int64_t GetArrivalTimeOffset() const;
  double GetReferenceDelta() const;

private:
  uint32_t m_seq;                  // Sequence number
  uint64_t m_timestamp;            // Sender timestamp in nanoseconds
  uint64_t m_recvTimestamp;        // Receiver timestamp in nanoseconds
  double m_receiveRate;            // Receive rate in bps
  double m_lossRate;               // Loss rate
  bool m_ecnMarked;                // ECN marking flag
  double m_overheadFactor;         // Overhead adjustment factor
  double m_delayGradient;          // Delay gradient
  uint32_t m_packetSize;           // Packet size in bytes
  uint32_t m_videoFrameSize;       // Video frame size in bytes
  uint8_t m_videoFrameType;        // Video frame type (I, P, B frames)
  int64_t m_arrivalTimeOffset;     // Arrival time offset in nanoseconds
  double m_referenceDelta;         // Reference delta from RFC
};

} // namespace ns3

#endif /* NADA_HEADER_H */
