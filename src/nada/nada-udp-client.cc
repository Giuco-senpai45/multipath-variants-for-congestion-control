#include "nada-udp-client.h"

#include "nada-header.h"
#include "nada-improved.h"

#include "ns3/applications-module.h"
#include "ns3/log.h"
#include "ns3/names.h"
#include "ns3/udp-socket-factory.h"

namespace ns3
{
// UdpNadaClient implementation
NS_LOG_COMPONENT_DEFINE("UdpNadaClient");
NS_OBJECT_ENSURE_REGISTERED(UdpNadaClient);

TypeId
UdpNadaClient::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UdpNadaClient")
                            .SetParent<Application>()
                            .SetGroupName("Applications")
                            .AddConstructor<UdpNadaClient>()
                            .AddAttribute("PacketSize",
                                          "Size of packets generated",
                                          UintegerValue(1024),
                                          MakeUintegerAccessor(&UdpNadaClient::m_packetSize),
                                          MakeUintegerChecker<uint32_t>(1, 1500))
                            .AddAttribute("MaxPackets",
                                          "The maximum number of packets the app will send",
                                          UintegerValue(0),
                                          MakeUintegerAccessor(&UdpNadaClient::m_numPackets),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("Interval",
                                          "The time to wait between packets",
                                          TimeValue(Seconds(1.0)),
                                          MakeTimeAccessor(&UdpNadaClient::m_interval),
                                          MakeTimeChecker())
                            .AddAttribute("RemoteAddress",
                                          "The destination Address",
                                          AddressValue(),
                                          MakeAddressAccessor(&UdpNadaClient::m_peer),
                                          MakeAddressChecker());
    return tid;
}

UdpNadaClient::UdpNadaClient()
    : m_packetSize(1000),
      m_numPackets(0),
      m_interval(Seconds(0.05)),
      m_running(false),
      m_packetsSent(0),
      m_sequence(0),
      m_videoMode(false),
      m_currentFrameSize(0),
      m_currentFrameType(DELTA_FRAME),
      m_overheadRatio(1.0)
{
    NS_LOG_FUNCTION(this);
}

UdpNadaClient::~UdpNadaClient()
{
    NS_LOG_FUNCTION(this);
}

void
UdpNadaClient::SetRemote(Address ip, uint16_t port)
{
    NS_LOG_FUNCTION(this << ip << port);
    if (InetSocketAddress::IsMatchingType(ip))
    {
        m_peer = InetSocketAddress(InetSocketAddress::ConvertFrom(ip).GetIpv4(), port);
    }
    else if (Inet6SocketAddress::IsMatchingType(ip))
    {
        m_peer = Inet6SocketAddress(Inet6SocketAddress::ConvertFrom(ip).GetIpv6(), port);
    }
    else
    {
        // Direct IP address
        m_peer = InetSocketAddress(Ipv4Address::ConvertFrom(ip), port);
    }
}

void
UdpNadaClient::SetRemote(Address addr)
{
    NS_LOG_FUNCTION(this << addr);
    m_peer = addr;
}

void
UdpNadaClient::SetOverheadFactor(double factor)
{
    NS_LOG_FUNCTION(this << factor);
    m_overheadRatio = factor;
}

void
UdpNadaClient::SendPacket(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    if (!m_running || !m_socket)
    {
        NS_LOG_WARN("Cannot send packet: application not running or socket not created");
        return;
    }

    // Add NADA header with sequence number and timestamp
    NadaHeader header;
    header.SetSequenceNumber(m_sequence++);
    header.SetTimestamp(Simulator::Now());

    // Add media information if available
    if (m_videoMode)
    {
        header.SetVideoFrameSize(m_currentFrameSize);
        header.SetVideoFrameType(m_currentFrameType);
    }

    // Add protocol overhead ratio
    header.SetOverheadFactor(m_overheadRatio); // Use SetOverheadFactor instead of SetOverheadRatio

    packet->AddHeader(header);

    NS_LOG_INFO("Sending packet at " << Simulator::Now().GetSeconds()
                                     << " seq=" << header.GetSequenceNumber());
    m_socket->Send(packet);
    m_packetsSent++;

    // Record send time for RTT calculation
    m_sentPackets[header.GetSequenceNumber()] = Simulator::Now();
}

Ptr<Socket>
UdpNadaClient::GetSocket() const
{
    return m_socket;
}

void
UdpNadaClient::DoDispose(void)
{
    NS_LOG_FUNCTION(this);
    m_socket = 0;
    m_nada = 0;
    Application::DoDispose();
}

void
UdpNadaClient::SetVideoMode(bool enable)
{
    NS_LOG_FUNCTION(this << enable);
    m_videoMode = enable;

    // Also enable video mode in the NADA controller
    if (m_nada)
    {
        m_nada->SetVideoMode(enable);
    }

    if (enable)
    {
        // Initialize with reasonable defaults
        m_currentFrameSize = m_packetSize;
        m_currentFrameType = DELTA_FRAME;
        NS_LOG_INFO("Video mode enabled");
    }
}

void
UdpNadaClient::SetVideoFrameSize(uint32_t size)
{
    NS_LOG_FUNCTION(this << size);
    m_currentFrameSize = size;
}

void
UdpNadaClient::SetVideoFrameType(VideoFrameType type)
{
    NS_LOG_FUNCTION(this << type);
    m_currentFrameType = type;

    // If it's a key frame, notify the congestion controller
    if (type == KEY_FRAME && m_nada)
    {
        // For improved implementation, we can now use the proper method if available
        // in the improved NADA implementation
        // Convert from enum to uint8_t as expected by header
        m_currentFrameType = type;
    }
}

void
UdpNadaClient::SetOverheadRatio(double ratio)
{
    NS_LOG_FUNCTION(this << ratio);
    m_overheadRatio = ratio;
}

void
UdpNadaClient::StartApplication(void)
{
    NS_LOG_FUNCTION(this);

    // Create and configure UDP socket
    if (!m_socket)
    {
        TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
        m_socket = Socket::CreateSocket(GetNode(), tid);

        // Set socket options
        m_socket->SetAllowBroadcast(true);

        // Bind to any available port
        if (m_socket->Bind() == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket");
        }

        // Debug the peer address with more information
        if (InetSocketAddress::IsMatchingType(m_peer))
        {
            InetSocketAddress addr = InetSocketAddress::ConvertFrom(m_peer);
            NS_LOG_INFO("Attempting to connect to " << addr.GetIpv4() << ":" << addr.GetPort());

            // Check if the address is valid
            if (addr.GetIpv4() == Ipv4Address::GetZero())
            {
                NS_FATAL_ERROR("Invalid destination address (zero IP)");
            }
        }
        else
        {
            NS_LOG_INFO("Peer address is not an InetSocketAddress type");
        }

        // Try to connect with error handling
        int result = m_socket->Connect(m_peer);
        if (result == -1)
        {
            // Print detailed error information
            NS_LOG_ERROR("Failed to connect socket. Error code: " << m_socket->GetErrno());

            if (InetSocketAddress::IsMatchingType(m_peer))
            {
                InetSocketAddress addr = InetSocketAddress::ConvertFrom(m_peer);
                NS_LOG_ERROR("Destination: " << addr.GetIpv4() << ":" << addr.GetPort());
            }

            // Instead of terminating, try to recover if possible
            NS_LOG_WARN("Connection failed, but continuing execution");
            // Don't terminate with NS_FATAL_ERROR
        }
        else
        {
            NS_LOG_INFO("Successfully connected socket");
        }

        // Set up receive callback
        m_socket->SetRecvCallback(MakeCallback(&UdpNadaClient::HandleRead, this));
    }

    // Create and initialize NADA congestion control
    m_nada = CreateObject<NadaCongestionControl>();
    m_nada->Init(m_socket);

    // Start sending packets
    m_running = true;
    Send();
}

void
UdpNadaClient::StopApplication(void)
{
    NS_LOG_FUNCTION(this);
    m_running = false;

    if (m_sendEvent.IsPending())
    {
        Simulator::Cancel(m_sendEvent);
    }

    if (m_socket)
    {
        m_socket->Close();
    }
}

void
UdpNadaClient::Send(void)
{
    NS_LOG_FUNCTION(this);

    // Check if we've reached the maximum number of packets
    if (m_numPackets > 0 && m_packetsSent >= m_numPackets)
    {
        m_socket->Close();
        return;
    }

    // Create a packet of specified size
    Ptr<Packet> packet = Create<Packet>(m_packetSize);

    // Add sequence number and timestamp headers
    // This would be implemented in a real application

    // Send the packet
    NS_LOG_INFO("Sending packet at " << Simulator::Now().GetSeconds());
    m_socket->Send(packet);
    m_packetsSent++;

    // Get updated sending rate from NADA
    DataRate sendingRate = m_nada->UpdateRate();

    // Calculate next sending interval based on packet size and rate
    Time nextInterval = Seconds((m_packetSize * 8.0) / sendingRate.GetBitRate());

    NS_LOG_INFO("Next packet scheduled in " << nextInterval.GetSeconds() << "s (rate: "
                                            << sendingRate.GetBitRate() / 1000000.0 << " Mbps)");

    if (m_running)
    {
        m_sendEvent = Simulator::Schedule(nextInterval, &UdpNadaClient::Send, this);
    }
}

void
UdpNadaClient::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        // Extract header with all feedback information
        NadaHeader header;
        if (packet->RemoveHeader(header))
        {
            // Calculate round-trip time if this is an ACK for a packet we sent
            uint32_t seq = header.GetSequenceNumber();
            auto it = m_sentPackets.find(seq);

            if (it != m_sentPackets.end())
            {
                Time sendTime = it->second;
                Time rtt = Simulator::Now() - sendTime;

                // Clean up old entries to prevent memory growth
                m_sentPackets.erase(it);

                // Approximate one-way delay as RTT/2 (can be improved with clock sync)
                Time delay = rtt / 2;

                // Process receive rate and loss information
                double receiveRate = header.GetReceiveRate();
                double lossRate = header.GetLossRate();
                bool ecnMarked = header.GetEcnMarked();

                // Update NADA congestion control with complete feedback
                m_nada->ProcessAck(packet, delay);
                m_nada->ProcessLoss(lossRate);
                m_nada->ProcessEcn(ecnMarked);
                m_nada->UpdateReceiveRate(receiveRate);
            }
        }
    }
}

UdpNadaClientHelper::UdpNadaClientHelper(Address address, uint16_t port)
{
    m_factory.SetTypeId("ns3::UdpNadaClient");

    // Debug the address
    if (InetSocketAddress::IsMatchingType(address))
    {
        NS_LOG_INFO("Address is InetSocketAddress type");
        InetSocketAddress inetAddr = InetSocketAddress::ConvertFrom(address);
        NS_LOG_INFO("Setting up with IP: " << inetAddr.GetIpv4() << " port: " << port);

        // Make sure port is set correctly
        inetAddr.SetPort(port);
        SetAttribute("RemoteAddress", AddressValue(inetAddr));
    }
    else if (Ipv4Address::IsMatchingType(address))
    {
        NS_LOG_INFO("Address is Ipv4Address type");
        Ipv4Address ipv4 = Ipv4Address::ConvertFrom(address);
        NS_LOG_INFO("Setting up with IP: " << ipv4 << " port: " << port);

        InetSocketAddress inetAddr(ipv4, port);
        SetAttribute("RemoteAddress", AddressValue(inetAddr));
    }
    else
    {
        NS_LOG_WARN("Address is an unknown type - attempting generic conversion");
        InetSocketAddress inetAddr(Ipv4Address::ConvertFrom(address), port);
        SetAttribute("RemoteAddress", AddressValue(inetAddr));
    }
}

UdpNadaClientHelper::UdpNadaClientHelper(Address address)
{
    m_factory.SetTypeId("ns3::UdpNadaClient");
    SetAttribute("RemoteAddress", AddressValue(address));
}

void
UdpNadaClientHelper::SetAttribute(std::string name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
UdpNadaClientHelper::Install(Ptr<Node> node) const
{
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
UdpNadaClientHelper::Install(std::string nodeName) const
{
    Ptr<Node> node = Names::Find<Node>(nodeName);
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
UdpNadaClientHelper::Install(NodeContainer c) const
{
    ApplicationContainer apps;
    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
    {
        apps.Add(InstallPriv(*i));
    }
    return apps;
}

Ptr<Application>
UdpNadaClientHelper::InstallPriv(Ptr<Node> node) const
{
    Ptr<UdpNadaClient> app = m_factory.Create<UdpNadaClient>();
    node->AddApplication(app);
    return app;
}

} // namespace ns3
