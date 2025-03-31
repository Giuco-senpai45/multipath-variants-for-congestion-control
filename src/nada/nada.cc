#include "nada.h"
#include "nada-udp-client.h"

#include "ns3/address-utils.h"
#include "ns3/address.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"


namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NadaCongestionControl");
NS_OBJECT_ENSURE_REGISTERED(NadaCongestionControl);

TypeId
NadaCongestionControl::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::NadaCongestionControl")
                            .SetParent<Object>()
                            .SetGroupName("Internet")
                            .AddConstructor<NadaCongestionControl>()
                            .AddAttribute("Alpha",
                                          "Smoothing factor for delay-based congestion detection",
                                          DoubleValue(0.1),
                                          MakeDoubleAccessor(&NadaCongestionControl::m_alpha),
                                          MakeDoubleChecker<double>(0.0, 1.0))
                            .AddAttribute("Beta",
                                          "Multiplicative decrease factor",
                                          DoubleValue(0.85),
                                          MakeDoubleAccessor(&NadaCongestionControl::m_beta),
                                          MakeDoubleChecker<double>(0.0, 1.0))
                            .AddAttribute("Gamma",
                                          "Additive increase factor",
                                          DoubleValue(0.005),
                                          MakeDoubleAccessor(&NadaCongestionControl::m_gamma),
                                          MakeDoubleChecker<double>(0.0, 1.0))
                            .AddAttribute("Delta",
                                          "Multiplicative increase factor",
                                          DoubleValue(0.05),
                                          MakeDoubleAccessor(&NadaCongestionControl::m_delta),
                                          MakeDoubleChecker<double>(0.0, 1.0))
                            .AddAttribute("MinRate",
                                          "Minimum sending rate (bps)",
                                          DoubleValue(150000.0), // 150 kbps
                                          MakeDoubleAccessor(&NadaCongestionControl::m_minRate),
                                          MakeDoubleChecker<double>(0.0))
                            .AddAttribute("MaxRate",
                                          "Maximum sending rate (bps)",
                                          DoubleValue(20000000.0), // 20 Mbps
                                          MakeDoubleAccessor(&NadaCongestionControl::m_maxRate),
                                          MakeDoubleChecker<double>(0.0));
    return tid;
}

NadaCongestionControl::NadaCongestionControl()
    : m_alpha(0.1),
      m_beta(0.85),
      m_gamma(0.005),
      m_delta(0.05),
      m_minRate(150000.0),
      m_maxRate(20000000.0),
      m_currentRate(500000.0), // Start at 500kbps
      m_baseDelay(0.0),
      m_currentDelay(0.0),
      m_lossRate(0.0)
{
    NS_LOG_FUNCTION(this);
    m_rtt = MilliSeconds(100); // Default initial RTT estimate
    m_lastUpdateTime = Simulator::Now();
}

NadaCongestionControl::~NadaCongestionControl()
{
    NS_LOG_FUNCTION(this);
}

void
NadaCongestionControl::Init(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    m_socket = socket;

    // Schedule periodic updates
    m_updateEvent =
        Simulator::Schedule(MilliSeconds(100), &NadaCongestionControl::PeriodicUpdate, this);
}

void
NadaCongestionControl::ProcessAck(Ptr<Packet> ack, Time delay)
{
    NS_LOG_FUNCTION(this << ack << delay);

    // Update delay measurements
    m_currentDelay = delay.GetSeconds();
    m_baseDelay = EstimateBaseDelay(delay);

    // Update RTT estimate (simplified)
    m_rtt = delay * 2; // Assuming symmetric delays for simplicity
}

void
NadaCongestionControl::ProcessLoss(double lossRate)
{
    NS_LOG_FUNCTION(this << lossRate);
    m_lossRate = lossRate;
}

DataRate
NadaCongestionControl::UpdateRate()
{
    NS_LOG_FUNCTION(this);

    // Calculate current congestion score
    double score = CalculateScore();

    // Implement NADA's rate adaptation logic
    Time now = Simulator::Now();
    double deltaT = (now - m_lastUpdateTime).GetSeconds();
    m_lastUpdateTime = now;

    double newRate = m_currentRate;

    // NADA rate adaptation algorithm (based on RFC 8698)
    if (score > 0)
    {
        // Congestion detected - multiplicative decrease
        newRate = m_currentRate * (1.0 - m_beta * score * deltaT);
    }
    else
    {
        // No congestion - additive increase and/or multiplicative increase
        double gradientFactor = 1.0 + m_delta * (-score) * deltaT;
        newRate = m_currentRate * gradientFactor + m_gamma * deltaT;
    }

    // Enforce min/max bounds
    newRate = std::max(m_minRate, std::min(m_maxRate, newRate));

    NS_LOG_INFO("NADA rate update: " << m_currentRate << " -> " << newRate
                                     << " bps (score: " << score << ", delay: " << m_currentDelay
                                     << ", loss: " << m_lossRate << ")");

    m_currentRate = newRate;
    return DataRate(m_currentRate);
}

void
NadaCongestionControl::PeriodicUpdate()
{
    NS_LOG_FUNCTION(this);

    // Update sending rate
    DataRate newRate = UpdateRate();

    // Schedule next update
    m_updateEvent =
        Simulator::Schedule(MilliSeconds(100), &NadaCongestionControl::PeriodicUpdate, this);
}

double
NadaCongestionControl::CalculateScore()
{
    // NADA congestion score calculation (based on RFC 8698)
    double queueDelay = EstimateQueueingDelay();

    // Simplified score calculation
    double score = 0.0;

    // Calculate delay-based component
    if (queueDelay > 0.010)
    {                                          // 10ms threshold
        score += (queueDelay - 0.010) / 0.100; // Normalize by 100ms
    }

    // Add loss-based component
    if (m_lossRate > 0.0)
    {
        score += m_lossRate * 20.0; // Weight loss more heavily
    }

    return score;
}

double
NadaCongestionControl::EstimateBaseDelay(Time delay)
{
    double delayValue = delay.GetSeconds();

    // Simple baseline delay estimator (min-filter over time window)
    if (m_baseDelay == 0.0 || delayValue < m_baseDelay)
    {
        return delayValue;
    }

    // Slowly increase baseline over time to handle route changes
    return m_baseDelay * 0.99 + delayValue * 0.01;
}

double
NadaCongestionControl::EstimateQueueingDelay()
{
    // Queuing delay = current delay - base delay
    return std::max(0.0, m_currentDelay - m_baseDelay);
}

// NadaCongestionControlHelper implementation
NadaCongestionControlHelper::NadaCongestionControlHelper()
{
}

ApplicationContainer
NadaCongestionControlHelper::Install(Ptr<Node> node) const
{
    ApplicationContainer apps;

    Ptr<UdpNadaClient> app = CreateObject<UdpNadaClient>();
    node->AddApplication(app);
    apps.Add(app);

    Ptr<NadaCongestionControl> nada = CreateObject<NadaCongestionControl>();
    node->AggregateObject(nada);

    return apps;
}

} // namespace ns3
