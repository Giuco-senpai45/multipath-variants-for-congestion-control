#ifndef NADA_IMPROVED_H
#define NADA_IMPROVED_H

#include "ns3/application.h"
#include "ns3/applications-module.h"
#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/inet-socket-address.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"

#include <deque>
#include <vector>

namespace ns3
{

/**
 * \ingroup internet
 * \brief NADA congestion control implementation following RFC 8698
 */
class NadaCongestionControl : public Object
{
  public:
    static TypeId GetTypeId(void);
    NadaCongestionControl();
    virtual ~NadaCongestionControl();

    /**
     * \brief Initialize with socket
     * \param socket The socket to use
     */
    void Init(Ptr<Socket> socket);

    /**
     * \brief Process an acknowledgment
     * \param ack The acknowledgment packet
     * \param delay The one-way delay estimate
     */
    void ProcessAck(Ptr<Packet> ack, Time delay);

    /**
     * \brief Process packet loss information
     * \param lossRate The reported loss rate
     */
    void ProcessLoss(double lossRate);

    /**
     * \brief Process ECN marking
     * \param ecnMarked Whether the packet was ECN marked
     */
    void ProcessEcn(bool ecnMarked);

    /**
     * \brief Update receive rate information
     * \param receiveRate The estimated receive rate
     */
    void UpdateReceiveRate(double receiveRate);

    /**
     * \brief Calculate and update the sending rate
     * \return The new sending rate
     */
    DataRate UpdateRate();

    /**
     * \brief Get the current sending rate
     * \return The current sending rate
     */
    DataRate GetCurrentRate() const
    {
        return DataRate(m_currentRate);
    }

    /**
     * \brief Enable or disable video adaptation mode
     * \param enable Whether to enable video adaptation
     */
    void SetVideoMode(bool enable);

    /**
     * \brief Set the minimum sending rate
     * \param rate The minimum rate
     */
    void SetMinRate(DataRate rate);

    /**
     * \brief Set the maximum sending rate
     * \param rate The maximum rate
     */
    void SetMaxRate(DataRate rate);

    /**
     * \brief Set the maximum RTT to consider
     * \param rtt The maximum RTT
     */
    void SetRttMax(Time rtt);

    /**
     * \brief Set the reference rate for congestion scoring
     * \param rate The reference rate
     */
    void SetXRef(DataRate rate);

    /**
     * \brief Get the current loss rate
     * \return The current loss rate
     */
    double GetLossRate() const
    {
        return m_lossRate;
    }

    /**
     * \brief Get the base delay estimate
     * \return The base delay estimate
     */
    Time GetBaseDelay() const
    {
        return Seconds(m_baseDelay);
    }

    /**
     * \brief Update with video frame information for better rate adaptation
     * \param frameSize Size of the current video frame
     * \param isKeyFrame Whether this is a key frame
     * \param frameInterval Time between frames
     */
    void UpdateVideoFrameInfo(uint32_t frameSize, bool isKeyFrame, Time frameInterval);

  private:
    /**
     * \brief Periodic update function called on a timer
     */
    void PeriodicUpdate();

    /**
     * \brief Calculate the congestion score according to RFC 8698
     * \return The congestion score
     */
    double CalculateScore();

    /**
     * \brief Estimate the baseline propagation delay
     * \param delay The current measured delay
     * \return The estimated baseline delay
     */
    double EstimateBaseDelay(Time delay);

    /**
     * \brief Estimate the queuing delay
     * \return The estimated queuing delay
     */
    double EstimateQueueingDelay();

    /**
     * \brief Calculate delay gradient (for delay trend detection)
     * \param currentDelay The current queuing delay
     * \return The delay gradient
     */
    double CalculateDelayGradient(double currentDelay);

    /**
     * \brief Apply video-specific adaptations to rate
     * \param baseRate The base calculated rate
     * \return The video-optimized rate
     */
    double ApplyVideoAdaptation(double baseRate);

    // RFC 8698 parameters
    double m_alpha;        // Smoothing factor
    double m_beta;         // Multiplicative decrease factor
    double m_gamma;        // Additive increase factor
    double m_delta;        // Multiplicative increase factor
    double m_minRate;      // Minimum sending rate (bps)
    double m_maxRate;      // Maximum sending rate (bps)
    double m_currentRate;  // Current sending rate (bps)
    double m_baseDelay;    // Baseline propagation delay (s)
    double m_currentDelay; // Current one-way delay (s)
    double m_lossRate;     // Packet loss rate
    Time m_rtt;            // Round-trip time estimate
    Time m_lastUpdateTime; // Last rate update time
    EventId m_updateEvent; // Event for periodic updates
    Ptr<Socket> m_socket;  // Socket to use

    // Enhanced RFC 8698 fields
    double m_referenceDelay;    // D_thr from RFC 8698
    double m_gamma0;            // Original gamma from RFC
    double m_queueDelayTarget;  // Target queue delay
    double m_ewmaFactor;        // EWMA smoothing factor
    double m_ewmaDelayGradient; // EWMA of delay gradient
    bool m_ecnMarked;           // ECN marking status
    double m_receiveRate;       // Estimated receive rate

    // Data structures for statistics and processing
    std::deque<double> m_delayWindow;    // Window for delay measurements
    std::deque<double> m_delayGradients; // Recent delay gradients
    std::deque<uint32_t> m_lossWindow;   // Window for loss measurements
    std::vector<double> m_rateHistory;   // History of sending rates

    // Video specific state
    bool m_videoMode;          // Whether video adaptation is active
    double m_lastKeyFrameTime; // Time of last key frame
    uint32_t m_frameSize;      // Current frame size

    // Statistics for debugging/evaluation
    struct Statistics
    {
        double minRtt;
        double maxRtt;
        double avgRtt;
        double stdDevRtt;
        uint32_t packetsSent;
        uint32_t packetsLost;
        double avgRate;
    } m_stats;
};

/**
 * \brief Helper to install NADA congestion control
 */
class NadaCongestionControlHelper
{
  public:
    NadaCongestionControlHelper();

    /**
     * \brief Install NADA on a node
     * \param node The node to install on
     * \return The created applications
     */
    ApplicationContainer Install(Ptr<Node> node) const;

    /**
     * \brief Install NADA on multiple nodes
     * \param c Container of nodes
     * \return The created applications
     */
    ApplicationContainer Install(NodeContainer c) const;
};

} // namespace ns3

#endif /* NADA_IMPROVED_H */
