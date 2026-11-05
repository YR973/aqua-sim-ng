/*
 * ergr-headers.h: on-air headers of ERGR-EMHC-g / CB-ERGR (spec §6).
 *
 * Every header starts with a 1-byte frame type (our addition, +1 B over the §6 tables), so a
 * receiver can tell DATA from ACK, BEACON, ELECTION and NACK by peeking at the first byte.
 * GetSerializedSize() is exact: the metrics count these bytes as overhead (K§5).
 *
 * Byte order is network order (ns-3 WriteHtonU16 etc.).
 */

#ifndef CB_ERGR_HEADERS_H
#define CB_ERGR_HEADERS_H

#include "ns3/header.h"
#include "ns3/packet.h"
#include "ns3/vector.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ns3
{
namespace cbergr
{

/// First byte of every frame.
enum class FrameType : uint8_t
{
    Data = 1,
    Ack = 2,
    Beacon = 3,
    Election = 4,
    Handover = 5,
    Nack = 6
};

/// Read the frame type from the first byte of a packet (0 if empty).
uint8_t PeekFrameType(Ptr<const Packet> p);

/// Short name for logs and CSV columns.
const char* FrameTypeName(FrameType t);

// ------------------------------------------------------------------ DATA ---

/// Spec §6.1. flags: bit0 AR (ACK request), bit1 priority, bits2-3 power level.
class ErgrDataHeader : public Header
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override { return GetTypeId(); }
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    bool GetAckRequest() const { return flags & 0x01; }
    bool GetPriority() const { return flags & 0x02; }
    uint8_t GetPowerLevel() const { return (flags >> 2) & 0x03; }
    void SetFlags(bool ackRequest, bool priority, uint8_t powerLevel);

    uint16_t senderId = 0;     ///< this hop's sender
    uint16_t forwarderId = 0;  ///< intended receiver (unicast)
    uint16_t sourceId = 0;     ///< E2E key part 1
    uint16_t sourceSeq = 0;    ///< E2E key part 2
    uint8_t hpkt = 0;          ///< ERGR hop budget
    uint16_t hopSeq = 0;       ///< per (sender, forwarder) attempt number
    uint8_t flags = 0;
    uint32_t genTimeMs = 0;    ///< creation time, for end-to-end delay
};

// ------------------------------------------------------------------- ACK ---

/// Spec §6.2: sent only when the data frame had AR = 1.
class ErgrAckHeader : public Header
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override { return GetTypeId(); }
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    uint16_t ackerId = 0;
    uint16_t senderId = 0;
    uint16_t hopSeq = 0;
};

// ---------------------------------------------------------------- BEACON ---

/// One receipt entry on a beacon: what this node heard from one upstream sender (spec §6.3, §7.2).
struct ReceiptEntry
{
    uint16_t senderId = 0;
    uint16_t highest = 0;
    uint32_t mask = 0;
};

/// Model blob shared on beacons (spec §6.3). theta scaled by 1/1024 (int16), diag(A) by 1/16 (uint16).
struct ModelBlob
{
    static constexpr int kDim = 9;
    std::array<int16_t, kDim> theta{};
    std::array<uint16_t, kDim> diagA{};
};

/**
 * Spec §6.3 plus K§2.1 fields. The role byte packs: bit0 isHead, bits1-3 headRank (1..k, 0 for a
 * member), bit4 isSink. Position is in whole metres (int16, noisy), velocity in mm/s (int16, noisy).
 * nextBeaconDs is the interval to this node's next beacon in 0.1 s units (N-C, P4 failure detection).
 */
class ErgrBeaconHeader : public Header
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override { return GetTypeId(); }
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    bool IsHead() const { return role & 0x01; }
    uint8_t HeadRank() const { return (role >> 1) & 0x07; }
    bool IsSink() const { return role & 0x10; }
    void SetRole(bool isHead, uint8_t headRank, bool isSink);

    /// Bytes used by the fixed part, by the receipt list and by the blob (for the overhead split).
    uint32_t BaseBytes() const;
    uint32_t ReceiptBytes() const;
    uint32_t BlobBytes() const;

    uint16_t nodeId = 0;
    uint16_t cellId = 0;
    uint8_t role = 0;
    uint8_t hmin = 255;
    int16_t pos[3] = {0, 0, 0};
    int16_t velMmps[3] = {0, 0, 0};
    uint8_t battery = 255;        ///< 0..255 = 0..100 % of the initial energy
    uint8_t queueLen = 0;
    uint16_t nextBeaconDs = 0;
    std::vector<ReceiptEntry> receipts;
    bool hasBlob = false;
    ModelBlob blob;
    bool hasQ = false;        ///< Q-learning methods advertise their state value (+2 B)
    int16_t qValue = 0;       ///< value in units of params::selector::kQlValueScale
    uint32_t QBytes() const { return hasQ ? 2 : 0; }
};

// -------------------------------------------------------------- ELECTION ---

/// Head election messages: a claim (HELLO) in the node's slot, or a step-down (RETIRE). D§3 P3.
class ErgrElectionHeader : public Header
{
  public:
    enum Kind : uint8_t
    {
        Hello = 1,
        Retire = 2,
        Exit = 3,    ///< P0: a node leaves its cell; scoreQ carries the cell it moves to
        Solicit = 5,   ///< a member without a head row asks the heads of cell cellId to beacon (nodeId = asker)
        DeadReport = 4 ///< quorum dead-head detection: nodeId = reporter, cellId = the silent head's cell, scoreQ = the silent head's id
    };

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override { return GetTypeId(); }
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    uint8_t kind = Hello;
    uint16_t nodeId = 0;
    uint16_t cellId = 0;
    uint8_t rank = 0;          ///< claimed head rank (1..k)
    uint16_t scoreQ = 0;       ///< election score in [0,1] quantised to 16 bits
};

// -------------------------------------------------------------- HANDOVER ---

/**
 * Planned handover of a cell's learned model from the outgoing head to its successor (spec §6.4, K§2.3).
 * theta: int16 at 1/1024; A is sent as its upper triangle: 9 diagonal entries as uint16 and 36 off-diagonal
 * entries as int16, all at 1/16, saturating (spec §6.4 "45 values at DiagScale").
 * Size: 1 type + 2 from + 2 to + 2 cell + 18 theta + 18 diagonal + 72 off-diagonal = 115 bytes.
 */
class ErgrHandoverHeader : public Header
{
  public:
    static constexpr int kDim = ModelBlob::kDim;
    static constexpr int kOffDiag = kDim * (kDim - 1) / 2;

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override { return GetTypeId(); }
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    uint16_t fromId = 0;
    uint16_t toId = 0;
    uint16_t cellId = 0;
    std::array<int16_t, kDim> theta{};
    std::array<uint16_t, kDim> diag{};
    std::array<int16_t, kOffDiag> offDiag{};
};

// ------------------------------------------------------------------ NACK ---

/// Void notification: "I (nackerId) have no eligible forwarder for this packet" (spec §5.2). Unicast to the previous hop.
class ErgrNackHeader : public Header
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override { return GetTypeId(); }
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    uint16_t nackerId = 0;
    uint16_t upstreamId = 0;   ///< the previous hop being told
    uint16_t sourceId = 0;
    uint16_t sourceSeq = 0;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_HEADERS_H */
