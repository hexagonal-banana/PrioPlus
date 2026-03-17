#ifndef NDP_HEADER_H
#define NDP_HEADER_H

#include "ns3/header.h"

namespace ns3
{

/**
 * \ingroup dcb
 * \brief NDP packet header
 *
 * NDP header contains:
 * - conn_id: Connection identifier
 * - seq: Sequence number
 * - pull_seq: Pull sequence number (for PULL packets)
 * - flags: SYN | ACK | NACK | PULL | TRIM | LAST
 * - path_id: Path identifier for multipath routing
 * - seq_offset: Offset for first RTT packets (only in SYN packets)
 */
class NdpHeader : public Header
{
  public:
    /**
     * \brief NDP packet flags
     */
    enum Flags : uint8_t
    {
        NONE = 0x00,
        SYN = 0x01,    // Connection establishment (first RTT)
        ACK = 0x02,    // Acknowledgment
        NACK = 0x04,   // Negative acknowledgment (trim notification)
        PULL = 0x08,   // Pull request from receiver
        TRIM = 0x10,   // Packet has been trimmed at switch
        LAST = 0x20,   // Last packet in connection
        RTS = 0x40,    // Return-to-Sender (trimmed packet returned by switch)
    };

    NdpHeader();
    virtual ~NdpHeader();

    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    void Print(std::ostream& os) const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;

    // Setters
    void SetConnectionId(uint64_t connId);
    void SetSequence(uint32_t seq);
    void SetPullSequence(uint32_t pullSeq);
    void SetFlags(uint8_t flags);
    void SetPathId(uint8_t pathId);
    void SetSeqOffset(uint32_t seqOffset);

    // Getters
    uint64_t GetConnectionId() const;
    uint32_t GetSequence() const;
    uint32_t GetPullSequence() const;
    uint8_t GetFlags() const;
    uint8_t GetPathId() const;
    uint32_t GetSeqOffset() const;

    // Flag checking helpers
    bool HasFlag(Flags flag) const;
    void AddFlag(Flags flag);
    void RemoveFlag(Flags flag);
    bool IsSyn() const;
    bool IsAck() const;
    bool IsNack() const;
    bool IsPull() const;
    bool IsTrim() const;
    bool IsLast() const;
    bool IsRts() const;  // Check if this is a Return-to-Sender packet
    bool IsData() const; // Not control packet

  private:
    uint64_t m_connId;      // Connection ID (8 bytes)
    uint32_t m_seq;         // Sequence number (4 bytes)
    uint32_t m_pullSeq;     // Pull sequence number (4 bytes)
    uint8_t m_flags;        // Flags (1 byte)
    uint8_t m_pathId;       // Path ID (1 byte)
    uint32_t m_seqOffset;   // Sequence offset for first RTT (4 bytes)
};

} // namespace ns3

#endif /* NDP_HEADER_H */
