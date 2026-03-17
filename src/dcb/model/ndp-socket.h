#ifndef NDP_SOCKET_H
#define NDP_SOCKET_H
 
#include "../utils/ndp-header.h"

#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-route.h"
#include "ns3/nstime.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"
 
#include <deque>
#include <map>
#include <vector>
 
 namespace ns3
 {
 
 class NdpL4Protocol;
 
 /**
  * \ingroup dcb
  * \brief Path health tracking for NDP multipath
  */
 struct NdpPathScore
 {
     uint32_t acks;     ///< Number of ACKs received on this path
     uint32_t nacks;    ///< Number of NACKs received on this path
     uint32_t losses;   ///< Number of losses on this path
     bool active;       ///< Whether this path is currently active
     Time lastProbe;    ///< Last time this path was probed
 
     NdpPathScore()
         : acks(0),
           nacks(0),
           losses(0),
           active(true),
           lastProbe(Seconds(0))
     {
     }
 
     double GetNackRatio() const
     {
         uint32_t total = acks + nacks;
         return total > 0 ? static_cast<double>(nacks) / total : 0.0;
     }
 };
 
 /**
  * \ingroup dcb
  * \brief Transmit buffer item for NDP
  */
struct NdpTxItem
{
    uint32_t seq;             ///< Sequence number
    Ptr<Packet> packet;       ///< Packet data
    Time sentTime;            ///< Time when packet was sent
    uint8_t pathId;           ///< Path ID used
    bool retransmitted;       ///< Whether this is a retransmission
    uint32_t rtoRetryCount;   ///< Number of times RTO has fired for this seq

    NdpTxItem()
        : seq(0), packet(nullptr), sentTime(Seconds(0)), pathId(0),
          retransmitted(false), rtoRetryCount(0)
    {
    }

    NdpTxItem(uint32_t s, Ptr<Packet> p, Time t, uint8_t pid)
        : seq(s), packet(p), sentTime(t), pathId(pid),
          retransmitted(false), rtoRetryCount(0)
    {
    }
};
 
 /**
  * \ingroup dcb
  * \brief NDP Socket
  *
  * Implements NDP transport protocol with:
  * - Push phase: first RTT blast
  * - Pull phase: receiver-driven rate control
  * - Per-packet multipath routing
  * - Trim-driven loss recovery
  * - Path health tracking
  */
 class NdpSocket : public Socket
 {
   public:
    // ── Socket state ─────────────────────────────────────────────────────────
    enum State
    {
        CLOSED,
        LISTEN,
        SYN_SENT,
        ESTABLISHED,
        CLOSE_WAIT,
        TIME_WAIT
    };

     /**
      * \brief Get the type ID.
      * \return the object TypeId
      */
     static TypeId GetTypeId();
 
     NdpSocket();
     virtual ~NdpSocket();
 
     /**
      * \brief Set the associated L4 protocol
      * \param ndp the NDP L4 protocol
      */
     void SetNdp(Ptr<NdpL4Protocol> ndp);
 
     // Socket interface implementation
     enum SocketErrno GetErrno() const override;
     enum SocketType GetSocketType() const override;
     Ptr<Node> GetNode() const override;
     int Bind() override;
     int Bind6() override;
     int Bind(const Address& address) override;
     int Close() override;
     int ShutdownSend() override;
     int ShutdownRecv() override;
     int Connect(const Address& address) override;
     int Listen() override;
     uint32_t GetTxAvailable() const override;
     int Send(Ptr<Packet> p, uint32_t flags) override;
     int SendTo(Ptr<Packet> p, uint32_t flags, const Address& address) override;
     Ptr<Packet> Recv(uint32_t maxSize, uint32_t flags) override;
     Ptr<Packet> RecvFrom(uint32_t maxSize, uint32_t flags, Address& fromAddress) override;
     uint32_t GetRxAvailable() const override;
     int GetSockName(Address& address) const override;
     int GetPeerName(Address& address) const override;
     bool SetAllowBroadcast(bool allowBroadcast) override;
     bool GetAllowBroadcast() const override;
 
     /**
      * \brief Notify application that a new flow-level connection was accepted.
      *
      * Wraps Socket::NotifyNewConnectionCreated (which is protected) so that
      * NdpL4Protocol can trigger the application's "new connection" callback
      * when it creates a per-flow accepted socket.
      *
      * \param accepted  The newly created per-flow socket
      * \param from      Remote address of the new flow
      */
     void NotifyAccepted(Ptr<Socket> accepted, const Address& from)
     {
         NotifyNewConnectionCreated(accepted, from);
     }
 
     /**
      * \brief Handle incoming packet
      * \param packet the received packet
      * \param header the IPv4 header
      * \param port the source port
      * \param incomingInterface the incoming interface
      */
     void ForwardUp(Ptr<Packet> packet,
                    Ipv4Header header,
                    uint16_t port,
                    Ptr<Ipv4Interface> incomingInterface);
 
     /**
      * \brief Set the available paths for multipath routing
      * \param paths vector of destination addresses (one per path)
      */
     void SetPaths(const std::vector<Ipv4Address>& paths);
 
    /**
     * \brief Get connection ID
     * \return connection ID
     */
    uint64_t GetConnectionId() const;

    // ── Methods called by NdpL4Protocol ──────────────────────────────────────

    /** Return current socket state (used by L4 to detect LISTEN sockets). */
    State GetState() const { return m_state; }

    /**
     * \brief Initialise this socket as an accepted receiver connection.
     *
     * Called by NdpL4Protocol::CreateAcceptedSocket().
     */
    void SetupAsReceiver(Ipv4Address localAddr,
                         uint16_t    localPort,
                         Ipv4Address remoteAddr,
                         uint16_t    remotePort,
                         uint64_t    connId,
                         uint32_t    firstSeq);

    /**
     * \brief Send one PULL (called by NdpL4Protocol global pull-pacing).
     */
    void SendOnePull();

    /**
     * \brief Complete connection establishment
     */
    void CompleteConnection();

    /**
     * \brief Set callback to be invoked when the flow is fully complete
     *        (all packets acknowledged by the receiver).
     *
     * This is the sender-side "all-ACKed" event.  It fires exactly once per
     * connection when ProcessAck() sees m_txBuffer empty && nextSeq >= lastSeq.
     *
     * \param cb  callback to invoke (no arguments)
     */
    void SetFlowCompleteCallback(Callback<void, Ptr<NdpSocket>> cb);

    /**
     * \brief Mark that the application has more data to submit later.
     *
     * While true, flow-completion detection in ProcessAck() is suppressed.
     * The application must call SetMoreDataPending(false) after the last
     * batch of Send() calls so that completion can fire normally.
     */
    void SetMoreDataPending(bool pending) { m_moreDataPending = pending; }

    /**
     * \brief Schedule RTO timer for a sent packet (1ms timeout per NDP spec)
     */
    void ScheduleRto(uint32_t seq);

    /**
     * \brief Cancel RTO timer for a seq (called when ACK/NACK received)
     */
    void CancelRto(uint32_t seq);

    /**
     * \brief RTO expiry handler - last-resort retransmission
     */
    void RtoExpired(uint32_t seq);

  protected:
    void DoDispose() override;
 
   private:
     /**
      * \brief Send first window (push phase)
      */
     void SendFirstWindow();
 
     /**
      * \brief Send a data packet
      * \param seq sequence number
      * \param isRetransmit whether this is a retransmission
      * \param avoidPath path to avoid (for retransmissions)
      */
     void SendDataPacket(uint32_t seq, bool isRetransmit = false, uint8_t avoidPath = 255);
 
     /**
      * \brief Handle received ACK
      * \param header NDP header
      */
     void ProcessAck(const NdpHeader& header);
 
     /**
      * \brief Handle received NACK
      * \param header NDP header
      */
     void ProcessNack(const NdpHeader& header);
 
     /**
      * \brief Handle received PULL
      * \param header NDP header
      */
     void ProcessPull(const NdpHeader& header);
 
     /**
      * \brief Handle received DATA
      * \param packet the data packet
      * \param header NDP header
      */
     void ProcessData(Ptr<Packet> packet, const NdpHeader& header);
 
     /**
      * \brief Send ACK for received data
      * \param seq sequence number to acknowledge
      */
     void SendAck(uint32_t seq);
 
     /**
      * \brief Send NACK for trimmed packet
      * \param seq sequence number to NACK
      */
     void SendNack(uint32_t seq);
 
     /**
      * \brief Send PULL request
      * \param pullSeq pull sequence number
      */
     void SendPull(uint32_t pullSeq);
 
     /**
      * \brief Select next path for sending
      * \param avoidPath path to avoid
      * \return path ID
      */
     uint8_t SelectPath(uint8_t avoidPath = 255);
 
     /**
      * \brief Update path health based on feedback
      * \param pathId path ID
      * \param isAck whether this is an ACK (true) or NACK (false)
      */
     void UpdatePathHealth(uint8_t pathId, bool isAck);
 
    /**
     * \brief Check and remove bad paths
     */
    void CheckPathHealth();

    /**
     * \brief Probe inactive paths to check if they have recovered
     */
    void ProbeInactivePaths();

    /**
     * \brief Send a probe packet on a specific path
     * \param pathId path ID to probe
     */
    void SendProbePacket(uint8_t pathId);
 
     State m_state;                       ///< Socket state
     uint64_t m_connectionId;             ///< Connection ID
     //[63:32] = 本地 IP 地址（32位）
     //[31:16] = 本地端口（16位）
     //[15:0]  = 远程端口（16位）
     Ipv4Address m_localAddress;          ///< Local address
     uint16_t m_localPort;                ///< Local port
     Ipv4Address m_remoteAddress;         ///< Remote address
     uint16_t m_remotePort;               ///< Remote port
     
     // Sender state
     uint32_t m_nextSeq;                  ///< Next sequence number to send
     uint32_t m_firstSeq;                 ///< First sequence number
     uint32_t m_lastSeq;                  ///< Last sequence number
     uint32_t m_firstWindowSize;          ///< First window size (packets) Push Phase 阶段一次性发送的包数量
     uint32_t m_lastPullSeq;              ///< Last received pull sequence 上一次收到的 PULL 序列号（累积式）
     bool m_inPushPhase;                  ///< Whether in push phase 
     
    std::deque<Ptr<Packet>> m_txQueue;        ///< Transmit queue 待发送的原始数据包队列
    std::deque<uint32_t> m_rtxQueue;          ///< Retransmission queue 只存储序列号（不是包本身）
    std::map<uint32_t, NdpTxItem> m_txBuffer; ///< Transmitted but unacked packets (Outstanding Table)
    std::map<uint32_t, EventId> m_rtoTimers;  ///< Per-seq RTO timers (1ms per NDP spec)
    uint32_t m_rtsCount;                      ///< Number of return-to-sender packets received
     
    // Receiver state
    uint32_t m_expectedSeq;              ///< Next expected sequence
    uint32_t m_pullSeq;                  ///< Per-flow pull sequence counter (incremented each SendOnePull)
    std::map<uint32_t, Ptr<Packet>> m_rxBuffer; ///< Out-of-order received packets <序列号,包数据>
    // NOTE: m_pullQueue and m_pullEvent have been removed.
    //       Pull credits are enqueued in NdpL4Protocol::m_globalPullQueue and
    //       paced by NdpL4Protocol::SendGlobalPulls().
     
    // Multipath state
    std::vector<Ipv4Address> m_paths;    ///< Available paths
    std::vector<uint8_t> m_pathOrder;    ///< Current path order
    uint32_t m_pathIndex;                ///< Current path index 当前使用的路径在 m_pathOrder 中的索引
    std::map<uint8_t, NdpPathScore> m_pathScores; ///< Path health scores
    Time m_pathProbeInterval;            ///< Interval for probing inactive paths
    EventId m_pathProbeEvent;            ///< Path probing event
    Ptr<UniformRandomVariable> m_rtoJitter; ///< RNG for RTO de-synchronization jitter
    double m_rtoMs{1.0};                    ///< RTO timeout in milliseconds (configurable via NdpSocket::RtoMs)
    
    // Protocol reference
     Ptr<NdpL4Protocol> m_ndp;            ///< NDP L4 protocol
     Ptr<Node> m_node;                    ///< Node
     
    // Callbacks
     TracedCallback<Ptr<const Packet>> m_txTrace; //发送数据包时触发的追踪回调
     TracedCallback<Ptr<const Packet>> m_rxTrace; //接收数据包时触发的追踪回调
     Callback<void, Ptr<NdpSocket>> m_flowCompleteCallback;  ///< fired once when all pkts ACK'd
     bool m_flowCompleted{false};              ///< guard: prevent duplicate completion
     bool m_moreDataPending{false};            ///< suppress completion until all app data submitted
     
     // Socket errno
     enum SocketErrno m_errno;

  public:
    // ── Per-flow statistics (mirrors RoCEv2Socket::Stats pattern) ────────
    class Stats
    {
      public:
        Stats();

        // ── Summary counters (always available, lightweight) ────────────
        uint32_t nTotalSizePkts{0};        ///< Original packets queued by app
        uint64_t nTotalSizeBytes{0};       ///< Original bytes queued by app
        uint32_t nTotalSentPkts{0};        ///< All pkts sent (first tx + retx)
        uint64_t nTotalSentBytes{0};       ///< All bytes sent (first tx + retx)
        uint32_t nRetxCount{0};            ///< Retransmission count
        uint32_t acksReceived{0};          ///< ACKs received
        uint32_t nacksReceived{0};         ///< NACKs received (trim notifications)
        uint32_t pullsConsumed{0};         ///< PULL credits consumed
        uint32_t rtoFires{0};              ///< Total RTO fires (watchdog + true-loss)
        uint32_t rtoTrueLoss{0};           ///< RTO true-loss fires

        Time tStart;                       ///< Flow start time
        Time tFinish;                      ///< Flow finish time
        Time tFct;                         ///< Flow completion time
        DataRate overallFlowRate;           ///< Overall flow rate (totalSizeBytes / FCT)

        std::string flowTag;               ///< Flow tag for identification

        // ── Detailed sender stats (only when bDetailedSenderStats=true) ──
        bool bDetailedSenderStats{false};
        std::vector<std::pair<Time, uint32_t>> vSentPkt;  ///< {time, sizeByte}

        // ── Detailed retx stats (only when bDetailedRetxStats=true) ──
        bool bDetailedRetxStats{false};
        std::vector<std::pair<Time, uint32_t>> vRecvAck;   ///< {time, seq}
        std::vector<std::pair<Time, uint32_t>> vRecvNack;  ///< {time, seq}

        // ── Record helpers (check flags internally) ────────────────────
        void RecordSentPkt(uint32_t size);
        void RecordRecvAck(uint32_t seq);
        void RecordRecvNack(uint32_t seq);

        void CollectAndCheck();
    };

    /** Get per-flow statistics (shared_ptr, mirrors RoCEv2Socket::GetStats()). */
    std::shared_ptr<Stats> GetStats() const;

  private:
    std::shared_ptr<Stats> m_stats;  ///< Per-flow statistics
};
 
 } // namespace ns3
 
 #endif /* NDP_SOCKET_H */
 