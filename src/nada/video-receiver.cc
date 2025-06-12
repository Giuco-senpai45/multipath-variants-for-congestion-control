#include "video-receiver.h"
#include "nada-header.h"

#include "ns3/address-utils.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <sstream>
#include <iomanip>
#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("VideoReceiver");
NS_OBJECT_ENSURE_REGISTERED(VideoReceiver);

TypeId
VideoReceiver::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::VideoReceiver")
                           .SetParent<Application>()
                           .SetGroupName("Applications")
                           .AddConstructor<VideoReceiver>()
                           .AddAttribute("Port",
                                         "Port on which we listen for incoming packets.",
                                         UintegerValue(9),
                                         MakeUintegerAccessor(&VideoReceiver::m_port),
                                         MakeUintegerChecker<uint16_t>())
                           .AddAttribute("FrameRate",
                                         "Frame rate in frames per second.",
                                         UintegerValue(30),
                                         MakeUintegerAccessor(&VideoReceiver::m_frameRate),
                                         MakeUintegerChecker<uint32_t>());
    return tid;
}

VideoReceiver::VideoReceiver()
    : m_port(9),
      m_frameRate(30),
      m_frameInterval(Seconds(1.0/30.0)), // Default 30fps
      m_lastFrameId(0),
      m_consumedFrames(0),
      m_bufferUnderruns(0)
{
    NS_LOG_FUNCTION(this);
}

VideoReceiver::~VideoReceiver()
{
    NS_LOG_FUNCTION(this);
}

void
VideoReceiver::DoDispose(void)
{
    NS_LOG_FUNCTION(this);
    m_socket = 0;
    Application::DoDispose();
}

void
VideoReceiver::SetFrameRate(uint32_t frameRate)
{
    NS_LOG_FUNCTION(this << frameRate);
    m_frameRate = frameRate;
    m_frameInterval = Seconds(1.0 / frameRate);
}

void
VideoReceiver::StartApplication(void)
{
    NS_LOG_FUNCTION(this);

    if (!m_socket)
    {
        TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
        m_socket = Socket::CreateSocket(GetNode(), tid);

        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
        if (m_socket->Bind(local) == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket");
        }

        // Enable broadcast for sending ACKs
        m_socket->SetAllowBroadcast(true);
        m_socket->SetAttribute("RcvBufSize", UintegerValue(1000000));
    }

    m_socket->SetRecvCallback(MakeCallback(&VideoReceiver::HandleRead, this));

    // Initial buffering - wait for some frames before starting consumption
    uint32_t initialBufferFrames = 5;
    Time initialBufferingTime = Seconds(std::max(10.0, initialBufferFrames * m_frameInterval.GetSeconds()));

    // Schedule initial frame consumption after initial buffering period
    m_consumeEvent = Simulator::Schedule(initialBufferingTime, &VideoReceiver::ConsumeFrame, this);

    NS_LOG_INFO("Video receiver starting with initial buffering time of " <<
                initialBufferingTime.GetSeconds() << " seconds (" <<
                initialBufferFrames << " frames at " << m_frameRate << " fps)");

    // Schedule buffer stats recording (every 100ms)
    m_statsEvent = Simulator::Schedule(MilliSeconds(100), &VideoReceiver::RecordBufferState, this);
}

void
VideoReceiver::StopApplication(void)
{
    NS_LOG_FUNCTION(this);

    if (m_socket)
    {
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    }

    if (m_consumeEvent.IsPending())
    {
        Simulator::Cancel(m_consumeEvent);
    }

    if (m_statsEvent.IsPending())
    {
        Simulator::Cancel(m_statsEvent);
    }
}

void
VideoReceiver::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        // Extract NADA header
        NadaHeader header;
        if (packet->PeekHeader(header) != 0)
        {
            NS_LOG_DEBUG("Received packet with NADA header of size " << packet->GetSize());
            ProcessVideoPacket(packet, from, header);
        }
        else
        {
            NS_LOG_WARN("Received packet without NADA header");
        }
    }
}

void
VideoReceiver::ProcessVideoPacket(Ptr<Packet> packet, Address from, const NadaHeader& header)
{
    NS_LOG_FUNCTION(this << packet << from);

    // Extract frame information from NADA header
    uint32_t sequenceNumber = header.GetSequenceNumber();
    uint32_t frameId, packetId;

    // **CRITICAL FIX: Handle both aggregate and multipath sequence formats**
    if (sequenceNumber >= 1000) {
        // Frame.Packet format (1000+ indicates frame ID encoding)
        frameId = sequenceNumber / 1000;
        packetId = sequenceNumber % 1000;
    } else {
        // Legacy format - convert to frame-based
        static uint32_t legacyFrameId = 1;
        static uint32_t legacyPacketCount = 0;

        if (legacyPacketCount >= 15) { // 15 packets per frame
            legacyFrameId++;
            legacyPacketCount = 0;
        }

        frameId = legacyFrameId;
        packetId = legacyPacketCount;
        legacyPacketCount++;
    }

    bool isKeyFrame = (header.GetVideoFrameType() == 0);
    uint32_t frameSize = header.GetVideoFrameSize();
    uint32_t packetSize = packet->GetSize();

    // **Force minimum frame size for aggregate client**
    if (frameSize == 0 || frameSize < packetSize * 10) {
        frameSize = packetSize * 15; // Assume 15 packets per frame
    }

    SendAcknowledgment(from, header);

    Time currentTime = Simulator::Now();

    // Update last frame ID
    if (frameId > m_lastFrameId) {
        m_lastFrameId = frameId;
    }

    // Find or create frame
    if (m_incompleteFrames.find(frameId) == m_incompleteFrames.end()) {
        VideoFrame newFrame(frameId, isKeyFrame);
        newFrame.firstPacketTime = currentTime;
        newFrame.lastPacketTime = currentTime;
        newFrame.totalSize = 0;
        newFrame.complete = false;
        m_incompleteFrames[frameId] = newFrame;

        NS_LOG_DEBUG("Created frame " << frameId << " (" << (isKeyFrame ? "KEY" : "DELTA") << ")"
                    << " - expected size: " << frameSize);
    }

    VideoFrame& frame = m_incompleteFrames[frameId];

    // Add packet to frame
    VideoPacket vPacket(frameId, packetId, isKeyFrame, packetSize, currentTime);
    frame.packets.push_back(vPacket);
    frame.totalSize += packetSize;
    frame.lastPacketTime = currentTime;

    // **CRITICAL FIX: More aggressive frame completion for aggregate client**
    Time assemblyTime = currentTime - frame.firstPacketTime;

    // **SIMPLIFIED completion criteria**
    bool hasMinPackets = (frame.packets.size() >= 3); // Minimum 3 packets
    bool hasReasonableSize = (frame.totalSize >= frameSize * 0.3); // 30% of expected size
    bool hasTimeout = (assemblyTime >= MilliSeconds(50)); // 50ms timeout
    bool hasMaxPackets = (frame.packets.size() >= 20); // Max 20 packets per frame

    if ((hasMinPackets && hasReasonableSize) || hasTimeout || hasMaxPackets) {
        frame.complete = true;

        NS_LOG_INFO("Frame " << frameId << " completed: "
                   << frame.packets.size() << " packets, "
                   << frame.totalSize << " bytes, "
                   << assemblyTime.GetMilliSeconds() << "ms assembly"
                   << (hasTimeout ? " (TIMEOUT)" : "")
                   << (hasMaxPackets ? " (MAX_PACKETS)" : ""));

        // Add to buffer immediately
        m_frameBuffer.push_back(frame);
        m_incompleteFrames.erase(frameId);

        NS_LOG_INFO("Frame " << frameId << " added to buffer (buffer size: " << m_frameBuffer.size() << ")");
    }

    // Clean up old incomplete frames
    for (auto it = m_incompleteFrames.begin(); it != m_incompleteFrames.end(); ) {
        if (it->first < frameId - 3) { // Keep only last 3 frames
            NS_LOG_DEBUG("Cleaning up stale frame " << it->first);
            it = m_incompleteFrames.erase(it);
        } else {
            ++it;
        }
    }
}

void
VideoReceiver::SendAcknowledgment(Address from, const NadaHeader& originalHeader)
{
    NS_LOG_FUNCTION(this << from);

    if (!m_socket)
    {
        NS_LOG_ERROR("Cannot send ACK: socket is null");
        return;
    }

    try
    {
        // Create ACK packet with minimal size
        Ptr<Packet> ackPacket = Create<Packet>(64);

        // Create ACK header based on original packet
        NadaHeader ackHeader;
        ackHeader.SetSequenceNumber(originalHeader.GetSequenceNumber());
        ackHeader.SetTimestamp(Simulator::Now()); // Current time for RTT calculation
        ackHeader.SetVideoFrameType(originalHeader.GetVideoFrameType());
        ackHeader.SetVideoFrameSize(originalHeader.GetVideoFrameSize());

        ackPacket->AddHeader(ackHeader);

        // Send ACK back to sender
        int sent = m_socket->SendTo(ackPacket, 0, from);

        if (sent > 0)
        {
            NS_LOG_DEBUG("ACK sent for sequence " << originalHeader.GetSequenceNumber()
                        << " to " << InetSocketAddress::ConvertFrom(from).GetIpv4());
        }
        else
        {
            NS_LOG_WARN("Failed to send ACK for sequence " << originalHeader.GetSequenceNumber());
        }
    }
    catch (const std::exception& e)
    {
        NS_LOG_ERROR("Exception sending ACK: " << e.what());
    }
}

void
VideoReceiver::CheckRebuffering()
{
    NS_LOG_FUNCTION(this);

    // Check if we have enough frames to resume playback
    uint32_t minimumFrames = std::max(2U, m_frameRate / 10); // At least 100ms worth of frames

    if (m_frameBuffer.size() >= minimumFrames) {
        NS_LOG_INFO("Rebuffering complete, resuming playback with "
                   << m_frameBuffer.size() << " frames");
        // Resume normal consumption
        m_consumeEvent = Simulator::Schedule(m_frameInterval, &VideoReceiver::ConsumeFrame, this);
    } else {
        NS_LOG_INFO("Still rebuffering, need " << minimumFrames
                   << " frames, have " << m_frameBuffer.size());
        // Continue rebuffering
        m_consumeEvent = Simulator::Schedule(MilliSeconds(100), &VideoReceiver::CheckRebuffering, this);
    }
}

void
VideoReceiver::ConsumeFrame()
{
    NS_LOG_FUNCTION(this);

    if (m_frameBuffer.empty()) {
        m_bufferUnderruns++;
        NS_LOG_WARN("Buffer underrun #" << m_bufferUnderruns);

        // **FIX: Consistent rebuffering across simulations**
        Time rebufferTime = MilliSeconds(300); // Consistent rebuffer time

        m_consumeEvent = Simulator::Schedule(rebufferTime, &VideoReceiver::CheckRebuffering, this);
        return;
    }

    // Consume frame
    VideoFrame frame = m_frameBuffer.front();
    m_frameBuffer.pop_front();
    m_consumedFrames++;

    Time delay = Simulator::Now() - frame.firstPacketTime;
    NS_LOG_INFO("Consumed frame " << frame.frameId
               << ", buffer size: " << m_frameBuffer.size()
               << ", delay: " << delay.GetMilliSeconds() << "ms");

    // **FIX: Consistent consumption timing**
    Time nextInterval = m_frameInterval;

    // Buffer-aware consumption adjustment
    if (m_frameBuffer.size() < 2) {
        nextInterval = m_frameInterval * 1.2; // Slow down when buffer low
    } else if (m_frameBuffer.size() > 8) {
        nextInterval = m_frameInterval * 0.8; // Speed up when buffer high
    }

    m_consumeEvent = Simulator::Schedule(nextInterval, &VideoReceiver::ConsumeFrame, this);
}

void
VideoReceiver::RecordBufferState()
{
    NS_LOG_FUNCTION(this);

    // Record current buffer length in frames
    m_bufferLengthSamples.push_back(m_frameBuffer.size());

    // Schedule next recording
    m_statsEvent = Simulator::Schedule(MilliSeconds(100), &VideoReceiver::RecordBufferState, this);
}

std::string
VideoReceiver::GetBufferStats() const
{
    std::ostringstream oss;

    // Calculate average buffer length
    double avgBufferLength = 0;
    if (!m_bufferLengthSamples.empty())
    {
        for (uint32_t sample : m_bufferLengthSamples)
        {
            avgBufferLength += sample;
        }
        avgBufferLength /= m_bufferLengthSamples.size();
    }

    // Calculate buffer length in time
    double bufferLengthMs = avgBufferLength * m_frameInterval.GetMilliSeconds();

    oss << "Video Receiver Statistics:\n";
    oss << "  Frames received: " << m_consumedFrames + m_frameBuffer.size() << "\n";
    oss << "  Frames consumed: " << m_consumedFrames << "\n";
    oss << "  Current buffer: " << m_frameBuffer.size() << " frames\n";
    oss << "  Avg buffer: " << std::fixed << std::setprecision(2) << avgBufferLength
        << " frames (" << bufferLengthMs << " ms)\n";
    oss << "  Buffer underruns: " << m_bufferUnderruns << "\n";

    return oss.str();
}

uint32_t
VideoReceiver::GetBufferUnderruns() const
{
    return m_bufferUnderruns;
}

double
VideoReceiver::GetAverageBufferLength() const
{
    if (m_bufferLengthSamples.empty())
    {
        return 0.0;
    }

    double sum = 0;
    for (uint32_t length : m_bufferLengthSamples)
    {
        sum += length;
    }

    double avgFrames = sum / m_bufferLengthSamples.size();
    return avgFrames * m_frameInterval.GetMilliSeconds();
}

// VideoReceiverHelper implementation
VideoReceiverHelper::VideoReceiverHelper(uint16_t port)
{
    m_factory.SetTypeId(VideoReceiver::GetTypeId());
    m_factory.Set("Port", UintegerValue(port));
}

void
VideoReceiverHelper::SetAttribute(std::string name, const AttributeValue &value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
VideoReceiverHelper::Install(Ptr<Node> node) const
{
    ApplicationContainer apps;
    Ptr<VideoReceiver> app = m_factory.Create<VideoReceiver>();
    node->AddApplication(app);
    apps.Add(app);
    return apps;
}

ApplicationContainer
VideoReceiverHelper::Install(NodeContainer c) const
{
    ApplicationContainer apps;
    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
    {
        apps.Add(Install(*i));
    }
    return apps;
}

} // namespace ns3
