#include "ndp-header.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NdpHeader");

NS_OBJECT_ENSURE_REGISTERED(NdpHeader);

NdpHeader::NdpHeader()
    : m_connId(0),
      m_seq(0),
      m_pullSeq(0),
      m_flags(0),
      m_pathId(0),
      m_seqOffset(0)
{
}

NdpHeader::~NdpHeader()
{
}

TypeId
NdpHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NdpHeader")
                            .SetParent<Header>()
                            .SetGroupName("Dcb")
                            .AddConstructor<NdpHeader>();
    return tid;
}

TypeId
NdpHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
NdpHeader::Print(std::ostream& os) const
{
    os << "NdpHeader: connId=" << m_connId << " seq=" << m_seq << " pullSeq=" << m_pullSeq
       << " flags=0x" << std::hex << (int)m_flags << std::dec << " pathId=" << (int)m_pathId
       << " seqOffset=" << m_seqOffset;
    
    os << " (";
    bool first = true;
    if (IsSyn()) {
        os << "SYN";
        first = false;
    }
    if (IsAck()) {
        if (!first) os << "|";
        os << "ACK";
        first = false;
    }
    if (IsNack()) {
        if (!first) os << "|";
        os << "NACK";
        first = false;
    }
    if (IsPull()) {
        if (!first) os << "|";
        os << "PULL";
        first = false;
    }
    if (IsTrim()) {
        if (!first) os << "|";
        os << "TRIM";
        first = false;
    }
    if (IsLast()) {
        if (!first) os << "|";
        os << "LAST";
        first = false;
    }
    if (IsRts()) {
        if (!first) os << "|";
        os << "RTS";
    }
    os << ")";
}

uint32_t
NdpHeader::GetSerializedSize() const
{
    // connId (8) + seq (4) + pullSeq (4) + flags (1) + pathId (1) + seqOffset (4) = 22 bytes
    return 22;
}

void
NdpHeader::Serialize(Buffer::Iterator start) const
{
    start.WriteHtonU64(m_connId);
    start.WriteHtonU32(m_seq);
    start.WriteHtonU32(m_pullSeq);
    start.WriteU8(m_flags);
    start.WriteU8(m_pathId);
    start.WriteHtonU32(m_seqOffset);
}

uint32_t
NdpHeader::Deserialize(Buffer::Iterator start)
{
    m_connId = start.ReadNtohU64();
    m_seq = start.ReadNtohU32();
    m_pullSeq = start.ReadNtohU32();
    m_flags = start.ReadU8();
    m_pathId = start.ReadU8();
    m_seqOffset = start.ReadNtohU32();
    return GetSerializedSize();
}

void
NdpHeader::SetConnectionId(uint64_t connId)
{
    m_connId = connId;
}

void
NdpHeader::SetSequence(uint32_t seq)
{
    m_seq = seq;
}

void
NdpHeader::SetPullSequence(uint32_t pullSeq)
{
    m_pullSeq = pullSeq;
}

void
NdpHeader::SetFlags(uint8_t flags)
{
    m_flags = flags;
}

void
NdpHeader::SetPathId(uint8_t pathId)
{
    m_pathId = pathId;
}

void
NdpHeader::SetSeqOffset(uint32_t seqOffset)
{
    m_seqOffset = seqOffset;
}

uint64_t
NdpHeader::GetConnectionId() const
{
    return m_connId;
}

uint32_t
NdpHeader::GetSequence() const
{
    return m_seq;
}

uint32_t
NdpHeader::GetPullSequence() const
{
    return m_pullSeq;
}

uint8_t
NdpHeader::GetFlags() const
{
    return m_flags;
}

uint8_t
NdpHeader::GetPathId() const
{
    return m_pathId;
}

uint32_t
NdpHeader::GetSeqOffset() const
{
    return m_seqOffset;
}

bool
NdpHeader::HasFlag(Flags flag) const
{
    return (m_flags & flag) != 0;
}

void
NdpHeader::AddFlag(Flags flag)
{
    m_flags |= flag;
}

void
NdpHeader::RemoveFlag(Flags flag)
{
    m_flags &= ~flag;
}

bool
NdpHeader::IsSyn() const
{
    return HasFlag(SYN);
}

bool
NdpHeader::IsAck() const
{
    return HasFlag(ACK);
}

bool
NdpHeader::IsNack() const
{
    return HasFlag(NACK);
}

bool
NdpHeader::IsPull() const
{
    return HasFlag(PULL);
}

bool
NdpHeader::IsTrim() const
{
    return HasFlag(TRIM);
}

bool
NdpHeader::IsLast() const
{
    return HasFlag(LAST);
}

bool
NdpHeader::IsRts() const
{
    return HasFlag(RTS);
}

bool
NdpHeader::IsData() const
{
    // Data packet: not control (ACK, NACK, PULL)
    // Can have SYN, TRIM, LAST flags
    return !HasFlag(ACK) && !HasFlag(NACK) && !HasFlag(PULL);
}

} // namespace ns3
