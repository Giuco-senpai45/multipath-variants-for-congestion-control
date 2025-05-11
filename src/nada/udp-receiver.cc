#include "udp-receiver.h"
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

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("UdpNadaReceiver");
NS_OBJECT_ENSURE_REGISTERED(UdpNadaReceiver);

TypeId
UdpNadaReceiver::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UdpNadaReceiver")
                            .SetParent<Application>()
                            .SetGroupName("Applications")
                            .AddConstructor<UdpNadaReceiver>()
                            .AddAttribute("Port",
                                          "Port on which we listen for incoming packets.",
                                          UintegerValue(9),
                                          MakeUintegerAccessor(&UdpNadaReceiver::m_port),
                                          MakeUintegerChecker<uint16_t>())
                            .AddAttribute("StatisticsInterval",
                                          "The time interval for processing statistics.",
                                          TimeValue(MilliSeconds(100)),
                                          MakeTimeAccessor(&UdpNadaReceiver::m_statInterval),
                                          MakeTimeChecker());
    return tid;
}

UdpNadaReceiver::UdpNadaReceiver()
    : m_port(9),
      m_statInterval(MilliSeconds(100))
{
    NS_LOG_FUNCTION(this);
}

UdpNadaReceiver::~UdpNadaReceiver()
{
    NS_LOG_FUNCTION(this);
}

void
UdpNadaReceiver::DoDispose(void)
{
    NS_LOG_FUNCTION(this);
    m_socket = 0;
    Application::DoDispose();
}

void
UdpNadaReceiver::StartApplication(void)
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

    m_socket->SetRecvCallback(MakeCallback(&UdpNadaReceiver::HandleRead, this));

    // Schedule statistics processing
    m_statisticsEvent =
        Simulator::Schedule(m_statInterval, &UdpNadaReceiver::ProcessStatistics, this);
}

void
UdpNadaReceiver::StopApplication(void)
{
    NS_LOG_FUNCTION(this);

    if (m_socket)
    {
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    }

    if (m_statisticsEvent.IsPending())
    {
        Simulator::Cancel(m_statisticsEvent);
    }
}

void
UdpNadaReceiver::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        // Extract NADA header
        NadaHeader header;
        if (packet->PeekHeader(header) == 0)
        {
            NS_LOG_WARN("Received packet without NADA header");
            continue;
        }

        uint32_t flowId = 0;

        // Create a simple flow ID from the address
        if (InetSocketAddress::IsMatchingType(from))
        {
            InetSocketAddress addr = InetSocketAddress::ConvertFrom(from);
            flowId = addr.GetIpv4().Get();
        }
        else if (Inet6SocketAddress::IsMatchingType(from))
        {
            // For IPv6 we'd need a better hash function
            flowId = 1; // Placeholder
        }

        // Create flow stats if not exist
        if (m_flows.find(flowId) == m_flows.end())
        {
            NS_LOG_INFO("New flow detected, id: " << flowId);
            FlowStats stats;
            stats.peerAddress = from;
            stats.lastSeq = 0;
            stats.receivedBytes = 0;
            stats.lastReceiveTime = Simulator::Now();
            stats.lossRate = 0.0;
            stats.receiveRate = 0.0;
            stats.delayGradient = 0.0;
            stats.ecnMarked = false;
            stats.feedbackSocket = nullptr;
            m_flows[flowId] = stats;
        }

        // Update flow statistics
        FlowStats& stats = m_flows[flowId];

        // Extract sequence number
        uint32_t seq = header.GetSequenceNumber();
        stats.seqList.push_back(seq);
        if (stats.seqList.size() > 100)
        {
            stats.seqList.pop_front();
        }

        // Record arrival time
        stats.arrivalTimes[seq] = Simulator::Now();

        // Trim old arrival times (keep last 1000 packets)
        while (stats.arrivalTimes.size() > 1000)
        {
            auto oldest = stats.arrivalTimes.begin();
            stats.arrivalTimes.erase(oldest);
        }

        // Update byte count
        stats.receivedBytes += packet->GetSize();

        // Check for ECN marking (could be from IP header in real implementation)
        bool ecnMarked = header.GetEcnMarked();
        if (ecnMarked)
        {
            stats.ecnMarked = true;
        }

        // Calculate receive rate
        Time now = Simulator::Now();
        double timeDelta = (now - stats.lastReceiveTime).GetSeconds();
        if (timeDelta > 0.001)
        { // At least 1ms since last update
            stats.receiveRate = (stats.receivedBytes * 8.0) / timeDelta; // bps
            stats.receivedBytes = 0;
            stats.lastReceiveTime = now;
        }

        // Send immediate feedback for this packet
        // Note: RFC 8698 recommends feedback at regular intervals,
        // but immediate feedback can be useful for testing
        // SendFeedback(flowId);
    }
}

void
UdpNadaReceiver::ProcessStatistics()
{
    NS_LOG_FUNCTION(this);

    // Process each active flow
    for (auto& flow : m_flows)
    {
        uint32_t flowId = flow.first;
        FlowStats& stats = flow.second;

        // Update loss rate
        stats.lossRate = CalculateLossRate(flowId);

        // Update receive rate (already calculated in HandleRead, but can refine here)
        stats.receiveRate = CalculateReceiveRate(flowId);

        // Calculate delay gradient using packet inter-arrival times
        if (stats.arrivalTimes.size() >= 3)
        {
            std::vector<double> delayValues;
            std::vector<uint32_t> seqs;

            // Get sorted sequence numbers
            for (const auto& entry : stats.arrivalTimes)
            {
                seqs.push_back(entry.first);
            }
            std::sort(seqs.begin(), seqs.end());

            // Get the last 5 entries or fewer if not available
            size_t start = seqs.size() > 5 ? seqs.size() - 5 : 0;

            // Extract timing information
            for (size_t i = start; i < seqs.size(); i++)
            {
                uint32_t seq = seqs[i];
                // Calculate queuing delay (would need one-way delay in real implementation)
                // Here we use inter-arrival variation as proxy
                if (i > start)
                {
                    Time current = stats.arrivalTimes[seq];
                    Time prev = stats.arrivalTimes[seqs[i - 1]];
                    Time delta = current - prev;

                    // Convert to seconds and store
                    delayValues.push_back(delta.GetSeconds());
                }
            }

            // Calculate gradient (simple linear regression)
            if (delayValues.size() >= 2)
            {
                double sumX = 0;
                double sumY = 0;
                double sumXY = 0;
                double sumX2 = 0;

                for (size_t i = 0; i < delayValues.size(); i++)
                {
                    double x = i;
                    double y = delayValues[i];

                    sumX += x;
                    sumY += y;
                    sumXY += x * y;
                    sumX2 += x * x;
                }

                int n = delayValues.size();
                double denominator = n * sumX2 - sumX * sumX;

                if (std::abs(denominator) > 1e-10)
                {
                    double gradient = (n * sumXY - sumX * sumY) / denominator;

                    // Apply EWMA to smooth gradient
                    const double alpha = 0.3; // EWMA factor
                    stats.delayGradient = alpha * gradient + (1 - alpha) * stats.delayGradient;

                    NS_LOG_DEBUG("Flow " << flowId << " delay gradient: " << stats.delayGradient);
                }
            }
        }

        // Send feedback
        SendFeedback(flowId);

        // Reset ECN marking for next interval
        stats.ecnMarked = false;
    }

    // Schedule next statistics processing
    m_statisticsEvent =
        Simulator::Schedule(m_statInterval, &UdpNadaReceiver::ProcessStatistics, this);
}

double
UdpNadaReceiver::CalculateLossRate(uint32_t flowId)
{
    NS_LOG_FUNCTION(this << flowId);

    FlowStats& stats = m_flows[flowId];

    // Simple loss detection by looking at sequence gaps
    if (stats.seqList.size() < 2)
    {
        return 0.0;
    }

    // Sort sequence numbers
    std::vector<uint32_t> sortedSeq(stats.seqList.begin(), stats.seqList.end());
    std::sort(sortedSeq.begin(), sortedSeq.end());

    // Count gaps
    uint32_t gaps = 0;
    uint32_t expected = sortedSeq.back() - sortedSeq.front() + 1;
    uint32_t received = sortedSeq.size();

    if (expected > received)
    {
        gaps = expected - received;
    }

    // Calculate loss rate
    double lossRate = (double)gaps / expected;

    NS_LOG_DEBUG("Flow " << flowId << " loss rate: " << lossRate << " (gaps: " << gaps
                         << ", expected: " << expected << ", received: " << received << ")");

    return lossRate;
}

double
UdpNadaReceiver::CalculateReceiveRate(uint32_t flowId)
{
    NS_LOG_FUNCTION(this << flowId);

    // We already calculated this in HandleRead
    return m_flows[flowId].receiveRate;
}

void
UdpNadaReceiver::SendFeedback(uint32_t flowId)
{
    NS_LOG_FUNCTION(this << flowId);

    FlowStats& stats = m_flows[flowId];

    // Create feedback packet
    Ptr<Packet> packet = Create<Packet>();

    // Create NADA header with feedback information
    NadaHeader header;
    header.SetReceiveTimestamp(Simulator::Now());
    header.SetReceiveRate(stats.receiveRate);
    header.SetLossRate(stats.lossRate);
    header.SetEcnMarked(stats.ecnMarked);
    header.SetDelayGradient(stats.delayGradient);

    // Calculate inter-packet delay variation based on arrival times
    if (stats.arrivalTimes.size() >= 2)
    {
        // Get last two sequence numbers (sorted)
        std::vector<uint32_t> seqs;
        for (const auto& entry : stats.arrivalTimes)
        {
            seqs.push_back(entry.first);
        }
        std::sort(seqs.begin(), seqs.end());

        if (seqs.size() >= 2)
        {
            uint32_t lastSeq = seqs[seqs.size() - 1];
            uint32_t prevSeq = seqs[seqs.size() - 2];

            // Calculate inter-arrival time
            Time lastArrival = stats.arrivalTimes[lastSeq];
            Time prevArrival = stats.arrivalTimes[prevSeq];
            Time interArrival = lastArrival - prevArrival;

            // Set reference packet spacing
            // In practice, this would be based on sender's rate
            // For now, use a fixed reference of 5ms (200 pkts/sec)
            const Time referenceSpacing = MilliSeconds(5);

            // Calculate arrival time offset as percentage of reference
            int64_t offset = (interArrival - referenceSpacing).GetNanoSeconds();
            header.SetArrivalTimeOffset(offset);

            NS_LOG_DEBUG("Flow " << flowId
                                 << " inter-arrival time: " << interArrival.GetSeconds()
                                 << "s, offset: " << offset << "ns");
        }
    }

    // Add header to packet
    packet->AddHeader(header);

    // Create socket for feedback if it doesn't exist
    if (!stats.feedbackSocket)
    {
        stats.feedbackSocket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        stats.feedbackSocket->Bind();
    }

    // Send feedback to the sender
    int result = stats.feedbackSocket->SendTo(packet, 0, stats.peerAddress);
    if (result < 0)
    {
        NS_LOG_ERROR("Error sending feedback packet: " << result);
    }
    else
    {
        NS_LOG_INFO("Sent feedback packet to flow "
                    << flowId << " (size: " << packet->GetSize() << ", loss rate: "
                    << stats.lossRate << ", receive rate: " << stats.receiveRate << " bps)");
    }
}

// Implementation of UdpNadaReceiverHelper
UdpNadaReceiverHelper::UdpNadaReceiverHelper(uint16_t port)
{
    m_factory.SetTypeId(UdpNadaReceiver::GetTypeId());
    m_factory.Set("Port", UintegerValue(port));
}

void
UdpNadaReceiverHelper::SetAttribute(std::string name, const AttributeValue &value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
UdpNadaReceiverHelper::Install(Ptr<Node> node) const
{
    ApplicationContainer apps;
    Ptr<UdpNadaReceiver> app = m_factory.Create<UdpNadaReceiver>();
    node->AddApplication(app);
    apps.Add(app);
    return apps;
}

ApplicationContainer
UdpNadaReceiverHelper::Install(NodeContainer c) const
{
    ApplicationContainer apps;
    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
    {
        apps.Add(Install(*i));
    }
    return apps;
}

} // namespace ns3
