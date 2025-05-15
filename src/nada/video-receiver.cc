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

    // Extract frame information from header
    uint32_t frameId = header.GetSequenceNumber() / 1000; // Using the high bits for frame ID
    uint32_t packetId = header.GetSequenceNumber() % 1000; // Low bits for packet ID within frame
    bool isKeyFrame = (header.GetVideoFrameType() == 0); // 0 = KEY_FRAME in the enum
    uint32_t frameSize = header.GetVideoFrameSize();
    uint32_t packetSize = packet->GetSize();

    NS_LOG_DEBUG("Processing video packet: frameId=" << frameId
                << ", packetId=" << packetId
                << ", isKeyFrame=" << (isKeyFrame ? "true" : "false")
                << ", size=" << packetSize);

    // Update last frame ID if needed
    if (frameId > m_lastFrameId)
    {
        m_lastFrameId = frameId;
    }

    // Find or create the frame
    if (m_incompleteFrames.find(frameId) == m_incompleteFrames.end())
    {
        // Create a new frame and initialize all its properties
        VideoFrame newFrame(frameId, isKeyFrame);
        newFrame.firstPacketTime = Simulator::Now();
        newFrame.lastPacketTime = Simulator::Now();
        newFrame.totalSize = 0;
        newFrame.complete = false;

        m_incompleteFrames[frameId] = newFrame;
    }

    VideoFrame& frame = m_incompleteFrames[frameId];

    // Add packet to the frame
    VideoPacket vPacket(frameId, packetId, isKeyFrame, packetSize, Simulator::Now());
    frame.packets.push_back(vPacket);
    frame.totalSize += packetSize;
    frame.lastPacketTime = Simulator::Now();

    // Simple heuristic for frame completion:
    // If it's a key frame, we need more packets (average key frame is 1.5x larger)
    uint32_t expectedPackets = isKeyFrame ? 3 : 2;

    // If we have received enough packets and the total size is reasonable, mark as complete
    if (frame.packets.size() >= expectedPackets && frame.totalSize >= frameSize * 0.9)
    {
        frame.complete = true;

        // Move from incomplete to buffer
        m_frameBuffer.push_back(frame);
        m_incompleteFrames.erase(frameId);

        NS_LOG_INFO("Frame " << frameId << " completed and added to buffer, buffer size: "
                   << m_frameBuffer.size() << " frames");
    }

    // Clean up old incomplete frames (if they're way behind the latest frame)
    for (auto it = m_incompleteFrames.begin(); it != m_incompleteFrames.end(); )
    {
        if (m_lastFrameId - it->first > 10) // If more than 10 frames behind
        {
            NS_LOG_INFO("Discarding incomplete frame " << it->first << " (too old)");
            it = m_incompleteFrames.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void
VideoReceiver::ConsumeFrame()
{
    NS_LOG_FUNCTION(this);

    if (m_frameBuffer.empty())
    {
        // Buffer underrun!
        m_bufferUnderruns++;
        NS_LOG_WARN("Buffer underrun #" << m_bufferUnderruns << " at time "
                   << Simulator::Now().GetSeconds() << "s");
    }
    else
    {
        // Consume the oldest frame in the buffer
        VideoFrame frame = m_frameBuffer.front();
        m_frameBuffer.pop_front();
        m_consumedFrames++;

        Time delay = Simulator::Now() - frame.firstPacketTime;
        NS_LOG_INFO("Consumed frame " << frame.frameId
                   << " (key=" << (frame.isKeyFrame ? "yes" : "no")
                   << ", size=" << frame.totalSize << " bytes"
                   << ", delay=" << delay.GetMilliSeconds() << "ms"
                   << "), remaining buffer: " << m_frameBuffer.size() << " frames");
    }

    // Schedule next frame consumption
    m_consumeEvent = Simulator::Schedule(m_frameInterval, &VideoReceiver::ConsumeFrame, this);
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

    return sum / m_bufferLengthSamples.size() * m_frameInterval.GetMilliSeconds();
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
