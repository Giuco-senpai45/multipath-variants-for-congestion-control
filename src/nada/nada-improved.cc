#include "nada-improved.h"

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
    : m_alpha(0.1),            // Smoothing factor
      m_beta(0.5),             // Multiplicative decrease factor (proper RFC value)
      m_gamma(0.005),          // Additive increase factor (~10kbps per RTT at 200ms)
      m_delta(0.1),            // Multiplicative increase factor (modified from RFC)
      m_minRate(150000.0),     // 150kbps min rate
      m_maxRate(120000000.0),  // 120Mbps max rate (more realistic)
      m_currentRate(500000.0), // 500kbps starting rate
      m_baseDelay(0.0),
      m_currentDelay(0.0),
      m_lossRate(0.0),
      m_referenceDelay(0.010),   // 10ms reference delay
      m_gamma0(0.001),           // Original gamma from RFC
      m_queueDelayTarget(0.020), // 20ms target queue delay
      m_ewmaFactor(0.1),         // EWMA smoothing factor
      m_ewmaDelayGradient(0.0),   // Initial delay gradient
      m_videoMode(false),        // Video mode disabled by default
      m_lastKeyFrameTime(0.0),   // Initialize key frame time
      m_frameSize(0)             // Initialize frame size
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

    // Add more aggressive response to high loss rates
    if (lossRate > 0.2 && m_currentRate > m_minRate*2)
    {
        double oldRate = m_currentRate;
        m_currentRate = m_currentRate * 0.5;
        NS_LOG_INFO("Emergency rate reduction due to high loss: "
                   << oldRate << " -> " << m_currentRate << " bps (loss: " << lossRate << ")");
    }
}

// In NadaCongestionControl::UpdateRate() function
DataRate
NadaCongestionControl::UpdateRate()
{
    NS_LOG_FUNCTION(this);

    // Calculate current congestion score
    double score = CalculateScore();

    Time now = Simulator::Now();
    double deltaT = (now - m_lastUpdateTime).GetSeconds();
    m_lastUpdateTime = now;

    double newRate = m_currentRate;

    // Make NADA less aggressive
    if (score < 0.1)
    {
        // Reduce the ramp-up factor to be less aggressive
        double rampUpFactor = 1.0 + (m_delta * 0.5) * (0.1 - score) * deltaT;
        newRate = m_currentRate * rampUpFactor;
    }
    else if (score < 0.5)
    {
        // More conservative for light congestion
        double gradientFactor = 1.0 - (m_beta * 1.5) * (score - 0.1) * deltaT;
        newRate = m_currentRate * gradientFactor + m_alpha * deltaT;
    }
    else
    {
        // More aggressive reduction for heavy congestion
        double reductionFactor = 1.0 - (m_beta * 2) * (score - 0.1) * deltaT;
        newRate = m_currentRate * reductionFactor;
    }

    // Impose tighter bounds
    if (newRate > m_maxRate)
    {
        newRate = m_maxRate;
    }
    else if (newRate < m_minRate)
    {
        newRate = m_minRate;
    }

    // Set current rate
    m_currentRate = newRate;
    return DataRate(m_currentRate);
}

void
NadaCongestionControl::PeriodicUpdate()
{
    NS_LOG_FUNCTION(this);

    // Update sending rate
    DataRate newRate = UpdateRate();

    // Schedule next update - RFC recommends once per RTT or 100ms min
    Time updateInterval = std::max(m_rtt, MilliSeconds(100));
    m_updateEvent = Simulator::Schedule(updateInterval,
                                       &NadaCongestionControl::PeriodicUpdate,
                                       this);
}

double
NadaCongestionControl::CalculateScore()
{
    // RFC 8698 Section 4.1
    double queueDelay = EstimateQueueingDelay();
    double referenceDelay = 0.010; // 10ms threshold from RFC

    // Calculate x_n term (normalized queuing delay)
    double xn = queueDelay / 0.100; // normalized by reference of 100ms

    // RFC 8698 Section 4.1 congestion score function
    double score = 0.0;
    if (queueDelay <= referenceDelay)
    {
        // Gradual, non-zero congestion score for small delays
        score = 0.5 * xn;
    }
    else
    {
        // Steeper rise for delays above the threshold
        score = 0.5 + (xn - 0.1) * (1 - 0.5) / 0.9;
    }

    // Add loss penalty (kappa = 20.0 from the RFC)
    if (m_lossRate > 0.0)
    {
        score += m_lossRate * 20.0;
    }

    return score;
}

double
NadaCongestionControl::EstimateBaseDelay(Time delay)
{
    double delayValue = delay.GetSeconds();

    // Store recent delay samples in a window (implement as a circular buffer)
    m_delayWindow.push_back(delayValue);
    if (m_delayWindow.size() > 100)
    { // 10-second window with 100ms sampling
        m_delayWindow.pop_front();
    }

    // Find minimum in the window (min-filter approach)
    double minDelay = *std::min_element(m_delayWindow.begin(), m_delayWindow.end());

    if (m_baseDelay == 0.0)
    {
        return minDelay;
    }

    // Slow increase factor for route changes (RFC recommends 0.1/300 = 0.0003)
    double tau = 0.0003;
    return std::min(m_baseDelay * (1.0 + tau), minDelay);
}

double
NadaCongestionControl::EstimateQueueingDelay()
{
    // Queuing delay = current delay - base delay
    return std::max(0.0, m_currentDelay - m_baseDelay);
}

void
NadaCongestionControl::ProcessEcn(bool ecnMarked)
{
    NS_LOG_FUNCTION(this << ecnMarked);
    m_ecnMarked = ecnMarked;

    // RFC 8698 mentions that ECN should be treated as a loss signal
    if (ecnMarked) {
        // Apply similar penalty as would be used for losses
        // In practice, the penalty may be less severe than for actual losses
        const double ecnPenaltyFactor = 0.5; // ECN penalty is half that of loss
        ProcessLoss(0.05 * ecnPenaltyFactor); // Equivalent to 5% loss rate with reduced penalty
    }
}

void
NadaCongestionControl::UpdateReceiveRate(double receiveRate)
{
    NS_LOG_FUNCTION(this << receiveRate);
    m_receiveRate = receiveRate;

    // If receive rate is significantly lower than sending rate, it may indicate congestion
    if (receiveRate < m_currentRate * 0.8) {
        NS_LOG_INFO("Receive rate (" << receiveRate <<
                    ") significantly lower than send rate (" << m_currentRate <<
                    "), possible congestion");

        // Could adjust rate immediately, but RFC suggests using this as input to scoring
        // rather than directly modifying the rate
    }
}

double
NadaCongestionControl::CalculateDelayGradient(double currentDelay)
{
    // Store delay values for gradient calculation
    m_delayGradients.push_back(currentDelay);
    if (m_delayGradients.size() > 5) { // Keep last 5 samples
        m_delayGradients.pop_front();
    }

    if (m_delayGradients.size() < 2) {
        return 0.0;
    }

    // Simple linear regression to estimate gradient
    double sumX = 0;
    double sumY = 0;
    double sumXY = 0;
    double sumX2 = 0;
    int n = m_delayGradients.size();

    for (int i = 0; i < n; i++) {
        double x = i;
        double y = m_delayGradients[i];

        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
    }

    double denominator = n * sumX2 - sumX * sumX;
    if (std::abs(denominator) < 1e-10) {
        return 0.0; // Avoid division by zero
    }

    double gradient = (n * sumXY - sumX * sumY) / denominator;

    // Update EWMA of delay gradient
    m_ewmaDelayGradient = m_ewmaFactor * gradient + (1 - m_ewmaFactor) * m_ewmaDelayGradient;

    return m_ewmaDelayGradient;
}

double
NadaCongestionControl::ApplyVideoAdaptation(double baseRate)
{
    if (!m_videoMode) {
        return baseRate;
    }

    // Video rate adaptation logic following RFC 8698 Section 6

    // 1. Rate shaping around key frames
    double timeSinceKeyFrame = Simulator::Now().GetSeconds() - m_lastKeyFrameTime;
    if (timeSinceKeyFrame < 0.100) { // Within 100ms of key frame
        // Allow temporary spike for key frames
        return std::min(baseRate * 1.5, m_maxRate);
    }

    // 2. Match video rate tiers - find closest viable encoding rate
    // This is simplified - a real implementation would have actual encoding rates
    const std::vector<double> encodingRates = {
        500000,   // 500 kbps
        1000000,  // 1 Mbps
        2000000,  // 2 Mbps
        3500000,  // 3.5 Mbps
        5000000,  // 5 Mbps
        8000000,  // 8 Mbps
        12000000  // 12 Mbps
    };

    // Find the highest encoding rate below our calculated rate
    double videoRate = m_minRate; // Default to minimum
    for (auto rate : encodingRates) {
        if (rate <= baseRate) {
            videoRate = rate;
        } else {
            break;
        }
    }

    // Add stability logic - don't change rates too frequently
    // Store rate decision in history
    m_rateHistory.push_back(videoRate);
    if (m_rateHistory.size() > 10) { // Keep last 10 decisions
        m_rateHistory.erase(m_rateHistory.begin());
    }

    // Only change rate if we've had the same decision multiple times
    if (m_rateHistory.size() >= 3) {
        // Count occurrences of the latest rate
        int count = 0;
        for (size_t i = m_rateHistory.size() - 3; i < m_rateHistory.size(); i++) {
            if (std::abs(m_rateHistory[i] - videoRate) < 1e-6) {
                count++;
            }
        }

        // If we haven't consistently decided on this rate, use previous rate
        if (count < 2 && !m_rateHistory.empty()) {
            videoRate = m_rateHistory[m_rateHistory.size() - 1];
        }
    }

    return videoRate;
}

void
NadaCongestionControl::SetVideoMode(bool enable)
{
    NS_LOG_FUNCTION(this << enable);
    m_videoMode = enable;

    if (enable) {
        // Initialize video-specific parameters
        m_lastKeyFrameTime = Simulator::Now().GetSeconds();
        m_frameSize = 0;

        // When enabling video mode, we should also adjust our update method
        // to use the video rate adaptation logic

        // Cancel any pending update event to restart with new timing
        if (m_updateEvent.IsPending()) {
            Simulator::Cancel(m_updateEvent);
        }

        // Schedule updates at frame-rate frequency (e.g., 30fps = 33.3ms)
        Time frameInterval = MilliSeconds(33);
        m_updateEvent = Simulator::Schedule(frameInterval,
                                           &NadaCongestionControl::PeriodicUpdate,
                                           this);

        NS_LOG_INFO("Video mode enabled with " << frameInterval.GetMilliSeconds()
                    << "ms update interval");
    }
}

void
NadaCongestionControl::UpdateVideoFrameInfo(uint32_t frameSize, bool isKeyFrame, Time frameInterval)
{
    NS_LOG_FUNCTION(this << frameSize << isKeyFrame << frameInterval);

    // Store frame information
    m_frameSize = frameSize;

    if (isKeyFrame) {
        m_lastKeyFrameTime = Simulator::Now().GetSeconds();
    }

    // Calculate target bitrate based on frame rate and size
    double frameRate = 1.0 / frameInterval.GetSeconds();
    double targetBitrate = frameSize * 8 * frameRate;

    // Adjust rate change speed based on how far we are from target
    if (m_videoMode) {
        double currentBitrate = m_currentRate;
        double ratio = currentBitrate / targetBitrate;

        // If our current rate is way off from what's needed for the video,
        // we can adjust more aggressively in the next rate update
        if (ratio < 0.8 || ratio > 1.2) {
            NS_LOG_INFO("Video bitrate mismatch - current: " << currentBitrate
                        << ", target for video: " << targetBitrate
                        << ", ratio: " << ratio);

            // Don't change rate here, but let UpdateRate know about the mismatch
            // by adjusting the parameters used in rate calculation
            if (ratio < 0.8) {
                // We're sending too little - increase ramp up speed temporarily
                m_delta *= 1.2;  // Temporary boost to delta
            } else if (ratio > 1.2) {
                // We're sending too much - decrease more aggressively temporarily
                m_beta *= 1.1;   // Temporary boost to beta
            }
        } else {
            // Reset to normal parameters
            m_delta = 0.05;  // Default delta
            m_beta = 0.5;   // Default beta
        }
    }
}

void
NadaCongestionControl::SetMinRate(DataRate rate)
{
    NS_LOG_FUNCTION(this << rate);
    m_minRate = rate.GetBitRate();

    // Ensure current rate respects the new minimum
    if (m_currentRate < m_minRate)
    {
        m_currentRate = m_minRate;
        NS_LOG_INFO("Current rate increased to match new minimum: " << m_currentRate << " bps");
    }
}

void
NadaCongestionControl::SetMaxRate(DataRate rate)
{
    NS_LOG_FUNCTION(this << rate);
    m_maxRate = rate.GetBitRate();

    // Ensure current rate respects the new maximum
    if (m_currentRate > m_maxRate)
    {
        m_currentRate = m_maxRate;
        NS_LOG_INFO("Current rate decreased to match new maximum: " << m_currentRate << " bps");
    }
}

void
NadaCongestionControl::SetRttMax(Time rtt)
{
    NS_LOG_FUNCTION(this << rtt);
    // Store the maximum RTT to consider in congestion scoring
    // This affects how delay measurements are weighted
    m_queueDelayTarget = rtt.GetSeconds() / 4.0; // RFC suggests target = max_rtt/4
    NS_LOG_INFO("Setting queue delay target to: " << m_queueDelayTarget << " seconds");
}

void
NadaCongestionControl::SetXRef(DataRate rate)
{
    NS_LOG_FUNCTION(this << rate);
    // Set the reference rate for congestion scoring
    // This is the target operating point (x_ref in RFC 8698)
    m_referenceDelay = rate.GetBitRate() / m_maxRate; // Normalize to [0,1] range
    NS_LOG_INFO("Setting reference delay to: " << m_referenceDelay);
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
