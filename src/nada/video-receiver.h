#ifndef VIDEO_RECEIVER_H
#define VIDEO_RECEIVER_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/address.h"
#include "ns3/socket.h"
#include "nada-header.h"

#include <queue>
#include <vector>
#include <deque>

namespace ns3 {

/**
 * \brief A receiver application for video streaming that includes buffer management
 *
 * This class extends UdpNadaReceiver to add video buffer management functionality.
 * It tracks received video packets, organizes them into frames, and simulates video
 * playback by consuming frames at regular intervals.
 */
class VideoReceiver : public Application
{
public:
  /**
   * \brief Get the TypeId
   *
   * \return The TypeId for this class
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Constructor
   */
  VideoReceiver ();

  /**
   * \brief Destructor
   */
  virtual ~VideoReceiver ();

  /**
   * \brief Structure to hold information about a video packet
   */
  struct VideoPacket {
    uint32_t frameId;       // Frame ID this packet belongs to
    uint32_t packetId;      // Packet ID within the frame
    bool isKeyFrame;        // Whether this packet is part of a key frame
    uint32_t size;          // Size of the packet
    Time arrivalTime;       // When the packet arrived

    VideoPacket(uint32_t fId, uint32_t pId, bool isKey, uint32_t s, Time t)
      : frameId(fId), packetId(pId), isKeyFrame(isKey), size(s), arrivalTime(t) {}
  };

  /**
   * \brief Structure to hold information about a video frame
   */
  struct VideoFrame {
    uint32_t frameId;           // Frame ID
    bool isKeyFrame;            // Whether this is a key frame
    uint32_t totalSize;         // Total size of the frame in bytes
    std::vector<VideoPacket> packets;  // Packets that make up this frame
    bool complete;              // Whether the frame is complete
    Time firstPacketTime;       // Arrival time of the first packet
    Time lastPacketTime;        // Arrival time of the last packet

    VideoFrame()
        : frameId(0),
          isKeyFrame(false),
          totalSize(0),
          packets(),
          complete(false),
          firstPacketTime(Seconds(0)),
          lastPacketTime(Seconds(0)) {}

    VideoFrame(uint32_t id, bool isKey)
        : frameId(id),
          isKeyFrame(isKey),
          totalSize(0),
          packets(),
          complete(false),
          firstPacketTime(Seconds(0)),
          lastPacketTime(Seconds(0)) {}
  };

  /**
   * \brief Set the frame rate
   *
   * \param frameRate The frame rate in frames per second
   */
  void SetFrameRate(uint32_t frameRate);

  /**
   * \brief Get statistics about buffer state
   *
   * \return A string containing buffer statistics
   */
  std::string GetBufferStats() const;

  /**
   * \brief Get the number of buffer underruns
   *
   * \return The number of buffer underruns
   */
  uint32_t GetBufferUnderruns() const;

  /**
   * \brief Get the average buffer length
   *
   * \return The average buffer length in milliseconds
   */
  double GetAverageBufferLength() const;

protected:
  virtual void DoDispose (void);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  /**
   * \brief Handle a packet reception
   *
   * \param socket The socket that received the packet
   */
  void HandleRead (Ptr<Socket> socket);

  /**
   * \brief Process a video packet
   *
   * \param packet The received packet
   * \param from The source address
   * \param header The NADA header
   */
  void ProcessVideoPacket (Ptr<Packet> packet, Address from, const NadaHeader& header);

  /**
   * \brief Consume a frame from the buffer
   */
  void ConsumeFrame ();

  /**
   * \brief Record buffer state for statistics
   */
  void RecordBufferState ();

  Ptr<Socket> m_socket;               ///< Socket for receiving
  Address m_localAddress;             ///< Local address to bind to
  uint16_t m_port;                    ///< Port to bind to

  uint32_t m_frameRate;               ///< Video frame rate (frames per second)
  Time m_frameInterval;               ///< Time between frame consumption

  std::map<uint32_t, VideoFrame> m_incompleteFrames;  ///< Frames being assembled
  std::deque<VideoFrame> m_frameBuffer;               ///< Complete frames ready for playback

  EventId m_consumeEvent;             ///< Event for consuming frames
  EventId m_statsEvent;               ///< Event for recording buffer statistics

  uint32_t m_lastFrameId;             ///< ID of the last received frame
  uint32_t m_consumedFrames;          ///< Number of frames consumed
  uint32_t m_bufferUnderruns;         ///< Number of buffer underruns

  std::vector<uint32_t> m_bufferLengthSamples;  ///< Samples of buffer length for statistics
};

/**
 * \brief Helper to create VideoReceiver applications
 */
class VideoReceiverHelper
{
public:
  /**
   * \brief Constructor
   *
   * \param port The port the receiver will listen on
   */
  VideoReceiverHelper (uint16_t port);

  /**
   * \brief Set an attribute for the video receiver
   *
   * \param name The name of the attribute
   * \param value The value of the attribute
   */
  void SetAttribute (std::string name, const AttributeValue &value);

  /**
   * \brief Install the video receiver on a node
   *
   * \param node The node to install on
   * \return The installed applications
   */
  ApplicationContainer Install (Ptr<Node> node) const;

  /**
   * \brief Install the video receiver on multiple nodes
   *
   * \param c The container of nodes to install on
   * \return The installed applications
   */
  ApplicationContainer Install (NodeContainer c) const;

private:
  ObjectFactory m_factory; ///< Factory for creating video receiver objects
};

} // namespace ns3

#endif /* VIDEO_RECEIVER_H */
