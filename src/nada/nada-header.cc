#include "nada-header.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("NadaHeader");
NS_OBJECT_ENSURE_REGISTERED(NadaHeader);

NadaHeader::NadaHeader() :
  m_seq(0),
  m_timestamp(0),
  m_recvTimestamp(0),
  m_receiveRate(0.0),
  m_lossRate(0.0),
  m_ecnMarked(false),
  m_overheadFactor(1.0),
  m_delayGradient(0.0),
  m_packetSize(0),
  m_videoFrameSize(0),
  m_videoFrameType(0)
{
  NS_LOG_FUNCTION(this);
}

NadaHeader::~NadaHeader()
{
  NS_LOG_FUNCTION(this);
}

TypeId
NadaHeader::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::NadaHeader")
    .SetParent<Header>()
    .SetGroupName("Internet")
    .AddConstructor<NadaHeader>();
  return tid;
}

TypeId
NadaHeader::GetInstanceTypeId(void) const
{
  return GetTypeId();
}

void
NadaHeader::Print(std::ostream &os) const
{
  NS_LOG_FUNCTION(this << &os);
  os << "seq=" << m_seq
     << " timestamp=" << m_timestamp
     << " recv_timestamp=" << m_recvTimestamp
     << " receive_rate=" << m_receiveRate
     << " loss_rate=" << m_lossRate
     << " ecn_marked=" << m_ecnMarked
     << " overhead_factor=" << m_overheadFactor
     << " delay_gradient=" << m_delayGradient
     << " packet_size=" << m_packetSize
     << " video_frame_size=" << m_videoFrameSize;
}

uint32_t
NadaHeader::GetSerializedSize(void) const
{
  NS_LOG_FUNCTION(this);
  // 4 bytes (seq) + 8 bytes (timestamp) + 8 bytes (recv timestamp) +
  // 8 bytes (receive rate) + 8 bytes (loss rate) + 1 byte (ECN) +
  // 8 bytes (overhead) + 8 bytes (delay gradient) + 4 bytes (packet size) +
  // 4 bytes (video frame size) + 1 byte (video frame type)
  return 62;
}

void
NadaHeader::Serialize(Buffer::Iterator start) const
{
  NS_LOG_FUNCTION(this << &start);
  // Basic fields
  start.WriteHtonU32(m_seq);
  start.WriteHtonU64(m_timestamp);
  start.WriteHtonU64(m_recvTimestamp);

  // Convert doubles to uint64_t for serialization
  uint64_t receiveRate = *(reinterpret_cast<const uint64_t*>(&m_receiveRate));
  uint64_t lossRate = *(reinterpret_cast<const uint64_t*>(&m_lossRate));
  uint64_t overheadFactor = *(reinterpret_cast<const uint64_t*>(&m_overheadFactor));
  uint64_t delayGradient = *(reinterpret_cast<const uint64_t*>(&m_delayGradient));

  start.WriteHtonU64(receiveRate);
  start.WriteHtonU64(lossRate);

  // ECN marking as a byte (boolean)
  start.WriteU8(m_ecnMarked ? 1 : 0);

  // Additional fields for RFC compliance
  start.WriteHtonU64(overheadFactor);
  start.WriteHtonU64(delayGradient);
  start.WriteHtonU32(m_packetSize);
  start.WriteHtonU32(m_videoFrameSize);
  start.WriteU8(m_videoFrameType);
}

uint32_t
NadaHeader::Deserialize(Buffer::Iterator start)
{
  NS_LOG_FUNCTION(this << &start);

  // Basic fields
  m_seq = start.ReadNtohU32();
  m_timestamp = start.ReadNtohU64();
  m_recvTimestamp = start.ReadNtohU64();

  // Convert uint64_t back to double
  uint64_t receiveRate = start.ReadNtohU64();
  uint64_t lossRate = start.ReadNtohU64();

  m_receiveRate = *(reinterpret_cast<double*>(&receiveRate));
  m_lossRate = *(reinterpret_cast<double*>(&lossRate));

  // ECN marking
  m_ecnMarked = (start.ReadU8() == 1);

  // Additional fields
  uint64_t overheadFactor = start.ReadNtohU64();
  uint64_t delayGradient = start.ReadNtohU64();
  m_overheadFactor = *(reinterpret_cast<double*>(&overheadFactor));
  m_delayGradient = *(reinterpret_cast<double*>(&delayGradient));

  m_packetSize = start.ReadNtohU32();
  m_videoFrameSize = start.ReadNtohU32();
  m_videoFrameType = start.ReadU8();

  return GetSerializedSize();
}

void
NadaHeader::SetEcnMarked(bool ecnMarked)
{
  NS_LOG_FUNCTION(this << ecnMarked);
  m_ecnMarked = ecnMarked;
}

bool
NadaHeader::GetEcnMarked() const
{
  NS_LOG_FUNCTION(this);
  return m_ecnMarked;
}

void
NadaHeader::SetOverheadFactor(double factor)
{
  NS_LOG_FUNCTION(this << factor);
  m_overheadFactor = factor;
}

double
NadaHeader::GetOverheadFactor() const
{
  NS_LOG_FUNCTION(this);
  return m_overheadFactor;
}

void
NadaHeader::SetDelayGradient(double gradient)
{
  NS_LOG_FUNCTION(this << gradient);
  m_delayGradient = gradient;
}

double
NadaHeader::GetDelayGradient() const
{
  NS_LOG_FUNCTION(this);
  return m_delayGradient;
}

void
NadaHeader::SetPacketSize(uint32_t size)
{
  NS_LOG_FUNCTION(this << size);
  m_packetSize = size;
}

uint32_t
NadaHeader::GetPacketSize() const
{
  NS_LOG_FUNCTION(this);
  return m_packetSize;
}

void
NadaHeader::SetVideoFrameSize(uint32_t size)
{
  NS_LOG_FUNCTION(this << size);
  m_videoFrameSize = size;
}

void
NadaHeader::SetVideoFrameType(uint8_t frameType)
{
  NS_LOG_FUNCTION(this << static_cast<uint32_t>(frameType));
  m_videoFrameType = frameType;
}

uint8_t
NadaHeader::GetVideoFrameType() const
{
  NS_LOG_FUNCTION(this);
  return m_videoFrameType;
}


uint32_t
NadaHeader::GetVideoFrameSize() const
{
  NS_LOG_FUNCTION(this);
  return m_videoFrameSize;
}

void
NadaHeader::SetSequenceNumber(uint32_t seq)
{
  NS_LOG_FUNCTION(this << seq);
  m_seq = seq;
}

void
NadaHeader::SetTimestamp(Time timestamp)
{
  NS_LOG_FUNCTION(this << timestamp);
  m_timestamp = timestamp.GetNanoSeconds();
}

void
NadaHeader::SetReceiveTimestamp(Time timestamp)
{
  NS_LOG_FUNCTION(this << timestamp);
  m_recvTimestamp = timestamp.GetNanoSeconds();
}

void
NadaHeader::SetReceiveRate(double rate)
{
  NS_LOG_FUNCTION(this << rate);
  m_receiveRate = rate;
}

void
NadaHeader::SetLossRate(double lossRate)
{
  NS_LOG_FUNCTION(this << lossRate);
  m_lossRate = lossRate;
}

uint32_t
NadaHeader::GetSequenceNumber() const
{
  NS_LOG_FUNCTION(this);
  return m_seq;
}

Time
NadaHeader::GetTimestamp() const
{
  NS_LOG_FUNCTION(this);
  return NanoSeconds(m_timestamp);
}

Time
NadaHeader::GetReceiveTimestamp() const
{
  NS_LOG_FUNCTION(this);
  return NanoSeconds(m_recvTimestamp);
}

double
NadaHeader::GetReceiveRate() const
{
  NS_LOG_FUNCTION(this);
  return m_receiveRate;
}

double
NadaHeader::GetLossRate() const
{
  NS_LOG_FUNCTION(this);
  return m_lossRate;
}

} // namespace ns3
