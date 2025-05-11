#include "nada-header.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NadaHeader");
NS_OBJECT_ENSURE_REGISTERED(NadaHeader);

NadaHeader::NadaHeader()
    : m_seq(0),
      m_timestamp(0),
      m_recvTimestamp(0),
      m_receiveRate(0.0),
      m_lossRate(0.0),
      m_ecnMarked(false),
      m_overheadFactor(1.0),
      m_delayGradient(0.0),
      m_packetSize(0),
      m_videoFrameSize(0),
      m_videoFrameType(0),
      m_arrivalTimeOffset(0),
      m_referenceDelta(0.0)
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
NadaHeader::Print(std::ostream& os) const
{
    NS_LOG_FUNCTION(this << &os);
    os << "seq=" << m_seq << " timestamp=" << m_timestamp << " recv_timestamp=" << m_recvTimestamp
       << " receive_rate=" << m_receiveRate << " loss_rate=" << m_lossRate
       << " ecn_marked=" << m_ecnMarked << " overhead_factor=" << m_overheadFactor
       << " delay_gradient=" << m_delayGradient << " packet_size=" << m_packetSize
       << " video_frame_size=" << m_videoFrameSize;
}

uint32_t
NadaHeader::GetSerializedSize(void) const
{
    NS_LOG_FUNCTION(this);
    // 4 bytes (seq) + 8 bytes (timestamp) + 8 bytes (recv timestamp) +
    // 8 bytes (receive rate) + 8 bytes (loss rate) + 1 byte (ECN) +
    // 8 bytes (overhead) + 8 bytes (delay gradient) + 4 bytes (packet size) +
    // 4 bytes (video frame size) + 1 byte (video frame type) +
    // 8 bytes (arrival time offset) + 8 bytes (reference delta)
    return 78; // Updated to include all fields
}

void
NadaHeader::Serialize(Buffer::Iterator start) const
{
    NS_LOG_FUNCTION(this << &start);
    // Basic fields
    start.WriteHtonU32(m_seq);
    start.WriteHtonU64(m_timestamp);
    start.WriteHtonU64(m_recvTimestamp);

    // Serialize doubles directly as bytes
    // This approach is more portable than reinterpret_cast
    uint64_t receiveRate, lossRate, overheadFactor, delayGradient, referenceDelta;

    // Use memcpy to avoid aliasing issues
    memcpy(&receiveRate, &m_receiveRate, sizeof(double));
    memcpy(&lossRate, &m_lossRate, sizeof(double));
    memcpy(&overheadFactor, &m_overheadFactor, sizeof(double));
    memcpy(&delayGradient, &m_delayGradient, sizeof(double));
    memcpy(&referenceDelta, &m_referenceDelta, sizeof(double));

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

    // Add the missing fields
    start.WriteHtonU64(m_arrivalTimeOffset);
    start.WriteHtonU64(referenceDelta);
}

uint32_t
NadaHeader::Deserialize(Buffer::Iterator start)
{
    NS_LOG_FUNCTION(this << &start);

    try
    {
        // Keep track of the start position for calculating total bytes read
        Buffer::Iterator bufferStart = start;
        uint32_t headerSize = GetSerializedSize();

        // Check if we have enough bytes
        if (start.GetSize() < headerSize)
        {
            NS_LOG_WARN("Buffer size (" << start.GetSize() << ") smaller than required header size ("
                      << headerSize << ")");
            return 0;
        }

        // Simple members first - less likely to cause issues
        m_seq = start.ReadNtohU32();

        // Read timestamp as raw uint64_t (don't convert to Time yet)
        m_timestamp = start.ReadNtohU64();

        // Read receive timestamp as raw uint64_t
        m_recvTimestamp = start.ReadNtohU64();

        // For double values, read as uint64_t and then convert safely
        uint64_t receiveRateBytes, lossRateBytes;

        // Check if we have at least 16 bytes for the two doubles
        if (start.GetRemainingSize() >= 16)
        {
            receiveRateBytes = start.ReadNtohU64();
            lossRateBytes = start.ReadNtohU64();

            // Convert to doubles safely
            memcpy(&m_receiveRate, &receiveRateBytes, sizeof(double));
            memcpy(&m_lossRate, &lossRateBytes, sizeof(double));
        }
        else
        {
            NS_LOG_WARN("Not enough bytes for receive/loss rates, using defaults");
            m_receiveRate = 0.0;
            m_lossRate = 0.0;
        }

        // Continue only if buffer has enough bytes
        if (start.GetRemainingSize() >= 1)
        {
            uint8_t ecnByte = start.ReadU8();
            m_ecnMarked = (ecnByte == 1);
        }
        else
        {
            m_ecnMarked = false;
        }

        // Read overhead factor and delay gradient (doubles)
        if (start.GetRemainingSize() >= 16)
        {
            uint64_t overheadBytes = start.ReadNtohU64();
            uint64_t delayGradientBytes = start.ReadNtohU64();

            memcpy(&m_overheadFactor, &overheadBytes, sizeof(double));
            memcpy(&m_delayGradient, &delayGradientBytes, sizeof(double));
        }
        else
        {
            m_overheadFactor = 1.0;
            m_delayGradient = 0.0;
        }

        // Read packet size and video information
        if (start.GetRemainingSize() >= 12)
        {
            m_packetSize = start.ReadNtohU32();
            m_videoFrameSize = start.ReadNtohU32();
            m_videoFrameType = start.ReadNtohU32();
        }
        else
        {
            m_packetSize = 0;
            m_videoFrameSize = 0;
            m_videoFrameType = 0;
        }

        // Read newer fields added to the header
        if (start.GetRemainingSize() >= 12)
        {
            m_arrivalTimeOffset = start.ReadNtohU32();

            uint64_t refDeltaBytes = start.ReadNtohU64();
            memcpy(&m_referenceDelta, &refDeltaBytes, sizeof(double));
        }
        else
        {
            m_arrivalTimeOffset = 0;
            m_referenceDelta = 0.0;
        }

        // Calculate how many bytes we've actually read
        uint32_t bytesRead = start.GetDistanceFrom(bufferStart);
        NS_LOG_DEBUG("Deserialized " << bytesRead << " bytes from NadaHeader");

        return bytesRead;
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception during NadaHeader::Deserialize: " << e.what());
        // Set valid defaults in case of error
        m_seq = 0;
        m_timestamp = 0;
        m_recvTimestamp = 0;
        m_receiveRate = 0.0;
        m_lossRate = 0.0;
        m_ecnMarked = false;
        m_overheadFactor = 1.0;
        m_delayGradient = 0.0;
        m_packetSize = 0;
        m_videoFrameSize = 0;
        m_videoFrameType = 0;
        m_arrivalTimeOffset = 0;
        m_referenceDelta = 0.0;
        return 0;
    }
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

uint32_t
NadaHeader::GetStaticSize(void)
{
    NS_LOG_FUNCTION_NOARGS();
    // This should return the same size as GetSerializedSize()
    // 4 bytes (seq) + 8 bytes (timestamp) + 8 bytes (recv timestamp) +
    // 8 bytes (receive rate) + 8 bytes (loss rate) + 1 byte (ECN) +
    // 8 bytes (overhead) + 8 bytes (delay gradient) + 4 bytes (packet size) +
    // 4 bytes (video frame size) + 1 byte (video frame type)
    return 78;
}

void
NadaHeader::SetArrivalTimeOffset(int64_t offset)
{
    NS_LOG_FUNCTION(this << offset);
    m_arrivalTimeOffset = offset;
}

void
NadaHeader::SetReferenceDelta(double referenceDelta)
{
    NS_LOG_FUNCTION(this << referenceDelta);
    m_referenceDelta = referenceDelta;
}

int64_t
NadaHeader::GetArrivalTimeOffset() const
{
    NS_LOG_FUNCTION(this);
    return m_arrivalTimeOffset;
}

double
NadaHeader::GetReferenceDelta() const
{
    NS_LOG_FUNCTION(this);
    return m_referenceDelta;
}

bool
NadaHeader::IsHeaderSizeValid(Ptr<Packet> packet) const
{
    if (!packet)
    {
        return false;
    }
    return packet->GetSize() >= NadaHeader::GetStaticSize();
}

} // namespace ns3
