/*
 * forwarder-selector.h: the interface every forwarding method implements (spec §5.3).
 * Routing owns one selector per node and never looks at which kind it is; the only part that
 * differs between round robin, Q-learning, CB, oracles... is Select() and OnLabel().
 */

#ifndef CB_ERGR_FORWARDER_SELECTOR_H
#define CB_ERGR_FORWARDER_SELECTOR_H

#include "acoustic-link-model.h"
#include "ergr-headers.h"
#include "neighbour-table.h"

#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/vector.h"

#include <array>
#include <functional>
#include <memory>
#include <cstdint>
#include <vector>

namespace ns3
{
namespace cbergr
{

/// End-to-end packet identity: (source node, per-source sequence number).
struct E2EKey
{
    uint16_t source = 0;
    uint16_t seq = 0;
    bool operator<(const E2EKey& o) const
    {
        return source != o.source ? source < o.source : seq < o.seq;
    }
    bool operator==(const E2EKey& o) const { return source == o.source && seq == o.seq; }
};

/// Largest feature vector supported (9 + optional onward feature, K§2.3).
constexpr int kMaxFeatureDim = 10;

/// What the selector may know when it decides (never ground truth, K§0 rule 7).
struct PacketCtx
{
    E2EKey key;
    uint8_t hpkt = 0;
    uint8_t attempt = 1;       ///< 1 for the first try of this packet at this node
    bool priority = false;
    uint16_t myId = 0;
    uint8_t myHmin = 255;
    Vector myPos;              ///< own (noisy) position estimate
    Vector myVel;
    double myBattery = 1.0;    ///< own residual energy fraction
    double myEnergyJ = 0.0;
    uint32_t dataBytes = 0;    ///< frame size, for airtime / PER
};

/// One eligible forwarder. 'nb' points into the neighbour table and is valid only during Select().
struct Candidate
{
    uint16_t id = 0;
    const NeighbourEntry* nb = nullptr;
};

/// A selector's answer for one packet.
struct Decision
{
    uint16_t fwdId = 0;
    uint8_t powerLevel = 0;     ///< index into the power levels (0 = lowest); full power = last
    bool ackRequest = false;
    std::array<double, kMaxFeatureDim> x{}; ///< features used (for logging and learning)
    double pMean = 0.0;         ///< predicted success (for logging)
    double width = 0.0;         ///< model uncertainty (for logging)
    double score = 0.0;
    double lambdaE = 0.0;       ///< energy price at decision time (logging)
    uint8_t numCandidates = 0; ///< how many eligible heads there were
    uint8_t chosenRank = 0;    ///< rank of the chosen one by score (1 = best), 0 if not scored
    bool valid = false;
};

/// How a label was obtained (spec §9, K§2.3).
enum class LabelSource
{
    Ack,
    AckTimeout,
    Bitmap,
    Expiry,
    Void   ///< the forwarder reported a void (NACK) before any other label arrived
};

/// Events that make a learner widen its uncertainty (spec §8.11, K§2.3: only deaths / unplanned loss).
enum class ChangeEvent
{
    HeadChangedInNeighbourCell,
    NodeLost,
    OwnCellChanged
};

/// What a selector may ask its node for. Everything here is something a real modem/node knows;
/// nothing is ground truth (K§0 rule 7).
struct SelectorServices
{
    uint16_t nodeId = 0;
    double beaconPeriodS = 0.0;
    double initialEnergyJ = 0.0;
    std::shared_ptr<const AcousticLinkModel> link;        ///< link budget of the node's own modem
    std::function<double()> residualEnergyJ;              ///< own battery
    std::function<double(uint32_t)> airtimeS;             ///< airtime of a frame of this many bytes
    std::function<uint16_t()> cellId;                     ///< own cell
    std::function<bool()> isHead;                         ///< currently a head?
};

class ForwarderSelector : public Object
{
  public:
    static TypeId GetTypeId();

    /// Called once before Start() so the selector can reach its node's services.
    virtual void SetServices(const SelectorServices& services) {}

    /// Called at time 0 (start timers here).
    virtual void Start() {}

    /// Use ns-3 random streams starting at 'stream'; returns how many were used.
    virtual int64_t AssignStreams(int64_t stream) { return 0; }

    /// Spec §5.3: may attach a model blob to the node's own beacon.
    virtual void OnBeaconTx(ErgrBeaconHeader& beacon, Time now) {}

    /// Spec §5.3: a beacon from another node arrived (may carry a model blob).
    virtual void OnBeaconRx(const ErgrBeaconHeader& beacon, Time now) {}

    /// Pick the forwarder (and power, ACK request) for one packet among the eligible heads.
    virtual Decision Select(const PacketCtx& pkt,
                            const std::vector<Candidate>& eligible,
                            Time now) = 0;

    /// Called right after the DATA frame was handed to the MAC (send-time evidence, spec §8.7).
    virtual void OnSent(const Decision& d, Time now) {}

    /// A label (1 received / 0 not) arrived for an earlier decision (spec §8.8).
    virtual void OnLabel(const Decision& d, int y, LabelSource src, Time now) {}

    virtual void OnKnownChange(ChangeEvent e, Time now) {}

    /// False for round robin: ERGR-EMHC as published has no hop ACKs (spec §9.1 step 2).
    virtual bool UsesFeedback() const { return true; }

    /// Distance to a candidate predicted from its last beacon (spec §8.1), at least 1 m.
    static double PredictedDistance(const PacketCtx& pkt, const NeighbourEntry& nb, Time now);
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_FORWARDER_SELECTOR_H */
