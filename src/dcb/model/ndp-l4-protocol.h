#ifndef NDP_L4_PROTOCOL_H
#define NDP_L4_PROTOCOL_H

#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/ip-l4-protocol.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-interface.h"

#include <deque>
#include <map>
#include <vector>

namespace ns3
{

class NdpSocket;
class Packet;
class Node;

/**
 * \ingroup dcb
 * \brief NDP Layer 4 Protocol
 *
 * Responsibilities:
 *  - Socket creation and port management for HOST nodes only.
 *  - Routing incoming packets:
 *      DATA / TRIM / RTS → per-flow accepted socket (looked up by connId)
 *      PULL / ACK / NACK  → sender socket (looked up by local port)
 *      First SYN          → creates a new accepted socket and registers it.
 *  - Global Pull-Queue shared across ALL accepted sockets on this receiver.
 *    Pull credits are paced at the receiver's egress link rate.
 *
 * Switch nodes MUST NOT have NdpL4Protocol installed (they forward packets
 * purely at L2/L3 level via SwitchNode::SendIpv4Packet).
 */
class NdpL4Protocol : public IpL4Protocol
{
  public:
    static TypeId GetTypeId();
    static const uint8_t PROT_NUMBER;

   NdpL4Protocol();
   virtual ~NdpL4Protocol();

   void SetNode(Ptr<Node> node);
    Ptr<Node> GetNode() const;

    // ── IpL4Protocol interface ─────────────────────────────────────────────
    int GetProtocolNumber() const override;
    enum IpL4Protocol::RxStatus Receive(Ptr<Packet> p,
                                        const Ipv4Header& header,
                                        Ptr<Ipv4Interface> incomingInterface) override;
    enum IpL4Protocol::RxStatus Receive(Ptr<Packet> p,
                                        const Ipv6Header& header,
                                        Ptr<Ipv6Interface> incomingInterface) override;
    void SetDownTarget(IpL4Protocol::DownTargetCallback cb) override;
    void SetDownTarget6(IpL4Protocol::DownTargetCallback6 cb) override;
    IpL4Protocol::DownTargetCallback GetDownTarget() const override;
    IpL4Protocol::DownTargetCallback6 GetDownTarget6() const override;

    // ── Socket management ─────────────────────────────────────────────────
    /** Create a new NDP socket (used by sender / LISTEN applications). */
    Ptr<NdpSocket> CreateSocket();
    uint16_t AllocatePort();
    void DeAllocatePort(uint16_t port);
    void RegisterSocket(Ptr<NdpSocket> socket);
    void RegisterSocketWithPort(Ptr<NdpSocket> socket, uint16_t port);
    void UnregisterSocket(Ptr<NdpSocket> socket);

    /** Remove a per-flow accepted socket (called when the socket closes). */
    void UnregisterConnId(uint64_t connId);

    // ── Packet sending ─────────────────────────────────────────────────────
    void Send(Ptr<Packet> packet,
              Ipv4Address saddr,
              Ipv4Address daddr,
              uint16_t sport,
              uint16_t dport);

    // ── Global Pull Queue (shared across all accepted flows) ───────────────
   /**
     * \brief Enqueue one pull credit for a specific accepted socket.
     *
     * Called by NdpSocket::ProcessData() / ProcessTrim() instead of
     * pushing to a per-socket queue.  The L4 protocol paces pull
     * transmission at m_pullLinkRate.
     *
     * \param socket  The accepted NdpSocket that earned the pull credit.
    */
    void EnqueuePull(Ptr<NdpSocket> socket);

   /**
     * \brief Remove all pending pull credits for a socket.
     *
     * Called when a socket receives LAST (flow finished) or is disposed.
    */
    void CancelPullsForSocket(Ptr<NdpSocket> socket);

    /** Configure the egress link rate used for pull pacing. */
    void SetPullLinkRate(DataRate rate);
    DataRate GetPullLinkRate() const;

    /** Configure MTU (bytes) used to compute per-pull transmission time. */
    void SetPullMtu(uint32_t mtu);

  protected:
    void DoDispose() override;
    void NotifyNewAggregate() override;

  private:
    // ── Socket lookup helpers ──────────────────────────────────────────────
    Ptr<NdpSocket> FindSocketByPort(uint16_t port);
    Ptr<NdpSocket> FindSocketByConnId(uint64_t connId);

    /**
     * \brief Create an accepted socket for an incoming connection.
     *
     * Called the first time a SYN with a new connId arrives at a LISTEN
     * socket.  The new socket is pre-initialised in ESTABLISHED state so
     * it can process the accompanying first-window DATA immediately.
     */
    Ptr<NdpSocket> CreateAcceptedSocket(Ipv4Address localAddr,
                                        uint16_t localPort,
                                        Ipv4Address remoteAddr,
                                        uint16_t remotePort,
                                        uint64_t connId,
                                        uint32_t firstSeq);

    /** Paced pull-sender: dequeue one socket, send one PULL, reschedule. */
    void SendGlobalPulls();

    // ── Core state ────────────────────────────────────────────────────────
    Ptr<Node> m_node;

    /** Port → LISTEN / sender socket map (addressable by port number). */
    std::map<uint16_t, Ptr<NdpSocket>> m_sockets;
    std::vector<Ptr<NdpSocket>> m_socketList;
    uint16_t m_nextPort;

    /** connId → accepted (ESTABLISHED) socket for per-flow DATA routing. */
    std::map<uint64_t, Ptr<NdpSocket>> m_connSockets;
    
    IpL4Protocol::DownTargetCallback  m_downTarget;
    IpL4Protocol::DownTargetCallback6 m_downTarget6;

    // ── Global Pull Queue (Fair Round-Robin) ─────────────────────────────
    //
    // OLD design: flat FIFO deque — one entry per credit, same socket can
    // appear thousands of times before another socket gets a turn.
    // Problem: rich-get-richer starvation → only 74/301 flows complete.
    //
    // NEW design: per-socket credit counter + round-robin active list.
    //   m_pullCredits[s]   = outstanding PULL credits for socket s
    //   m_globalPullQueue  = ordered list of sockets that have ≥1 credit
    //                        (each socket appears AT MOST ONCE)
    //
    // EnqueuePull(s): m_pullCredits[s]++; if first credit → append s to queue.
    // SendGlobalPulls(): pop s from front; consume 1 credit; if credits remain
    //                    → append s to back (round-robin); send PULL to s.
    //
    // Result: every active flow gets exactly one PULL per round-robin cycle,
    //         guaranteeing max-min fair bandwidth allocation.
    std::deque<Ptr<NdpSocket>> m_globalPullQueue;  ///< round-robin active-socket list
    std::map<Ptr<NdpSocket>, uint32_t> m_pullCredits; ///< per-socket pending credits
    EventId  m_globalPullEvent;  ///< Pacing timer (one per receiver node)
    DataRate m_pullLinkRate;     ///< Egress link rate used for pacing
    uint32_t m_pullMtu;          ///< MTU bytes (packet size for timing)
};

} // namespace ns3

#endif /* NDP_L4_PROTOCOL_H */
