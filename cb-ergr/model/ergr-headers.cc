/*
 * ergr-headers.cc: serialisation of the DATA, ACK, BEACON, ELECTION and NACK headers.
 * Implements spec §6 with exact sizes; round trips are covered by test 9.
 */

#include "ergr-headers.h"

#include "ns3/log.h"

namespace ns3
{
namespace cbergr
{

NS_LOG_COMPONENT_DEFINE("CbErgrHeaders");
NS_OBJECT_ENSURE_REGISTERED(ErgrDataHeader);
NS_OBJECT_ENSURE_REGISTERED(ErgrAckHeader);
NS_OBJECT_ENSURE_REGISTERED(ErgrBeaconHeader);
NS_OBJECT_ENSURE_REGISTERED(ErgrElectionHeader);
NS_OBJECT_ENSURE_REGISTERED(ErgrNackHeader);

uint8_t
PeekFrameType(Ptr<const Packet> p)
{
    uint8_t first = 0;
    if (p->GetSize() > 0)
    {
        p->CopyData(&first, 1);
    }
    return first;
}

const char*
FrameTypeName(FrameType t)
{
    switch (t)
    {
    case FrameType::Data:
        return "DATA";
    case FrameType::Ack:
        return "ACK";
    case FrameType::Beacon:
        return "BEACON";
    case FrameType::Election:
        return "ELECTION";
    case FrameType::Handover:
        return "HANDOVER";
    case FrameType::Nack:
        return "NACK";
    }
    return "UNKNOWN";
}

// ------------------------------------------------------------------ DATA ---

TypeId
ErgrDataHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::cbergr::ErgrDataHeader")
                            .SetParent<Header>()
                            .AddConstructor<ErgrDataHeader>();
    return tid;
}

// type 1 + sender 2 + forwarder 2 + source 2 + seq 2 + hpkt 1 + hopSeq 2 + flags 1 + genTime 4
uint32_t
ErgrDataHeader::GetSerializedSize() const
{
    return 17;
}

void
ErgrDataHeader::SetFlags(bool ackRequest, bool priority, uint8_t powerLevel)
{
    flags = static_cast<uint8_t>((ackRequest ? 0x01 : 0) | (priority ? 0x02 : 0) |
                                 ((powerLevel & 0x03) << 2));
}

void
ErgrDataHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(static_cast<uint8_t>(FrameType::Data));
    i.WriteHtonU16(senderId);
    i.WriteHtonU16(forwarderId);
    i.WriteHtonU16(sourceId);
    i.WriteHtonU16(sourceSeq);
    i.WriteU8(hpkt);
    i.WriteHtonU16(hopSeq);
    i.WriteU8(flags);
    i.WriteHtonU32(genTimeMs);
}

uint32_t
ErgrDataHeader::Deserialize(Buffer::Iterator i)
{
    i.ReadU8(); // frame type
    senderId = i.ReadNtohU16();
    forwarderId = i.ReadNtohU16();
    sourceId = i.ReadNtohU16();
    sourceSeq = i.ReadNtohU16();
    hpkt = i.ReadU8();
    hopSeq = i.ReadNtohU16();
    flags = i.ReadU8();
    genTimeMs = i.ReadNtohU32();
    return GetSerializedSize();
}

void
ErgrDataHeader::Print(std::ostream& os) const
{
    os << "DATA sender=" << senderId << " fwd=" << forwarderId << " src=" << sourceId << ":"
       << sourceSeq << " hpkt=" << int(hpkt) << " hopSeq=" << hopSeq << " flags=" << int(flags);
}

// ------------------------------------------------------------------- ACK ---

TypeId
ErgrAckHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::cbergr::ErgrAckHeader")
                            .SetParent<Header>()
                            .AddConstructor<ErgrAckHeader>();
    return tid;
}

uint32_t
ErgrAckHeader::GetSerializedSize() const
{
    return 7; // type 1 + acker 2 + sender 2 + hopSeq 2
}

void
ErgrAckHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(static_cast<uint8_t>(FrameType::Ack));
    i.WriteHtonU16(ackerId);
    i.WriteHtonU16(senderId);
    i.WriteHtonU16(hopSeq);
}

uint32_t
ErgrAckHeader::Deserialize(Buffer::Iterator i)
{
    i.ReadU8();
    ackerId = i.ReadNtohU16();
    senderId = i.ReadNtohU16();
    hopSeq = i.ReadNtohU16();
    return GetSerializedSize();
}

void
ErgrAckHeader::Print(std::ostream& os) const
{
    os << "ACK acker=" << ackerId << " sender=" << senderId << " hopSeq=" << hopSeq;
}

// ---------------------------------------------------------------- BEACON ---

TypeId
ErgrBeaconHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::cbergr::ErgrBeaconHeader")
                            .SetParent<Header>()
                            .AddConstructor<ErgrBeaconHeader>();
    return tid;
}

void
ErgrBeaconHeader::SetRole(bool isHead, uint8_t headRank, bool isSink)
{
    role = static_cast<uint8_t>((isHead ? 0x01 : 0) | ((headRank & 0x07) << 1) | (isSink ? 0x10 : 0));
}

// type 1 + node 2 + cell 2 + role 1 + hmin 1 + pos 6 + vel 6 + battery 1 + queue 1 + nextBeacon 2
// + nReceipts 1 + blobFlag 1 = 25
uint32_t
ErgrBeaconHeader::BaseBytes() const
{
    return 25;
}

uint32_t
ErgrBeaconHeader::ReceiptBytes() const
{
    return 8 * static_cast<uint32_t>(receipts.size()); // sender 2 + highest 2 + mask 4 each
}

uint32_t
ErgrBeaconHeader::BlobBytes() const
{
    return hasBlob ? 4 * ModelBlob::kDim : 0; // int16 theta + uint16 diag per dimension
}

uint32_t
ErgrBeaconHeader::GetSerializedSize() const
{
    return BaseBytes() + ReceiptBytes() + BlobBytes() + QBytes();
}

void
ErgrBeaconHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(static_cast<uint8_t>(FrameType::Beacon));
    i.WriteHtonU16(nodeId);
    i.WriteHtonU16(cellId);
    i.WriteU8(role);
    i.WriteU8(hmin);
    for (int k = 0; k < 3; ++k)
    {
        i.WriteHtonU16(static_cast<uint16_t>(pos[k]));
    }
    for (int k = 0; k < 3; ++k)
    {
        i.WriteHtonU16(static_cast<uint16_t>(velMmps[k]));
    }
    i.WriteU8(battery);
    i.WriteU8(queueLen);
    i.WriteHtonU16(nextBeaconDs);
    i.WriteU8(static_cast<uint8_t>(receipts.size()));
    i.WriteU8(static_cast<uint8_t>((hasBlob ? 1 : 0) | (hasQ ? 2 : 0))); // flag byte: bit0 blob, bit1 Q value
    for (const ReceiptEntry& r : receipts)
    {
        i.WriteHtonU16(r.senderId);
        i.WriteHtonU16(r.highest);
        i.WriteHtonU32(r.mask);
    }
    if (hasBlob)
    {
        for (int k = 0; k < ModelBlob::kDim; ++k)
        {
            i.WriteHtonU16(static_cast<uint16_t>(blob.theta[k]));
        }
        for (int k = 0; k < ModelBlob::kDim; ++k)
        {
            i.WriteHtonU16(blob.diagA[k]);
        }
    }
    if (hasQ)
    {
        i.WriteHtonU16(static_cast<uint16_t>(qValue));
    }
}

uint32_t
ErgrBeaconHeader::Deserialize(Buffer::Iterator i)
{
    i.ReadU8();
    nodeId = i.ReadNtohU16();
    cellId = i.ReadNtohU16();
    role = i.ReadU8();
    hmin = i.ReadU8();
    for (int k = 0; k < 3; ++k)
    {
        pos[k] = static_cast<int16_t>(i.ReadNtohU16());
    }
    for (int k = 0; k < 3; ++k)
    {
        velMmps[k] = static_cast<int16_t>(i.ReadNtohU16());
    }
    battery = i.ReadU8();
    queueLen = i.ReadU8();
    nextBeaconDs = i.ReadNtohU16();
    uint8_t nReceipts = i.ReadU8();
    uint8_t flags = i.ReadU8();
    hasBlob = (flags & 1) != 0;
    hasQ = (flags & 2) != 0;
    receipts.clear();
    for (uint8_t n = 0; n < nReceipts; ++n)
    {
        ReceiptEntry r;
        r.senderId = i.ReadNtohU16();
        r.highest = i.ReadNtohU16();
        r.mask = i.ReadNtohU32();
        receipts.push_back(r);
    }
    if (hasBlob)
    {
        for (int k = 0; k < ModelBlob::kDim; ++k)
        {
            blob.theta[k] = static_cast<int16_t>(i.ReadNtohU16());
        }
        for (int k = 0; k < ModelBlob::kDim; ++k)
        {
            blob.diagA[k] = i.ReadNtohU16();
        }
    }
    if (hasQ)
    {
        qValue = static_cast<int16_t>(i.ReadNtohU16());
    }
    return GetSerializedSize();
}

void
ErgrBeaconHeader::Print(std::ostream& os) const
{
    os << "BEACON node=" << nodeId << " cell=" << cellId << " head=" << IsHead()
       << " rank=" << int(HeadRank()) << " sink=" << IsSink() << " hmin=" << int(hmin)
       << " receipts=" << receipts.size() << " blob=" << hasBlob;
}

// -------------------------------------------------------------- ELECTION ---

TypeId
ErgrElectionHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::cbergr::ErgrElectionHeader")
                            .SetParent<Header>()
                            .AddConstructor<ErgrElectionHeader>();
    return tid;
}

uint32_t
ErgrElectionHeader::GetSerializedSize() const
{
    return 9; // type 1 + kind 1 + node 2 + cell 2 + rank 1 + score 2
}

void
ErgrElectionHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(static_cast<uint8_t>(FrameType::Election));
    i.WriteU8(kind);
    i.WriteHtonU16(nodeId);
    i.WriteHtonU16(cellId);
    i.WriteU8(rank);
    i.WriteHtonU16(scoreQ);
}

uint32_t
ErgrElectionHeader::Deserialize(Buffer::Iterator i)
{
    i.ReadU8();
    kind = i.ReadU8();
    nodeId = i.ReadNtohU16();
    cellId = i.ReadNtohU16();
    rank = i.ReadU8();
    scoreQ = i.ReadNtohU16();
    return GetSerializedSize();
}

void
ErgrElectionHeader::Print(std::ostream& os) const
{
    os << (kind == Hello ? "HELLO" : "RETIRE") << " node=" << nodeId << " cell=" << cellId
       << " rank=" << int(rank) << " score=" << scoreQ;
}

// ------------------------------------------------------------------ NACK ---

TypeId
ErgrNackHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::cbergr::ErgrNackHeader")
                            .SetParent<Header>()
                            .AddConstructor<ErgrNackHeader>();
    return tid;
}

uint32_t
ErgrNackHeader::GetSerializedSize() const
{
    return 9; // type 1 + nacker 2 + upstream 2 + source 2 + seq 2
}

void
ErgrNackHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(static_cast<uint8_t>(FrameType::Nack));
    i.WriteHtonU16(nackerId);
    i.WriteHtonU16(upstreamId);
    i.WriteHtonU16(sourceId);
    i.WriteHtonU16(sourceSeq);
}

uint32_t
ErgrNackHeader::Deserialize(Buffer::Iterator i)
{
    i.ReadU8();
    nackerId = i.ReadNtohU16();
    upstreamId = i.ReadNtohU16();
    sourceId = i.ReadNtohU16();
    sourceSeq = i.ReadNtohU16();
    return GetSerializedSize();
}

void
ErgrNackHeader::Print(std::ostream& os) const
{
    os << "NACK nacker=" << nackerId << " upstream=" << upstreamId << " pkt=" << sourceId << ":"
       << sourceSeq;
}

} // namespace cbergr
} // namespace ns3
