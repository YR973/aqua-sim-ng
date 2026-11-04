/*
 * ergr-emhc-routing.h: ERGR-EMHC-g routing for Aqua-Sim NG (spec §5; kickoff §2.1).
 *
 * One object per node. It owns the neighbour table (G1), runs beaconing, the rank-slotted head
 * election, the Hmin wave, forwarding and the void NACK, and asks a pluggable ForwarderSelector
 * which eligible head to use. It never branches on the selector type (spec §5.3).
 *
 * Roles: sink (static, surface, Hmin 0), head (rank 1..k in its cell), member (everyone else).
 * Members hand their own packets to a head of their own cell; only heads choose forwarders.
 */

#ifndef CB_ERGR_ERGR_EMHC_ROUTING_H
#define CB_ERGR_ERGR_EMHC_ROUTING_H

#include "ergr-headers.h"
#include "feedback-manager.h"
#include "held-packet.h"
#include "forwarder-selector.h"
#include "grid-geometry.h"
#include "localisation-noise.h"
#include "metrics-collector.h"
#include "neighbour-table.h"
#include "recent-set.h"

#include "ns3/aqua-sim-routing.h"
#include "ns3/event-id.h"
#include "ns3/mobility-model.h"
#include "ns3/random-variable-stream.h"

#include <functional>
#include <map>
#include <memory>
#include <set>

namespace ns3
{
namespace cbergr
{

class ErgrEmhcRouting : public AquaSimRouting
{
  public:
    static TypeId GetTypeId();
    ErgrEmhcRouting();

    // --- Wiring (helper calls these before Start()) ---
    void SetNodeRole(bool isSink);
    void SetSelector(Ptr<ForwarderSelector> selector) { m_selector = selector; }
    void SetMetrics(Ptr<MetricsCollector> metrics) { m_metrics = metrics; }
    void SetLocalisation(Ptr<LocalisationNoise> noise) { m_localisation = noise; }
    /// The node's own modem link budget (for selectors that predict margins).
    void SetLinkModel(std::shared_ptr<const AcousticLinkModel> link) { m_linkModel = link; }
    /// Called after every selector decision (used to measure regret against the oracle). Must not change state.
    void SetDecisionObserver(std::function<void(const PacketCtx&, const std::vector<Candidate>&, const Decision&, Time)> observer)
    {
        m_decisionObserver = observer;
    }
    /// Source of measured beacon SNR (RSSI): (sender node, my node, snr out) -> found?
    void SetSnrMeter(std::function<bool(uint32_t, uint32_t, double&)> meter) { m_snrMeter = meter; }

    /// Schedule beacons, elections and traffic. Call once at time 0.
    void Start();

    // --- AquaSimRouting interface ---
    bool Recv(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber) override;
    int64_t AssignStreams(int64_t stream) override;

    // --- Inspection (tests, logging) ---
    uint16_t GetNodeId() const { return m_id; }
    bool IsHead() const { return m_isHead; }
    bool IsSink() const { return m_isSink; }
    uint8_t HeadRank() const { return m_rank; }
    uint16_t CellId() const { return m_cell; }
    uint8_t Hmin() const { return m_hmin; }
    const NeighbourTable& Table() const { return m_table; }
    size_t PendingCount() const { return m_sent.size(); }

    bool IsAlive();

    // --- Test hooks (used by the integration tests only) ---
    /// Make this node a head of rank 'rank' (or a member for rank 0) without any election messages.
    void ForceRole(uint8_t rank);
    /// Create one data packet now, as if the traffic timer had fired.
    void InjectPacket() { OriginatePacket(); }
    const FeedbackManager* Feedback() const { return m_feedback.get(); }

  protected:
    void DoDispose() override;

  private:
    // --- Self state (position estimate, cell, Hmin) ---
    void UpdateSelfEstimate();
    Vector EstimatedPosition() const;
    void UpdateOwnCell();
    void RecomputeHmin();
    double ResidualFraction();

    // --- Beacons ---
    void BeaconTick();
    void SendBeacon();
    void OnBeacon(const ErgrBeaconHeader& hdr);
    void ExpireNeighbours();

    // --- Election (D§3 P3) ---
    double ScoreOf(double batteryFraction, const Vector& pos, uint16_t cell, bool firstElection) const;
    double SelfScore(bool firstElection);
    int RankInCell(bool firstElection);
    void OnElectionEpoch();
    void StartElectionRound(const char* reason, bool firstElection);
    void OnHelloSlot(uint8_t rank);
    void OnElectionFrame(const ErgrElectionHeader& hdr);
    void BecomeHead(uint8_t rank);
    void BecomeMember(const char* reason);
    int CountBetterHeadsInCell();
    int CountHeadsInCell();

    // --- Traffic and forwarding ---
    void TrafficTick();
    void OriginatePacket();
    void RoutePacket(HeldPacket pkt);
    void HandOffToHead(HeldPacket pkt);
    std::vector<Candidate> GetEligible(const HeldPacket& pkt, Time now) const;
    /// 'decision' is null for member hand-offs (no selector involved, label is logged but not learned).
    void SendData(HeldPacket pkt, uint16_t forwarderId, uint8_t powerLevel, bool ackRequest, const Decision* decision);
    void SendAck(uint16_t toSender, uint16_t hopSeq);
    double AckTimeoutS(uint16_t fwdId, double dataBytes);
    uint16_t NextHopSeq(uint16_t fwdId);
    void OnData(const ErgrDataHeader& hdr);
    void OnNack(const ErgrNackHeader& hdr);
    void HandleVoid(const HeldPacket& pkt);
    void Remember(HeldPacket pkt);
    void Forget(const E2EKey& key);
    void Drop(const HeldPacket& pkt, DropReason reason);

    // --- Frame I/O ---
    void TransmitFrame(Ptr<Packet> packet, FrameType type, int32_t receiver, uint8_t powerLevel);
    GridGeometry& Grid() const;

    // Attributes
    double m_cellSizeM, m_boxSizeM, m_txRangeM;
    int m_headsPerCell;
    bool m_faceOnly, m_strictProgress;
    double m_beaconPeriodS, m_neighbourTimeoutBeacons;
    double m_electionPeriodS, m_electionBeta, m_electionSlotS, m_electionJitterS, m_electionOffsetPerCellS;
    double m_firstElectionS, m_hysteresisM;
    double m_nackWindowS;
    int m_voidRetryLimit;
    int m_maxTx;
    double m_ackProcS, m_ackGuardS, m_phyRxDelayAirtimes;
    bool m_retriesAlwaysAck, m_autoElection;
    double m_expiryBeacons, m_receiptKeepBeacons;
    double m_dataStartS, m_pktRateHz;
    uint32_t m_payloadBytes;
    uint32_t m_duplicateCapacity;
    double m_duplicateTtlS;

    // Identity and role
    uint16_t m_id = 0;
    bool m_isSink = false;
    bool m_started = false;
    bool m_deathLogged = false;

    // Self estimate
    Vector m_estPos, m_estVel;
    Time m_estTime;
    uint16_t m_cell = 0;
    bool m_cellKnown = false;
    uint8_t m_hmin = 255;

    // Election
    bool m_isHead = false;
    uint8_t m_rank = 0;
    bool m_electionPending = false;
    Time m_lastElectionTrigger = Seconds(-1e9);
    EventId m_helloEvent;
    uint32_t m_electionEpochs = 0;

    // Forwarding state
    NeighbourTable m_table;
    std::map<E2EKey, HeldPacket> m_sent;
    std::unique_ptr<RecentSet> m_seen;
    std::unique_ptr<FeedbackManager> m_feedback;   ///< only for selectors that use feedback
    uint16_t m_ownSeq = 0;
    std::map<uint16_t, uint16_t> m_hopSeq;
    uint32_t m_headRoundRobin = 0;

    mutable std::unique_ptr<GridGeometry> m_grid; ///< built on first use (attributes are set after construction)
    Ptr<ForwarderSelector> m_selector;
    std::shared_ptr<const AcousticLinkModel> m_linkModel;
    std::function<bool(uint32_t, uint32_t, double&)> m_snrMeter;
    std::function<void(const PacketCtx&, const std::vector<Candidate>&, const Decision&, Time)> m_decisionObserver;
    Ptr<MetricsCollector> m_metrics;
    Ptr<LocalisationNoise> m_localisation;
    Ptr<UniformRandomVariable> m_uniform;
    Ptr<ExponentialRandomVariable> m_interArrival;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_ERGR_EMHC_ROUTING_H */
