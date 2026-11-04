/*
 * ergr-emhc-published.h: "P0", ERGR-EMHC as published (Al-Salti, Alzeidi, Day, Touzene, Computer Networks 162 (2019)
 * 106869), as far as the paper specifies it. Separate from ErgrEmhcRouting (the improved ERGR-EMHC-g) so that the
 * published protocol stays unchanged by the improvements. Always round robin; no hop ACKs, no learning.
 *
 *   - Cells of side d = R / (2 sqrt 3); 32 neighbour cells; Hmin = cell-level hops to the nearest sink cell (grid
 *     geometry, no beacon wave).
 *   - Each node keeps a table of the cell-head of every neighbour cell, learned from Hello packets (the paper's NG1T
 *     = neighbour cells with a lower Hmin than the own cell, NG2T = the others), with a Busy flag per entry.
 *   - Forwarder = round robin over NG1T entries with Hmin <= Hpkt that are not busy. Hpkt = beta Hmin + (1 - beta) Hmax.
 *   - Void handling: NegAck to the previous hop, which re-selects from NG1T + NG2T (lowest Hmin, not the previous or an
 *     already tried cell); waiting time T_w without a NegAck means success (implicit acknowledgement).
 *   - Election: initialisation timer (random, GBPR style, or iPeriod x P(i)), Hello, update timer, new-election timer,
 *     Exit when a node leaves its cell, Retire when a head's energy falls below a threshold.
 * Everything the paper leaves open is a [CONFIRM] value in cb-ergr-params.h (namespace p0).
 */

#ifndef CB_ERGR_ERGR_EMHC_PUBLISHED_H
#define CB_ERGR_ERGR_EMHC_PUBLISHED_H

#include "ergr-headers.h"
#include "grid-geometry.h"
#include "metrics-collector.h"
#include "recent-set.h"

#include "ns3/aqua-sim-routing.h"
#include "ns3/event-id.h"
#include "ns3/mobility-model.h"
#include "ns3/random-variable-stream.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace ns3
{
namespace cbergr
{

class ErgrEmhcPublishedRouting : public AquaSimRouting
{
  public:
    static TypeId GetTypeId();
    ErgrEmhcPublishedRouting();

    // --- Wiring ---
    void SetNodeRole(bool isSink) { m_isSink = isSink; }
    void SetMetrics(Ptr<MetricsCollector> metrics) { m_metrics = metrics; }
    /// Sink table (SNKT): node id and position of every sink; known to all nodes (paper §3).
    void SetSinks(const std::vector<std::pair<uint16_t, Vector>>& sinks) { m_sinks = sinks; }
    void Start();

    bool Recv(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber) override;
    int64_t AssignStreams(int64_t stream) override;

    // --- Inspection / failure injection ---
    bool IsAlive();
    void Kill();
    bool IsHead() const { return m_isHead; }
    bool IsSink() const { return m_isSink; }
    uint16_t CellId() const { return m_cell; }
    uint8_t CellHmin() const { return m_hmin.empty() ? 255 : m_hmin[m_cell]; }
    void InjectPacket() { OriginatePacket(); }

  protected:
    void DoDispose() override;

  private:
    /// Cell-head known for a neighbour cell (the NG1T/NG2T record).
    struct HeadEntry
    {
        uint16_t nodeId = 0;
        Time busyUntil = Seconds(0);   ///< "Busy": a packet sent to it has not been acknowledged yet
    };

    /// A forwarded packet awaiting its waiting time T_w (the paper's packets-list PKT).
    struct SentRecord
    {
        E2EKey key;
        uint32_t genTimeMs = 0;
        int hpkt = 0;
        int32_t prevCell = -1;
        int32_t prevHop = -1;
        int nextCell = -1;
        std::set<uint16_t> triedCells;
        int voidRetries = 0;
        EventId tw;
    };

    // --- Geometry / position ---
    GridGeometry& Grid() const;
    void BuildHminTable();
    Vector TruePosition() const;
    void CheckCell();

    // --- Election ---
    double Priority();
    double ElectionTimerS();
    void StartInitTimer();
    void OnInitTimer();
    void ArmElectionTimer();
    void OnElectionTimer();
    void OnUpdateTimer();
    void BecomeHead();
    void StepDown(const char* reason);
    void SendControl(ErgrElectionHeader::Kind kind, uint16_t newCell);
    void OnControl(const ErgrElectionHeader& hdr);
    double ResidualFraction();

    // --- Forwarding ---
    void TrafficTick();
    void OriginatePacket();
    void Forward(SentRecord rec, bool isSource);
    int ChooseNextCell(const SentRecord& rec, int hpkt, bool voidMode);
    void SendData(SentRecord rec, uint16_t forwarderNode);
    void OnData(const ErgrDataHeader& hdr);
    void OnNack(const ErgrNackHeader& hdr);
    void OnWaitExpired(E2EKey key);
    void SendNack(const SentRecord& rec);
    void Drop(const SentRecord& rec, DropReason reason);
    double WaitingTimeS();
    void TransmitFrame(Ptr<Packet> packet, FrameType type, int32_t receiver);

    // Attributes (defaults: params::p0 and params::scenario)
    double m_cellSizeM, m_boxSizeM, m_txRangeM;
    double m_beta, m_hmaxFactor;
    double m_iPeriodS, m_updatePeriodS, m_newElectionS, m_energyThresholdJ, m_weight;
    bool m_randomTimers;
    double m_positionCheckS;
    int m_voidRetryLimit;
    double m_dataStartS, m_pktRateHz;
    uint32_t m_payloadBytes;

    uint16_t m_id = 0;
    bool m_isSink = false;
    bool m_started = false;
    bool m_deathLogged = false;
    uint16_t m_cell = 0;
    bool m_isHead = false;
    int32_t m_cellHead = -1;                    ///< node id of the head of the own cell, -1 unknown
    std::vector<uint8_t> m_hmin;                ///< cell-level Hmin for every cell
    std::map<uint16_t, HeadEntry> m_heads;      ///< neighbour cell -> its head (NG1T and NG2T together)
    std::map<E2EKey, SentRecord> m_sent;
    std::unique_ptr<RecentSet> m_seen;
    uint16_t m_ownSeq = 0;
    uint32_t m_roundRobin = 0;
    std::vector<std::pair<uint16_t, Vector>> m_sinks;
    EventId m_initTimer, m_electionTimer, m_updateTimer;

    mutable std::unique_ptr<GridGeometry> m_grid;
    Ptr<MetricsCollector> m_metrics;
    Ptr<UniformRandomVariable> m_uniform;
    Ptr<ExponentialRandomVariable> m_interArrival;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_ERGR_EMHC_PUBLISHED_H */
