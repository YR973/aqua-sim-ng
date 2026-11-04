/*
 * metrics-collector.h: every counter and CSV the experiments need (spec §13, kickoff §2.6).
 *
 * Protocol classes never count or write anything themselves: they call the one-line hooks below
 * (OnFrameTx, OnPacketCreated, OnDrop, ...) and this class does the bookkeeping, so the
 * logging stays out of the protocol logic. Files written to OutDir:
 *   summary.csv (always), nodes.csv, packets.csv, events.csv, frames.csv (flags).
 * Counters ignore everything before the warm-up time (spec §12); packets are counted by creation time.
 */

#ifndef CB_ERGR_METRICS_COLLECTOR_H
#define CB_ERGR_METRICS_COLLECTOR_H

#include "ergr-headers.h"
#include "forwarder-selector.h"

#include "ns3/aqua-sim-energy-model.h"
#include "ns3/nstime.h"
#include "ns3/object.h"

#include <array>
#include <fstream>
#include <functional>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

namespace ns3
{
namespace cbergr
{

/// Why a DATA packet was lost for good (kickoff §2.6). Never "unknown".
enum class DropReason : uint8_t
{
    NoEligibleForwarder,
    MaxTxReached,
    HopBudgetExhausted,
    QueueFull,
    NodeDiedHolding,
    TtlExpired,       ///< no hop ever reported it again; ground truth says it is lost (round robin cannot see losses)
    kCount
};

/// Why a frame was not received by a node that was in range (kickoff §2.6).
enum class RxFailReason : uint8_t
{
    Collision,
    PerLoss,
    HalfDuplex,
    OutOfRange,
    ReceiverAsleep,
    ReceiverDead,
    kCount
};

const char* DropReasonName(DropReason r);
const char* RxFailReasonName(RxFailReason r);

constexpr int kNumFrameTypes = 7; // index = FrameType value (1..6), slot 0 unused

class MetricsCollector : public Object
{
  public:
    struct Config
    {
        double warmupS = 0.0;
        double simTimeS = 0.0;
        double packetTtlS = 120.0;     ///< undelivered packets older than this at the end count as lost
        std::string outDir = "results/";
        std::string selectorName = "RoundRobin";
        std::string flags;             ///< free text describing the variant (goes to summary.csv)
        std::string scenarioParams;    ///< "key=value;..." copy of the main parameters
        uint32_t seed = 1;
        uint32_t run = 1;
        bool logNodes = true;
        bool logPackets = true;
        bool logEvents = true;
        bool logFrames = false;
        bool logLinks = true;
        bool logDecisions = false;
    };

    static TypeId GetTypeId();

    /// Store the settings, create OutDir and open frames.csv if enabled.
    void Configure(const Config& cfg);
    const Config& GetConfig() const { return m_cfg; }

    /// Register a node so energy, role and final state can be read at the end.
    void RegisterNode(uint32_t nodeId, bool isSink, Ptr<AquaSimEnergyModel> energy);

    /// Remember energy at the end of warm-up so per-delivered energy covers the measured window only.
    void SnapshotEnergyAtWarmup();

    // --- Frame hooks ---
    void OnFrameTx(uint32_t node, FrameType type, uint32_t bytes, uint8_t powerLevel, int32_t receiver);
    void OnFrameRx(uint32_t node, FrameType type, uint32_t bytes, bool addressedToMe, int32_t sender);
    void OnLinkEval(uint32_t txNode,
                    uint32_t rxNode,
                    FrameType type,
                    uint32_t bytes,
                    double distM,
                    double snrDb,
                    bool ok,
                    RxFailReason reason);
    /// PHY dropped an incoming frame as erroneous (collision, half-duplex or below threshold).
    void OnPhyRxError(uint32_t node);

    // --- Packet hooks ---
    void OnPacketCreated(E2EKey key, uint32_t node);
    void OnPacketAtNode(E2EKey key, uint32_t node);          ///< a node accepted the packet (adds to the path)
    void OnDataSent(E2EKey key, uint32_t node, bool isRetry); ///< one DATA frame sent for this packet
    void OnDelivered(E2EKey key, uint32_t sinkNode);
    void OnDrop(uint32_t node, E2EKey key, DropReason reason);
    void OnDuplicate(uint32_t node);

    // --- Feedback hooks (D4) ---
    /// A DATA frame was sent. Registers the attempt for ground truth (did the forwarder get it?).
    void OnAttemptSent(uint32_t sender,
                       uint16_t fwd,
                       uint16_t hopSeq,
                       uint8_t attempt,
                       bool ackRequested,
                       bool learn,
                       bool tracked,
                       double predictedDistM,
                       const Decision& decision);
    /// A feedback manager reports how many attempts it still holds at the end (consistency check).
    void RegisterPendingProbe(std::function<size_t()> probe) { m_pendingProbes.push_back(probe); }
    /// An attempt was removed without a label (its forwarder was declared dead).
    void OnAttemptDiscarded(uint32_t node);
    /// GROUND TRUTH, called by the forwarder when a DATA frame addressed to it arrives. Logging only.
    void OnHopReceived(uint32_t sender, uint32_t fwd, uint16_t hopSeq);
    /// Ground truth lookup for the CSVs (never given to a learner).
    bool HopWasReceived(uint32_t sender, uint32_t fwd, uint16_t hopSeq) const;
    /// An attempt got its label.
    void OnLabel(uint32_t node,
                 uint16_t fwd,
                 uint16_t hopSeq,
                 int label,
                 LabelSource source,
                 double delayS,
                 uint8_t attempt,
                 bool ackRequested,
                 bool learn,
                 const Decision& decision);
    void OnDuplicateLabel(uint32_t node);
    void OnAckSent(uint32_t node);
    void OnAckReceived(uint32_t node);

    // --- Protocol events ---
    void OnEvent(uint32_t node, const std::string& kind, const std::string& details);
    void OnHeadChange(uint32_t node, bool isHead, uint16_t cellId);

    // --- Final output ---
    /// Close open head tenures, classify packets, run consistency checks, write all CSVs.
    void Finalize();

    /// Consistency checks of kickoff §2.6; returns one message per violation (empty = consistent).
    std::vector<std::string> CheckConsistency() const;

    // --- Read access (tests, calibration report) ---
    uint64_t TotalFramesTx(FrameType t) const { return m_totalFramesTx[static_cast<int>(t)]; }
    uint64_t TotalAttempts() const { return m_totalAttempts; }
    uint64_t TotalLabels() const { return m_totalLabels; }
    uint64_t TotalFalseNegatives() const { return m_falseNegatives; }
    uint64_t TotalBytesTx(FrameType t) const { return m_totalBytesTx[static_cast<int>(t)]; }
    uint32_t PacketsCreated() const { return m_createdCounted; }
    uint32_t PacketsDelivered() const { return m_deliveredCounted; }
    double RxEnergyShare() const { return m_rxEnergyShare; }
    double FirstNodeDeathS() const { return m_fndS; }
    double Pdr() const { return m_pdr; }

  private:
    struct NodeStats
    {
        bool isSink = false;
        Ptr<AquaSimEnergyModel> energy;
        double txAtWarmup = 0, rxAtWarmup = 0, idleAtWarmup = 0;
        std::array<uint64_t, kNumFrameTypes> framesTx{}, bytesTx{}, framesRxOk{}, bytesRxOk{};
        std::array<uint64_t, kNumFrameTypes> framesOverheard{};
        std::array<std::array<uint64_t, static_cast<int>(RxFailReason::kCount)>, kNumFrameTypes> rxFail{};
        uint64_t phyRxErrors = 0;   ///< raw PHY error events; collisions = this minus known PER losses
        uint64_t dataOriginated = 0, dataForwarded = 0, dataDuplicates = 0;
        std::array<uint64_t, static_cast<int>(DropReason::kCount)> drops{};
        uint64_t attempts = 0, firstAttempts = 0, retransmissions = 0;
        uint64_t hopSuccessFirst = 0, hopSuccessRetry = 0;   ///< ground truth, by attempt number
        uint64_t arRequested = 0, acksSent = 0, acksReceived = 0, ackTimeouts = 0, duplicateLabels = 0;
        std::array<uint64_t, 5> labelsBySource{};            ///< Ack, AckTimeout, Bitmap, Expiry, Void
        std::array<uint64_t, 2> labelsByValue{};
        double labelDelaySumS = 0.0;
        double headTimeS = 0.0;
        double headSinceS = -1.0;   ///< start of the current tenure, -1 when not head
        uint32_t timesElected = 0;
        uint16_t lastCell = 0;
    };

    struct PacketRecord
    {
        E2EKey key;
        double createdS = 0.0;
        bool delivered = false;
        double deliveredS = 0.0;
        uint32_t sinkNode = 0;
        uint32_t hops = 0;
        uint32_t dataTx = 0;
        uint32_t retx = 0;
        double lastActivityS = 0.0;
        std::string path;
        bool dropped = false;
        DropReason dropReason = DropReason::TtlExpired;
        uint32_t dropNode = 0;
    };

    /// Ground truth for one outstanding attempt, keyed by (sender, forwarder, hopSeq).
    struct Outstanding
    {
        uint8_t attempt = 1;
        bool received = false;
        double sentS = 0.0;
    };
    static uint64_t HopId(uint32_t sender, uint32_t fwd, uint16_t hopSeq)
    {
        return (static_cast<uint64_t>(sender) << 40) | (static_cast<uint64_t>(fwd) << 16) | hopSeq;
    }
    struct LinkStats
    {
        uint64_t attempts = 0, retx = 0, truthSuccess = 0;
        std::array<uint64_t, 5> labelsBySource{};   ///< label 1 / 0 split is in the two arrays below
        uint64_t labelOnes = 0, labelZeros = 0;
        double distSumM = 0.0, predSum = 0.0;
        double firstS = -1.0, lastS = 0.0;
    };
    struct SnrStats
    {
        double sum = 0.0;
        uint64_t n = 0;
    };

    bool Counting() const;
    void WriteLinks();
    void WriteDecisionLine(uint32_t node, uint16_t fwd, uint16_t hopSeq, int label, LabelSource src, double delayS,
                           uint8_t attempt, bool ar, bool learn, bool truth, const Decision& d);
    NodeStats& Node(uint32_t id) { return m_nodes[id]; }
    uint64_t CollisionsOf(const NodeStats& n) const;
    void WriteSummary();
    void WriteNodes();
    void WritePackets();
    void WriteEvents();
    void WriteFramesLine(const std::string& line);

    Config m_cfg;
    std::map<uint32_t, NodeStats> m_nodes;
    std::map<E2EKey, PacketRecord> m_packets;
    struct EventRow { double t; uint32_t node; std::string kind; std::string details; };
    std::vector<EventRow> m_events;
    std::ofstream m_framesFile;                 ///< frames.csv, streamed because it can be huge
    std::vector<double> m_tenuresS;             ///< completed head tenures (s)

    std::vector<std::function<size_t()>> m_pendingProbes;
    uint64_t m_trackedAttempts = 0, m_discardedAttempts = 0, m_labelsAllTime = 0;
    std::unordered_map<uint64_t, Outstanding> m_outstanding;
    std::map<std::pair<uint32_t, uint32_t>, LinkStats> m_links;
    std::map<std::pair<uint32_t, uint32_t>, SnrStats> m_linkSnr;   ///< DATA frames only
    std::ofstream m_decisionsFile;
    uint64_t m_totalAttempts = 0, m_totalLabels = 0, m_falseNegatives = 0, m_labelZeros = 0;
    std::array<uint64_t, 5> m_labelsBySource{};
    double m_labelDelaySumS = 0.0;
    std::vector<double> m_labelDelays;
    uint64_t m_arAttempts = 0, m_duplicateLabelsTotal = 0;
    std::array<uint64_t, kNumFrameTypes> m_totalFramesTx{};
    std::array<uint64_t, kNumFrameTypes> m_totalBytesTx{};
    uint64_t m_electionsAfterWarmup = 0;

    // results filled by Finalize()
    uint32_t m_createdCounted = 0, m_deliveredCounted = 0, m_inFlight = 0, m_droppedCounted = 0;
    std::array<uint32_t, static_cast<int>(DropReason::kCount)> m_dropCounts{};
    double m_pdr = 0.0, m_delayMeanS = 0.0, m_delayP95S = 0.0;
    double m_txJ = 0.0, m_rxJ = 0.0, m_idleJ = 0.0, m_rxEnergyShare = 0.0;
    double m_fndS = -1.0, m_hndS = -1.0, m_jain = 0.0, m_tenureMeanS = 0.0;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_METRICS_COLLECTOR_H */
