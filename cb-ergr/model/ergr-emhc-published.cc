/*
 * ergr-emhc-published.cc: P0, ERGR-EMHC as published (paper §4). Sections: start-up, cell tracking, election,
 * forwarding with void handling, frame I/O. Gaps in the paper are [CONFIRM] values in params::p0.
 */

#include "ergr-emhc-published.h"

#include "cb-ergr-params.h"

#include "ns3/aqua-sim-address.h"
#include "ns3/aqua-sim-header.h"
#include "ns3/aqua-sim-net-device.h"
#include "ns3/aqua-sim-phy-cmn.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace ns3
{
namespace cbergr
{

NS_LOG_COMPONENT_DEFINE("ErgrEmhcPublished");
NS_OBJECT_ENSURE_REGISTERED(ErgrEmhcPublishedRouting);

namespace
{
double
Dist(const Vector& a, const Vector& b)
{
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
} // namespace

TypeId
ErgrEmhcPublishedRouting::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::ErgrEmhcPublishedRouting")
            .SetParent<AquaSimRouting>()
            .AddConstructor<ErgrEmhcPublishedRouting>()
            .AddAttribute("CellSizeM", "Cell side d = R / (2 sqrt 3) (m).", DoubleValue(params::p0::kCellSizeM),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_cellSizeM), MakeDoubleChecker<double>(1.0))
            .AddAttribute("BoxSizeM", "Box side (m).", DoubleValue(params::grid::kBoxSizeM),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_boxSizeM), MakeDoubleChecker<double>(1.0))
            .AddAttribute("TxRangeM", "Range R (m), for the waiting time T_w.", DoubleValue(params::phy::kTxRangeM),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_txRangeM), MakeDoubleChecker<double>(1.0))
            .AddAttribute("Beta", "Weight of Hmin in Hpkt.", DoubleValue(params::p0::kBeta),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_beta), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("HmaxFactor", "Hmax = factor x Hmin [CONFIRM].", DoubleValue(params::p0::kHmaxFactor),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_hmaxFactor), MakeDoubleChecker<double>(1.0))
            .AddAttribute("IPeriodS", "Initialisation period (s).", DoubleValue(params::p0::kIPeriodS),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_iPeriodS), MakeDoubleChecker<double>(0.1))
            .AddAttribute("UpdatePeriodS", "Update (Hello) period of a head (s).", DoubleValue(params::p0::kUpdatePeriodS),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_updatePeriodS), MakeDoubleChecker<double>(1.0))
            .AddAttribute("NewElectionS", "New election timer (s).", DoubleValue(params::p0::kNewElectionS),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_newElectionS), MakeDoubleChecker<double>(1.0))
            .AddAttribute("EnergyThresholdJ", "A head below this energy sends Retire (J).",
                          DoubleValue(params::p0::kEnergyThresholdJ),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_energyThresholdJ), MakeDoubleChecker<double>(0.0))
            .AddAttribute("PriorityWeight", "w in the election priority P(i) [CONFIRM].", DoubleValue(params::p0::kPriorityWeight),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_weight), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("RandomTimers", "Election timers are random (GBPR style) instead of iPeriod x P(i).",
                          BooleanValue(params::p0::kRandomTimers),
                          MakeBooleanAccessor(&ErgrEmhcPublishedRouting::m_randomTimers), MakeBooleanChecker())
            .AddAttribute("PositionCheckS", "How often a node checks whether it left its cell (s).",
                          DoubleValue(params::p0::kPositionCheckS),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_positionCheckS), MakeDoubleChecker<double>(0.1))
            .AddAttribute("VoidRetryLimit", "NegAck-triggered re-selections.", UintegerValue(params::p0::kVoidRetryLimit),
                          MakeUintegerAccessor(&ErgrEmhcPublishedRouting::m_voidRetryLimit), MakeUintegerChecker<int>(0))
            .AddAttribute("DataStartS", "Traffic starts at this time (s).", DoubleValue(params::timers::kDataStartS),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_dataStartS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("PktRatePerNodeHz", "Poisson data rate of a source (packets/s).",
                          DoubleValue(params::scenario::kPktRatePerNodeHz),
                          MakeDoubleAccessor(&ErgrEmhcPublishedRouting::m_pktRateHz), MakeDoubleChecker<double>(0.0))
            .AddAttribute("PayloadBytes", "Data payload (B).", UintegerValue(params::scenario::kPayloadBytes),
                          MakeUintegerAccessor(&ErgrEmhcPublishedRouting::m_payloadBytes), MakeUintegerChecker<uint32_t>());
    return tid;
}

ErgrEmhcPublishedRouting::ErgrEmhcPublishedRouting()
    : m_cellSizeM(params::p0::kCellSizeM),
      m_boxSizeM(params::grid::kBoxSizeM),
      m_txRangeM(params::phy::kTxRangeM),
      m_beta(params::p0::kBeta),
      m_hmaxFactor(params::p0::kHmaxFactor),
      m_iPeriodS(params::p0::kIPeriodS),
      m_updatePeriodS(params::p0::kUpdatePeriodS),
      m_newElectionS(params::p0::kNewElectionS),
      m_energyThresholdJ(params::p0::kEnergyThresholdJ),
      m_weight(params::p0::kPriorityWeight),
      m_randomTimers(params::p0::kRandomTimers),
      m_positionCheckS(params::p0::kPositionCheckS),
      m_voidRetryLimit(params::p0::kVoidRetryLimit),
      m_dataStartS(params::timers::kDataStartS),
      m_pktRateHz(params::scenario::kPktRatePerNodeHz),
      m_payloadBytes(params::scenario::kPayloadBytes)
{
    m_uniform = CreateObject<UniformRandomVariable>();
    m_interArrival = CreateObject<ExponentialRandomVariable>();
}

void
ErgrEmhcPublishedRouting::DoDispose()
{
    m_metrics = nullptr;
    m_sent.clear();
    AquaSimRouting::DoDispose();
}

int64_t
ErgrEmhcPublishedRouting::AssignStreams(int64_t stream)
{
    m_uniform->SetStream(stream);
    m_interArrival->SetStream(stream + 1);
    return 2;
}

GridGeometry&
ErgrEmhcPublishedRouting::Grid() const
{
    if (!m_grid)
    {
        m_grid = std::make_unique<GridGeometry>(m_boxSizeM, m_cellSizeM);
    }
    return *m_grid;
}

bool
ErgrEmhcPublishedRouting::IsAlive()
{
    Ptr<AquaSimEnergyModel> energy = GetNetDevice()->GetPhy()->EM();
    return !energy || energy->GetEnergy() > 0.0;
}

void
ErgrEmhcPublishedRouting::Kill()
{
    m_metrics->OnEvent(m_id, "NODE_KILLED", "cell=" + std::to_string(m_cell) + " head=" + std::to_string(m_isHead));
    GetNetDevice()->GetPhy()->EM()->SetEnergy(0.0);
}

double
ErgrEmhcPublishedRouting::ResidualFraction()
{
    Ptr<AquaSimEnergyModel> energy = GetNetDevice()->GetPhy()->EM();
    if (!energy || energy->GetInitialEnergy() <= 0.0)
    {
        return 1.0;
    }
    return std::max(0.0, std::min(1.0, energy->GetEnergy() / energy->GetInitialEnergy()));
}

Vector
ErgrEmhcPublishedRouting::TruePosition() const
{
    // The paper assumes every node knows its location (§3): no localisation noise in P0.
    return const_cast<ErgrEmhcPublishedRouting*>(this)->GetNetDevice()->GetNode()->GetObject<MobilityModel>()->GetPosition();
}

// ============================================================== start-up ===

void
ErgrEmhcPublishedRouting::BuildHminTable()
{
    std::vector<Vector> sinkPositions;
    for (const auto& sink : m_sinks)
    {
        sinkPositions.push_back(sink.second);
    }
    m_hmin = Grid().HminTable(sinkPositions);
}

void
ErgrEmhcPublishedRouting::Start()
{
    m_id = static_cast<uint16_t>(GetNetDevice()->GetNode()->GetId());
    m_started = true;
    m_seen = std::make_unique<RecentSet>(params::timers::kDuplicateCapacity, Seconds(params::timers::kDuplicateTtlS));
    BuildHminTable();
    m_cell = Grid().CellIdOf(TruePosition());
    if (m_isSink)
    {
        return; // sinks only receive
    }
    StartInitTimer();
    Simulator::Schedule(Seconds(m_positionCheckS), &ErgrEmhcPublishedRouting::CheckCell, this);
    if (m_pktRateHz > 0.0)
    {
        m_interArrival->SetAttribute("Mean", DoubleValue(1.0 / m_pktRateHz));
        Simulator::Schedule(Seconds(m_dataStartS + m_interArrival->GetValue()), &ErgrEmhcPublishedRouting::TrafficTick, this);
    }
}

// =============================================================== election ===

// Paper eq. (3): P(i) = w (1 - E/maxE) + (1 - w) D / maxD, maxD = d sqrt(3) / 2. Lower is better.
double
ErgrEmhcPublishedRouting::Priority()
{
    Vector centre = Grid().CellCentre(m_cell);
    double d = Dist(TruePosition(), centre) / Grid().MaxCentreDistanceM();
    return m_weight * (1.0 - ResidualFraction()) + (1.0 - m_weight) * std::min(1.0, d);
}

double
ErgrEmhcPublishedRouting::ElectionTimerS()
{
    // GBPR-style random timer, or the paper's priority timer iPeriod x P(i) (eq. 4).
    return m_iPeriodS * (m_randomTimers ? m_uniform->GetValue() : Priority());
}

void
ErgrEmhcPublishedRouting::StartInitTimer()
{
    m_initTimer.Cancel();
    m_initTimer = Simulator::Schedule(Seconds(ElectionTimerS()), &ErgrEmhcPublishedRouting::OnInitTimer, this);
}

void
ErgrEmhcPublishedRouting::OnInitTimer()
{
    if (!IsAlive() || m_isHead || m_cellHead >= 0)
    {
        return; // somebody else announced itself first
    }
    BecomeHead();
}

void
ErgrEmhcPublishedRouting::ArmElectionTimer()
{
    m_electionTimer.Cancel();
    m_electionTimer = Simulator::Schedule(Seconds(m_newElectionS + ElectionTimerS()),
                                          &ErgrEmhcPublishedRouting::OnElectionTimer, this);
}

void
ErgrEmhcPublishedRouting::OnElectionTimer()
{
    if (!IsAlive() || m_isHead)
    {
        return;
    }
    BecomeHead(); // no Hello arrived since the timer was armed: compete for the next period
}

void
ErgrEmhcPublishedRouting::BecomeHead()
{
    m_initTimer.Cancel();
    m_electionTimer.Cancel();
    if (!m_isHead)
    {
        m_isHead = true;
        m_metrics->OnHeadChange(m_id, true, m_cell);
        m_metrics->OnEvent(m_id, "HEAD_ELECTED", "cell=" + std::to_string(m_cell) + " ");
    }
    m_cellHead = m_id;
    SendControl(ErgrElectionHeader::Hello, 0);
    m_updateTimer.Cancel();
    m_updateTimer = Simulator::Schedule(Seconds(m_updatePeriodS), &ErgrEmhcPublishedRouting::OnUpdateTimer, this);
}

void
ErgrEmhcPublishedRouting::StepDown(const char* reason)
{
    m_updateTimer.Cancel();
    if (!m_isHead)
    {
        return;
    }
    m_isHead = false;
    m_metrics->OnHeadChange(m_id, false, m_cell);
    m_metrics->OnEvent(m_id, "HEAD_RETIRED", reason);
}

// A head renews its Hello every update period if it still has energy; otherwise it retires.
void
ErgrEmhcPublishedRouting::OnUpdateTimer()
{
    if (!IsAlive() || !m_isHead)
    {
        return;
    }
    if (GetNetDevice()->GetPhy()->EM()->GetEnergy() > m_energyThresholdJ)
    {
        SendControl(ErgrElectionHeader::Hello, 0);
        m_updateTimer = Simulator::Schedule(Seconds(m_updatePeriodS), &ErgrEmhcPublishedRouting::OnUpdateTimer, this);
        return;
    }
    SendControl(ErgrElectionHeader::Retire, 0);
    StepDown("energy below threshold");
    m_cellHead = -1;
    ArmElectionTimer();
}

void
ErgrEmhcPublishedRouting::SendControl(ErgrElectionHeader::Kind kind, uint16_t newCell)
{
    ErgrElectionHeader hdr;
    hdr.kind = kind;
    hdr.nodeId = m_id;
    hdr.cellId = m_cell;
    hdr.rank = m_hmin[m_cell];                                  // Hello carries Hmin
    hdr.scoreQ = (kind == ErgrElectionHeader::Exit) ? newCell : 0; // Exit carries the new cell
    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(hdr);
    TransmitFrame(packet, FrameType::Election, -1);
}

void
ErgrEmhcPublishedRouting::OnControl(const ErgrElectionHeader& hdr)
{
    if (hdr.nodeId == m_id)
    {
        return;
    }
    const uint16_t cell = hdr.cellId;
    if (hdr.kind == ErgrElectionHeader::Hello)
    {
        if (cell == m_cell)
        {
            m_cellHead = hdr.nodeId;      // "the last Hello received is assumed to be the cell-head"
            if (m_isHead)
            {
                StepDown("Hello of another node in my cell");
            }
            m_initTimer.Cancel();
            ArmElectionTimer();           // compete again for the next period
        }
        else if (Grid().AreNeighboursPaper(m_cell, cell))
        {
            HeadEntry& entry = m_heads[cell];
            if (entry.nodeId != hdr.nodeId)
            {
                entry = HeadEntry{hdr.nodeId, Seconds(0)};
            }
        }
        return;
    }
    // Retire or Exit: the node is no longer the head of 'cell'.
    if (cell == m_cell)
    {
        if (m_cellHead == hdr.nodeId)
        {
            m_cellHead = -1;
            if (!m_isHead)
            {
                m_electionTimer.Cancel();
                StartInitTimer(); // local nodes start competing again
            }
        }
    }
    else
    {
        auto it = m_heads.find(cell);
        if (it != m_heads.end() && it->second.nodeId == hdr.nodeId)
        {
            m_heads.erase(it);
        }
    }
}

// Nodes check their position regularly; on leaving a cell they broadcast Exit (paper §4.5.2).
void
ErgrEmhcPublishedRouting::CheckCell()
{
    if (!IsAlive())
    {
        if (!m_deathLogged)
        {
            m_deathLogged = true;
            m_metrics->OnEvent(m_id, "NODE_DIED", "energy exhausted");
            if (m_isHead)
            {
                m_isHead = false;
                m_metrics->OnHeadChange(m_id, false, m_cell);
            }
        }
        return;
    }
    uint16_t now = Grid().CellIdOf(TruePosition());
    if (now != m_cell)
    {
        const bool wasHead = m_isHead;
        if (wasHead)
        {
            // Only a head announces its departure: the other nodes are in nobody's table, so an Exit from them would
            // only add collisions. [CONFIRM] the paper says "a node" (§4.5.2); see CHANGELOG.
            SendControl(ErgrElectionHeader::Exit, now);
            StepDown("left cell");
        }
        m_metrics->OnEvent(m_id, "CELL_CHANGED", "from=" + std::to_string(m_cell) + " to=" + std::to_string(now));
        m_cell = now;
        m_initTimer.Cancel();
        m_electionTimer.Cancel();
        // The new cell was a neighbour before, so the node knows whether it has a head.
        auto it = m_heads.find(now);
        m_cellHead = (it != m_heads.end()) ? it->second.nodeId : -1;
        if (it != m_heads.end())
        {
            m_heads.erase(it);
        }
        for (auto h = m_heads.begin(); h != m_heads.end();) // keep only the neighbours of the new cell
        {
            h = Grid().AreNeighboursPaper(m_cell, h->first) ? std::next(h) : m_heads.erase(h);
        }
        if (m_cellHead < 0)
        {
            BecomeHead(); // "if there is no cell-head in the new cell, the node considers itself the cell-head"
        }
        else
        {
            ArmElectionTimer();
        }
    }
    Simulator::Schedule(Seconds(m_positionCheckS), &ErgrEmhcPublishedRouting::CheckCell, this);
}

// ============================================================== forwarding ===

void
ErgrEmhcPublishedRouting::TrafficTick()
{
    if (!IsAlive())
    {
        return;
    }
    OriginatePacket();
    Simulator::Schedule(Seconds(m_interArrival->GetValue()), &ErgrEmhcPublishedRouting::TrafficTick, this);
}

void
ErgrEmhcPublishedRouting::OriginatePacket()
{
    SentRecord rec;
    rec.key = E2EKey{m_id, ++m_ownSeq};
    rec.genTimeMs = static_cast<uint32_t>(Simulator::Now().GetMilliSeconds());
    const double hmin = m_hmin[m_cell];
    const double hmax = m_hmaxFactor * hmin;
    rec.hpkt = static_cast<int>(std::round(m_beta * hmin + (1.0 - m_beta) * hmax)); // paper eq. (5)
    m_metrics->OnPacketCreated(rec.key, m_id);
    Forward(rec, true);
}

// Candidate cell for the next hop. Normal mode: NG1T entries (lower Hmin than the own cell) with Hmin <= Hpkt, not busy,
// round robin. Void mode (after a NegAck or with no G1 candidate): lowest Hmin of NG1T + NG2T that is neither the previous
// nor an already tried cell. Returns the cell id or -1.
int
ErgrEmhcPublishedRouting::ChooseNextCell(const SentRecord& rec, int hpkt, bool voidMode)
{
    const Time now = Simulator::Now();
    const uint8_t myHmin = m_hmin[m_cell];
    std::vector<std::pair<uint8_t, uint16_t>> candidates; // (Hmin, cell) sorted ascending like the paper's tables
    for (const auto& entry : m_heads)
    {
        const uint16_t cell = entry.first;
        if (static_cast<int>(cell) == rec.prevCell || rec.triedCells.count(cell) || entry.second.nodeId == m_id)
        {
            continue;
        }
        const uint8_t hm = m_hmin[cell];
        if (!voidMode && (hm >= myHmin || (hpkt > 0 && hm > hpkt)))
        {
            continue;
        }
        candidates.emplace_back(hm, cell);
    }
    if (candidates.empty())
    {
        return -1;
    }
    std::sort(candidates.begin(), candidates.end());
    if (voidMode || hpkt <= 0)
    {
        return candidates.front().second; // best (lowest Hmin) candidate
    }
    std::vector<uint16_t> idle;
    for (const auto& c : candidates)
    {
        if (m_heads.at(c.second).busyUntil <= now)
        {
            idle.push_back(c.second);
        }
    }
    if (idle.empty())
    {
        for (const auto& c : candidates)
        {
            idle.push_back(c.second); // everybody busy: use them anyway
        }
    }
    return idle[m_roundRobin++ % idle.size()];
}

void
ErgrEmhcPublishedRouting::Forward(SentRecord rec, bool isSource)
{
    if (!IsAlive())
    {
        m_metrics->OnDrop(m_id, rec.key, DropReason::NodeDiedHolding);
        return;
    }
    const uint8_t myHmin = m_hmin[m_cell];
    if (myHmin == 1)
    {
        // Within one hop of a sink: send the packet to the nearest sink directly.
        Vector me = TruePosition();
        const std::pair<uint16_t, Vector>* best = nullptr;
        for (const auto& sink : m_sinks)
        {
            if (!best || Dist(me, sink.second) < Dist(me, best->second))
            {
                best = &sink;
            }
        }
        if (!best)
        {
            Drop(rec, DropReason::NoEligibleForwarder);
            return;
        }
        rec.nextCell = -1;
        SendData(rec, best->first);
        return;
    }
    int cell = ChooseNextCell(rec, rec.hpkt, rec.voidRetries > 0);
    if (cell < 0 && rec.voidRetries == 0)
    {
        cell = ChooseNextCell(rec, rec.hpkt, true); // no G1 candidate: fall back to NG1T + NG2T
    }
    if (cell < 0)
    {
        if (rec.prevHop < 0)
        {
            Drop(rec, DropReason::NoEligibleForwarder);
        }
        else
        {
            SendNack(rec); // tell the previous hop to try another cell
        }
        return;
    }
    rec.nextCell = cell;
    SendData(rec, m_heads[cell].nodeId);
}

double
ErgrEmhcPublishedRouting::WaitingTimeS()
{
    // Paper eq. (10), with the extra airtime the Aqua-Sim PHY holds each frame (CONFIRM E17).
    Ptr<AquaSimPhyCmn> phy = DynamicCast<AquaSimPhyCmn>(GetNetDevice()->GetPhy());
    double airS = (1.0 + params::timers::kPhyRxDelayAirtimes) *
                  (phy->CalcTxTime(m_payloadBytes + 17).GetSeconds() + phy->CalcTxTime(9).GetSeconds());
    return airS + 2.0 * m_txRangeM / params::timers::kAckSoundSpeedMps + params::p0::kNackProcS;
}

void
ErgrEmhcPublishedRouting::SendData(SentRecord rec, uint16_t forwarderNode)
{
    const bool isSource = rec.prevHop < 0;
    ErgrDataHeader hdr;
    hdr.senderId = m_id;
    hdr.forwarderId = forwarderNode;
    hdr.sourceId = rec.key.source;
    hdr.sourceSeq = rec.key.seq;
    hdr.hpkt = static_cast<uint8_t>(std::max(0, isSource ? rec.hpkt - 1 : rec.hpkt)); // source writes Hpkt - 1
    hdr.hopSeq = m_cell;   // P0 has no hop sequence numbers: this field carries the paper's "previousCell"
    hdr.genTimeMs = rec.genTimeMs;
    Ptr<Packet> packet = Create<Packet>(m_payloadBytes);
    packet->AddHeader(hdr);
    m_metrics->OnDataSent(rec.key, m_id, rec.voidRetries > 0);
    Decision none;
    m_metrics->OnAttemptSent(m_id, forwarderNode, hdr.hopSeq, static_cast<uint8_t>(1 + rec.voidRetries), false, false,
                             false, 0.0, none);
    TransmitFrame(packet, FrameType::Data, forwarderNode);

    // Busy until T_w expires or a NegAck arrives; without a NegAck the hop counts as acknowledged (implicit ACK).
    if (rec.nextCell >= 0)
    {
        auto it = m_heads.find(rec.nextCell);
        if (it != m_heads.end())
        {
            it->second.busyUntil = Simulator::Now() + Seconds(WaitingTimeS());
        }
    }
    rec.tw.Cancel();
    E2EKey key = rec.key;
    rec.tw = Simulator::Schedule(Seconds(WaitingTimeS()), &ErgrEmhcPublishedRouting::OnWaitExpired, this, key);
    m_sent[key] = rec;
}

void
ErgrEmhcPublishedRouting::OnWaitExpired(E2EKey key)
{
    auto it = m_sent.find(key);
    if (it == m_sent.end())
    {
        return;
    }
    auto head = m_heads.find(it->second.nextCell);
    if (head != m_heads.end())
    {
        head->second.busyUntil = Seconds(0);
    }
    m_sent.erase(it);
}

void
ErgrEmhcPublishedRouting::SendNack(const SentRecord& rec)
{
    ErgrNackHeader nack;
    nack.nackerId = m_id;
    nack.upstreamId = static_cast<uint16_t>(rec.prevHop);
    nack.sourceId = rec.key.source;
    nack.sourceSeq = rec.key.seq;
    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(nack);
    m_metrics->OnEvent(m_id, "VOID_NACK", "pkt=" + std::to_string(rec.key.source) + ":" + std::to_string(rec.key.seq));
    TransmitFrame(packet, FrameType::Nack, rec.prevHop);
}

void
ErgrEmhcPublishedRouting::Drop(const SentRecord& rec, DropReason reason)
{
    m_metrics->OnDrop(m_id, rec.key, reason);
    auto it = m_sent.find(rec.key);
    if (it != m_sent.end())
    {
        it->second.tw.Cancel();
        m_sent.erase(it);
    }
}

void
ErgrEmhcPublishedRouting::OnData(const ErgrDataHeader& hdr)
{
    E2EKey key{hdr.sourceId, hdr.sourceSeq};
    m_metrics->OnHopReceived(hdr.senderId, m_id, hdr.hopSeq);
    if (!m_seen->Insert(key, Simulator::Now()))
    {
        m_metrics->OnDuplicate(m_id);
        return;
    }
    if (m_isSink)
    {
        m_metrics->OnDelivered(key, m_id);
        return;
    }
    m_metrics->OnPacketAtNode(key, m_id);
    SentRecord rec;
    rec.key = key;
    rec.genTimeMs = hdr.genTimeMs;
    rec.hpkt = static_cast<int>(hdr.hpkt) - 1;   // paper Fig. 11: Pkt.Hpkt = Pkt.Hpkt - 1 at an intermediate forwarder
    rec.prevCell = hdr.hopSeq;
    rec.prevHop = hdr.senderId;
    Forward(rec, false);
}

void
ErgrEmhcPublishedRouting::OnNack(const ErgrNackHeader& hdr)
{
    if (hdr.upstreamId != m_id)
    {
        return;
    }
    E2EKey key{hdr.sourceId, hdr.sourceSeq};
    auto it = m_sent.find(key);
    if (it == m_sent.end())
    {
        return; // T_w expired: the hop had already counted as acknowledged
    }
    SentRecord rec = it->second;
    rec.tw.Cancel();
    m_sent.erase(it);
    auto head = m_heads.find(rec.nextCell);
    if (head != m_heads.end())
    {
        head->second.busyUntil = Seconds(0); // reset the Busy flag
    }
    rec.triedCells.insert(static_cast<uint16_t>(rec.nextCell));
    rec.voidRetries++;
    if (rec.voidRetries > m_voidRetryLimit)
    {
        Drop(rec, DropReason::MaxTxReached);
        return;
    }
    Forward(rec, rec.prevHop < 0);
}

// ================================================================ frame I/O ===

void
ErgrEmhcPublishedRouting::TransmitFrame(Ptr<Packet> packet, FrameType type, int32_t receiver)
{
    uint32_t bytes = packet->GetSize();
    AquaSimHeader ash;
    ash.SetSize(bytes);
    ash.SetDAddr(AquaSimAddress::GetBroadcast());
    ash.SetNextHop(AquaSimAddress::GetBroadcast());
    ash.SetSAddr(AquaSimAddress::ConvertFrom(GetNetDevice()->GetAddress()));
    ash.SetDirection(AquaSimHeader::DOWN);
    ash.SetErrorFlag(false);
    ash.SetNumForwards(0);
    packet->AddHeader(ash);
    m_metrics->OnFrameTx(m_id, type, bytes, params::phy::kNumPowerLevels - 1, receiver);
    SendDown(packet, AquaSimAddress::GetBroadcast(), Seconds(0));
}

bool
ErgrEmhcPublishedRouting::Recv(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
    if (!m_started || !IsAlive())
    {
        return false;
    }
    AquaSimHeader ash;
    packet->RemoveHeader(ash);
    if (ash.GetDirection() == AquaSimHeader::DOWN)
    {
        return false;
    }
    const uint32_t bytes = packet->GetSize();
    switch (static_cast<FrameType>(PeekFrameType(packet)))
    {
    case FrameType::Data: {
        ErgrDataHeader hdr;
        packet->RemoveHeader(hdr);
        bool mine = hdr.forwarderId == m_id;
        m_metrics->OnFrameRx(m_id, FrameType::Data, bytes, mine, hdr.senderId);
        if (mine)
        {
            OnData(hdr);
        }
        break;
    }
    case FrameType::Election: {
        ErgrElectionHeader hdr;
        packet->RemoveHeader(hdr);
        m_metrics->OnFrameRx(m_id, FrameType::Election, bytes, true, hdr.nodeId);
        if (!m_isSink)
        {
            OnControl(hdr);
        }
        break;
    }
    case FrameType::Nack: {
        ErgrNackHeader hdr;
        packet->RemoveHeader(hdr);
        bool mine = hdr.upstreamId == m_id;
        m_metrics->OnFrameRx(m_id, FrameType::Nack, bytes, mine, hdr.nackerId);
        OnNack(hdr);
        break;
    }
    default:
        break;
    }
    return true;
}

} // namespace cbergr
} // namespace ns3
