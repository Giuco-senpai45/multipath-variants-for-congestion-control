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
  m_lossRate(0.0)
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
     << " loss_rate=" << m_lossRate;
}

uint32_t
NadaHeader::GetSerializedSize(void) const
{
  NS_LOG_FUNCTION(this);
  // 4 bytes (seq) + 8 bytes (timestamp) + 8 bytes (recv timestamp) + 
  // 8 bytes (receive rate) + 8 bytes (loss rate)
  return 36;
}

void
NadaHeader::Serialize(Buffer::Iterator start) const
{
  NS_LOG_FUNCTION(this << &start);
  start.WriteHtonU32(m_seq);
  start.WriteHtonU64(m_timestamp);
  start.WriteHtonU64(m_recvTimestamp);
  
  // Convert double to uint64_t for serialization
  uint64_t receiveRate = *(reinterpret_cast<const uint64_t*>(&m_receiveRate));
  uint64_t lossRate = *(reinterpret_cast<const uint64_t*>(&m_lossRate));
  
  start.WriteHtonU64(receiveRate);
  start.WriteHtonU64(lossRate);
}

uint32_t
NadaHeader::Deserialize(Buffer::Iterator start)
{
  NS_LOG_FUNCTION(this << &start);
  m_seq = start.ReadNtohU32();
  m_timestamp = start.ReadNtohU64();
  m_recvTimestamp = start.ReadNtohU64();
  
  // Convert uint64_t back to double
  uint64_t receiveRate = start.ReadNtohU64();
  uint64_t lossRate = start.ReadNtohU64();
  
  m_receiveRate = *(reinterpret_cast<double*>(&receiveRate));
  m_lossRate = *(reinterpret_cast<double*>(&lossRate));
  
  return GetSerializedSize();
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