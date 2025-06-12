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

    // Use default rate - will be set properly by SetInitialRate
    NS_LOG_INFO("NADA initialized with default rate: " << m_currentRate/1000000.0 << " Mbps");

    // Schedule periodic updates
    m_updateEvent = Simulator::Schedule(MilliSeconds(100),
                                       &NadaCongestionControl::PeriodicUpdate, this);
}

void
NadaCongestionControl::SetInitialRate(DataRate linkCapacity)
{
    NS_LOG_FUNCTION(this << linkCapacity);

    double linkBps = linkCapacity.GetBitRate();
    double initialRate;

    if (linkBps >= 1e9) { // 1Gbps+
        // Start at 25% for high-speed links
        initialRate = linkBps * 0.25;
        NS_LOG_INFO("High-speed link detected (" << linkBps/1e9
                   << " Gbps), starting NADA at " << initialRate/1e6 << " Mbps");
    } else if (linkBps >= 100e6) { // 100Mbps+
        // Start at 15%
        initialRate = linkBps * 0.15;
        NS_LOG_INFO("Medium-speed link detected (" << linkBps/1e6
                   << " Mbps), starting NADA at " << initialRate/1e6 << " Mbps");
    } else {
        // Start at 10%
        initialRate = linkBps * 0.1;
        NS_LOG_INFO("Standard link detected (" << linkBps/1e6
                   << " Mbps), starting NADA at " << initialRate/1e6 << " Mbps");
    }

    // Ensure bounds
    initialRate = std::max(initialRate, m_minRate);
    initialRate = std::min(initialRate, m_maxRate);

    m_currentRate = initialRate;

    // Update max rate based on link capacity (leave 5% headroom)
    m_maxRate = std::min(m_maxRate, linkBps * 0.95);

    NS_LOG_INFO("NADA initial rate set to: " << m_currentRate/1000000.0 << " Mbps"
               << ", max rate: " << m_maxRate/1000000.0 << " Mbps");
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

DataRate
NadaCongestionControl::UpdateRate()
{
    NS_LOG_FUNCTION(this);

    double score = CalculateScore();
    Time now = Simulator::Now();
    double deltaT = (now - m_lastUpdateTime).GetSeconds();
    m_lastUpdateTime = now;

    if (deltaT <= 0 || deltaT > 1.0) {
        deltaT = 0.1;
    }

    double newRate = m_currentRate;

    // Adaptive parameters based on network capacity
    double adaptiveGamma = m_gamma;
    double adaptiveBeta = m_beta;

    // For high-capacity networks, use more aggressive parameters
    if (m_maxRate >= 1e9) { // 1Gbps+
        adaptiveGamma = m_gamma * 5.0;  // 5x faster increase
        adaptiveBeta = m_beta * 1.2;    // Slightly more aggressive decrease

        // Early ramp-up phase: be very aggressive if we're far below capacity
        double utilizationRatio = m_currentRate / m_maxRate;
        if (utilizationRatio < 0.3 && score < 0.3) { // Low utilization, low congestion
            adaptiveGamma = m_gamma * 20.0; // 20x faster ramp-up
            NS_LOG_DEBUG("Fast ramp-up mode: utilization=" << utilizationRatio
                        << ", score=" << score);
        }
    } else if (m_maxRate >= 100e6) { // 100Mbps+
        adaptiveGamma = m_gamma * 2.0;  // 2x faster increase
    }

    // Apply rate control logic with adaptive parameters
    if (score < 0.1) {
        // Low congestion: increase rate
        double increaseRate = m_currentRate * adaptiveGamma * deltaT;

        // For high-speed links, allow larger jumps during ramp-up
        if (m_maxRate >= 1e9 && m_currentRate < m_maxRate * 0.5) {
            increaseRate = std::min(increaseRate, m_currentRate * 0.5); // Max 50% increase
        } else {
            increaseRate = std::min(increaseRate, m_currentRate * 0.1); // Max 10% increase
        }

        newRate = m_currentRate + increaseRate;
    }
    else if (score < 0.5) {
        // Moderate congestion: small adjustments
        double factor = 1.0 - (adaptiveBeta * 0.5) * score * deltaT;
        newRate = m_currentRate * factor;
    }
    else {
        // High congestion: decrease
        double factor = 1.0 - (adaptiveBeta * 1.5) * score * deltaT;
        newRate = m_currentRate * factor;

        if (score > 0.8) {
            newRate = std::min(newRate, m_currentRate * 0.8);
        }
    }

    // Apply bounds
    newRate = std::max(newRate, m_minRate);
    newRate = std::min(newRate, m_maxRate);

    // Adaptive smoothing based on network capacity
    double smoothingFactor = 0.3;
    if (m_maxRate >= 1e9) { // 1Gbps+
        smoothingFactor = 0.7; // Less smoothing for faster adaptation
    } else if (m_maxRate >= 100e6) { // 100Mbps+
        smoothingFactor = 0.5;
    }

    double oldRate = m_currentRate;
    m_currentRate = (1.0 - smoothingFactor) * m_currentRate + smoothingFactor * newRate;

    // Video-specific adaptations
    if (m_videoMode) {
        m_currentRate = ApplyVideoAdaptation(m_currentRate);
    }

    NS_LOG_DEBUG("NADA rate update: score=" << score
                << ", old=" << oldRate/1000000.0 << "Mbps"
                << ", new=" << m_currentRate/1000000.0 << "Mbps"
                << ", capacity=" << m_maxRate/1000000.0 << "Mbps");

    return DataRate(m_currentRate);
}

void
NadaCongestionControl::PeriodicUpdate()
{
    NS_LOG_FUNCTION(this);

    UpdateRate();

    // Adaptive update interval based on network capacity and current state
    Time updateInterval;

    if (m_maxRate >= 1e9) { // 1Gbps+
        // Fast updates during ramp-up phase
        double utilizationRatio = m_currentRate / m_maxRate;
        if (utilizationRatio < 0.5) {
            updateInterval = MilliSeconds(50); // 50ms during ramp-up
        } else {
            updateInterval = MilliSeconds(100); // 100ms during steady state
        }
    } else {
        // RFC recommends once per RTT or 100ms min
        updateInterval = std::max(m_rtt, MilliSeconds(100));
    }

    m_updateEvent = Simulator::Schedule(updateInterval,
                                       &NadaCongestionControl::PeriodicUpdate,
                                       this);
}

double
NadaCongestionControl::CalculateScore()
{
    double queueDelay = EstimateQueueingDelay();
    double referenceDelay = m_referenceDelay;

    // Enhanced congestion scoring with better sensitivity
    double xn = queueDelay / 0.100; // Normalize by 100ms reference

    double score = 0.0;

    // More responsive scoring function
    if (queueDelay <= referenceDelay) {
        // Very light congestion
        score = 0.1 * xn;
    } else if (queueDelay <= 2 * referenceDelay) {
        // Light to moderate congestion
        score = 0.1 + 0.3 * (xn - 0.1);
    } else {
        // Heavy congestion
        score = 0.4 + 0.6 * std::min(1.0, (xn - 0.2) / 0.8);
    }

    // Add loss penalty with better scaling
    if (m_lossRate > 0.0) {
        double lossPenalty = std::min(0.5, m_lossRate * 10.0); // Cap loss impact
        score += lossPenalty;
    }

    // Add ECN penalty if applicable
    if (m_ecnMarked) {
        score += 0.1; // Moderate penalty for ECN
        m_ecnMarked = false; // Reset ECN flag
    }

    // Ensure score is in valid range
    score = std::max(0.0, std::min(1.0, score));

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
