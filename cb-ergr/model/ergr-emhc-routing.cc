/*
 * ergr-emhc-routing.cc: ERGR-EMHC-g per-node routing (spec §5, §9; kickoff §2.1).
 * Sections: self state, beacons, election, traffic and forwarding, frame I/O.
 * All forwarding decisions go through the ForwarderSelector; this file never checks which one it is.
 */

#include "ergr-emhc-routing.h"

#include "cb-ergr-params.h"

#include "ns3/aqua-sim-address.h"
#include "ns3/aqua-sim-header.h"
#include "ns3/aqua-sim-net-device.h"
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

NS_LOG_COMPONENT_DEFINE("ErgrEmhcRouting");
NS_OBJECT_ENSURE_REGISTERED(ErgrEmhcRouting);

namespace
{
constexpr int32_t kNone = -1;
const uint8_t kUnknownHmin = 255;

double
Distance(const Vector& a, const Vector& b)
{
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

int16_t
ToInt16(double v)
{
    return static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, std::round(v))));
}
} // namespace

TypeId
ErgrEmhcRouting::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::ErgrEmhcRouting")
            .SetParent<AquaSimRouting>()
            .AddConstructor<ErgrEmhcRouting>()
            .AddAttribute("CellSizeM", "Cell side (m).", DoubleValue(params::grid::kCellSizeM),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_cellSizeM), MakeDoubleChecker<double>(1.0))
            .AddAttribute("BoxSizeM", "Box side (m).", DoubleValue(params::grid::kBoxSizeM),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_boxSizeM), MakeDoubleChecker<double>(1.0))
            .AddAttribute("TxRangeM", "Maximum range R used to filter forwarders (m).",
                          DoubleValue(params::phy::kTxRangeM),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_txRangeM), MakeDoubleChecker<double>(1.0))
            .AddAttribute("HeadsPerCell", "Heads elected per cell (k).", UintegerValue(params::grid::kHeadsPerCell),
                          MakeUintegerAccessor(&ErgrEmhcRouting::m_headsPerCell), MakeUintegerChecker<int>(1, 7))
            .AddAttribute("FaceOnly", "Only the 6 face-adjacent cells count as neighbours.",
                          BooleanValue(params::grid::kFaceOnly),
                          MakeBooleanAccessor(&ErgrEmhcRouting::m_faceOnly), MakeBooleanChecker())
            .AddAttribute("StrictProgress", "Require a candidate to be closer in hops or shallower (loop guard).",
                          BooleanValue(params::grid::kStrictProgress),
                          MakeBooleanAccessor(&ErgrEmhcRouting::m_strictProgress), MakeBooleanChecker())
            .AddAttribute("BeaconPeriodS", "Beacon interval (s).", DoubleValue(params::timers::kBeaconPeriodS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_beaconPeriodS), MakeDoubleChecker<double>(0.1))
            .AddAttribute("NeighbourTimeoutBeacons", "A neighbour row expires after this many beacon periods.",
                          DoubleValue(params::timers::kNeighbourTimeoutBeacons),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_neighbourTimeoutBeacons),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("ElectionPeriodS", "Periodic election interval (s).",
                          DoubleValue(params::timers::kElectionPeriodS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_electionPeriodS), MakeDoubleChecker<double>(1.0))
            .AddAttribute("ElectionBeta", "Weight of residual energy in the head score.",
                          DoubleValue(params::election::kBeta),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_electionBeta), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("ElectionSlotS", "Length of one rank slot (s).", DoubleValue(params::election::kSlotS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_electionSlotS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("ElectionJitterS", "Random delay inside a slot (s).",
                          DoubleValue(params::election::kJitterMaxS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_electionJitterS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("ElectionOffsetPerCellS", "Elections of cell c start c * this later (s).",
                          DoubleValue(params::timers::kElectionOffsetPerCellS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_electionOffsetPerCellS),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("FirstElectionS", "Time of the start-up election (s).",
                          DoubleValue(params::timers::kFirstElectionS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_firstElectionS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("HysteresisM", "A node changes cell only when this far inside the new one (m).",
                          DoubleValue(params::election::kMembershipHysteresisM),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_hysteresisM), MakeDoubleChecker<double>(0.0))
            .AddAttribute("NackWindowS", "How long a forwarded packet stays remembered for void NACKs (s).",
                          DoubleValue(params::timers::kNackWindowS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_nackWindowS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("VoidRetryLimit", "NACK-triggered re-selections before dropping.",
                          UintegerValue(params::timers::kVoidRetryLimit),
                          MakeUintegerAccessor(&ErgrEmhcRouting::m_voidRetryLimit), MakeUintegerChecker<int>(0))
            .AddAttribute("DataStartS", "Traffic starts at this time (s).", DoubleValue(params::timers::kDataStartS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_dataStartS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("PktRatePerNodeHz", "Poisson data rate per node (packets/s).",
                          DoubleValue(params::scenario::kPktRatePerNodeHz),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_pktRateHz), MakeDoubleChecker<double>(0.0))
            .AddAttribute("PayloadBytes", "Data payload (B).", UintegerValue(params::scenario::kPayloadBytes),
                          MakeUintegerAccessor(&ErgrEmhcRouting::m_payloadBytes), MakeUintegerChecker<uint32_t>())
            .AddAttribute("DuplicateCapacity", "Duplicate filter size.", UintegerValue(params::timers::kDuplicateCapacity),
                          MakeUintegerAccessor(&ErgrEmhcRouting::m_duplicateCapacity), MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("DuplicateTtlS", "Duplicate filter lifetime (s).", DoubleValue(params::timers::kDuplicateTtlS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_duplicateTtlS), MakeDoubleChecker<double>(1.0));
    return tid;
}

ErgrEmhcRouting::ErgrEmhcRouting()
    : m_cellSizeM(params::grid::kCellSizeM),
      m_boxSizeM(params::grid::kBoxSizeM),
      m_txRangeM(params::phy::kTxRangeM),
      m_headsPerCell(params::grid::kHeadsPerCell),
      m_faceOnly(params::grid::kFaceOnly),
      m_strictProgress(params::grid::kStrictProgress),
      m_beaconPeriodS(params::timers::kBeaconPeriodS),
      m_neighbourTimeoutBeacons(params::timers::kNeighbourTimeoutBeacons),
      m_electionPeriodS(params::timers::kElectionPeriodS),
      m_electionBeta(params::election::kBeta),
      m_electionSlotS(params::election::kSlotS),
      m_electionJitterS(params::election::kJitterMaxS),
      m_electionOffsetPerCellS(params::timers::kElectionOffsetPerCellS),
      m_firstElectionS(params::timers::kFirstElectionS),
      m_hysteresisM(params::election::kMembershipHysteresisM),
      m_nackWindowS(params::timers::kNackWindowS),
      m_voidRetryLimit(params::timers::kVoidRetryLimit),
      m_dataStartS(params::timers::kDataStartS),
      m_pktRateHz(params::scenario::kPktRatePerNodeHz),
      m_payloadBytes(params::scenario::kPayloadBytes),
      m_duplicateCapacity(params::timers::kDuplicateCapacity),
      m_duplicateTtlS(params::timers::kDuplicateTtlS)
{
    m_uniform = CreateObject<UniformRandomVariable>();
    m_interArrival = CreateObject<ExponentialRandomVariable>();
}

void
ErgrEmhcRouting::DoDispose()
{
    m_selector = nullptr;
    m_metrics = nullptr;
    m_localisation = nullptr;
    m_sent.clear();
    AquaSimRouting::DoDispose();
}

int64_t
ErgrEmhcRouting::AssignStreams(int64_t stream)
{
    m_uniform->SetStream(stream);
    m_interArrival->SetStream(stream + 1);
    return 2;
}

void
ErgrEmhcRouting::SetNodeRole(bool isSink)
{
    m_isSink = isSink;
}

GridGeometry&
ErgrEmhcRouting::Grid() const
{
    if (!m_grid)
    {
        m_grid = std::make_unique<GridGeometry>(m_boxSizeM, m_cellSizeM);
    }
    return *m_grid;
}

bool
ErgrEmhcRouting::IsAlive()
{
    Ptr<AquaSimEnergyModel> energy = GetNetDevice()->GetPhy()->EM();
    return !energy || energy->GetEnergy() > 0.0;
}

double
ErgrEmhcRouting::ResidualFraction()
{
    Ptr<AquaSimEnergyModel> energy = GetNetDevice()->GetPhy()->EM();
    if (!energy || energy->GetInitialEnergy() <= 0.0)
    {
        return 1.0;
    }
    return std::max(0.0, std::min(1.0, energy->GetEnergy() / energy->GetInitialEnergy()));
}

// =========================================================== start-up ======

void
ErgrEmhcRouting::Start()
{
    NS_ASSERT_MSG(m_selector, "selector must be set before Start()");
    m_id = static_cast<uint16_t>(GetNetDevice()->GetNode()->GetId());
    m_started = true;
    m_seen = std::make_unique<RecentSet>(m_duplicateCapacity, Seconds(m_duplicateTtlS));
    m_hmin = m_isSink ? 0 : kUnknownHmin;
    UpdateSelfEstimate();
    UpdateOwnCell();

    // Beacons: random first phase so nodes do not all transmit together (spec §5.2).
    Simulator::Schedule(Seconds(m_uniform->GetValue(0.0, m_beaconPeriodS)), &ErgrEmhcRouting::BeaconTick, this);

    if (!m_isSink)
    {
        double electionAt = m_firstElectionS + m_cell * m_electionOffsetPerCellS;
        Simulator::Schedule(Seconds(electionAt), &ErgrEmhcRouting::OnElectionEpoch, this);
        if (m_pktRateHz > 0.0)
        {
            m_interArrival->SetAttribute("Mean", DoubleValue(1.0 / m_pktRateHz));
            Simulator::Schedule(Seconds(m_dataStartS + m_interArrival->GetValue()),
                                &ErgrEmhcRouting::TrafficTick, this);
        }
    }
}

// ========================================================= self state ======

void
ErgrEmhcRouting::UpdateSelfEstimate()
{
    Ptr<MobilityModel> mobility = GetNetDevice()->GetNode()->GetObject<MobilityModel>();
    Vector truePos = mobility->GetPosition();
    Vector trueVel = mobility->GetVelocity();
    if (m_localisation)
    {
        m_estPos = m_localisation->NoisyPosition(truePos);
        m_estVel = m_localisation->NoisyVelocity(trueVel);
    }
    else
    {
        m_estPos = truePos;
        m_estVel = trueVel;
    }
    m_estTime = Simulator::Now();
}

Vector
ErgrEmhcRouting::EstimatedPosition() const
{
    double dt = (Simulator::Now() - m_estTime).GetSeconds();
    return Vector(m_estPos.x + m_estVel.x * dt, m_estPos.y + m_estVel.y * dt, m_estPos.z + m_estVel.z * dt);
}

// Cell membership with hysteresis (D§3 P3): move to a new cell only when well inside it.
void
ErgrEmhcRouting::UpdateOwnCell()
{
    Vector pos = EstimatedPosition();
    uint16_t candidate = Grid().CellIdOf(pos);
    if (!m_cellKnown)
    {
        m_cell = candidate;
        m_cellKnown = true;
        return;
    }
    if (candidate == m_cell || Grid().InsideMarginM(pos, candidate) < m_hysteresisM)
    {
        return;
    }
    if (m_isHead)
    {
        BecomeMember("left cell");
    }
    std::ostringstream details;
    details << "from=" << m_cell << " to=" << candidate;
    m_metrics->OnEvent(m_id, "CELL_CHANGED", details.str());
    m_cell = candidate;
    m_selector->OnKnownChange(ChangeEvent::OwnCellChanged, Simulator::Now());
    if (!m_isSink && CountHeadsInCell() < m_headsPerCell)
    {
        StartElectionRound("joined cell without heads", false);
    }
}

// Spec §5.2: Hmin = 1 + the smallest Hmin among reachable G1 heads (or sinks); sinks have 0.
void
ErgrEmhcRouting::RecomputeHmin()
{
    if (m_isSink)
    {
        m_hmin = 0;
        return;
    }
    Time now = Simulator::Now();
    Vector myPos = EstimatedPosition();
    uint8_t best = kUnknownHmin;
    for (const auto& entry : m_table.Rows())
    {
        const NeighbourEntry& row = entry.second;
        bool inG1 = row.isSink || (row.isHead && Grid().AreNeighbours(m_cell, row.cellId, m_faceOnly));
        if (!inG1 || row.hmin >= best)
        {
            continue;
        }
        if (Distance(row.PredictedPos(now), myPos) <= m_txRangeM)
        {
            best = row.hmin;
        }
    }
    uint8_t updated = (best == kUnknownHmin) ? kUnknownHmin : static_cast<uint8_t>(best + 1);
    if (updated != m_hmin)
    {
        std::ostringstream details;
        details << "from=" << int(m_hmin) << " to=" << int(updated);
        m_metrics->OnEvent(m_id, "HMIN_CHANGED", details.str());
        m_hmin = updated;
    }
}

// ============================================================ beacons ======

void
ErgrEmhcRouting::BeaconTick()
{
    if (!IsAlive())
    {
        if (!m_deathLogged)
        {
            m_deathLogged = true;
            m_metrics->OnEvent(m_id, "NODE_DIED", "energy exhausted");
        }
        return; // a dead node stops all activity
    }
    UpdateSelfEstimate();
    UpdateOwnCell();
    ExpireNeighbours();
    RecomputeHmin();
    SendBeacon();
    Simulator::Schedule(Seconds(m_beaconPeriodS), &ErgrEmhcRouting::BeaconTick, this);
}

void
ErgrEmhcRouting::SendBeacon()
{
    ErgrBeaconHeader hdr;
    hdr.nodeId = m_id;
    hdr.cellId = m_cell;
    hdr.SetRole(m_isHead, m_rank, m_isSink);
    hdr.hmin = m_hmin;
    hdr.pos[0] = ToInt16(m_estPos.x);
    hdr.pos[1] = ToInt16(m_estPos.y);
    hdr.pos[2] = ToInt16(m_estPos.z);
    hdr.velMmps[0] = ToInt16(m_estVel.x * 1000.0);
    hdr.velMmps[1] = ToInt16(m_estVel.y * 1000.0);
    hdr.velMmps[2] = ToInt16(m_estVel.z * 1000.0);
    hdr.battery = static_cast<uint8_t>(std::round(ResidualFraction() * 255.0));
    hdr.queueLen = static_cast<uint8_t>(std::min<size_t>(255, m_sent.size()));
    hdr.nextBeaconDs = static_cast<uint16_t>(std::round(m_beaconPeriodS * 10.0));

    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(hdr);
    TransmitFrame(packet, FrameType::Beacon, kNone, params::phy::kNumPowerLevels - 1);
}

void
ErgrEmhcRouting::ExpireNeighbours()
{
    // Remember which heads of my own cell we had, to notice when one disappears.
    std::set<uint16_t> headsBefore;
    for (const auto& entry : m_table.Rows())
    {
        if (entry.second.isHead && entry.second.cellId == m_cell)
        {
            headsBefore.insert(entry.first);
        }
    }
    Time timeout = Seconds(m_neighbourTimeoutBeacons * m_beaconPeriodS);
    std::vector<uint16_t> removed = m_table.Expire(Simulator::Now(), timeout);
    bool lostOwnCellHead = false;
    for (uint16_t id : removed)
    {
        m_selector->OnKnownChange(ChangeEvent::NodeLost, Simulator::Now());
        lostOwnCellHead = lostOwnCellHead || headsBefore.count(id) > 0;
    }
    if (lostOwnCellHead && !m_isSink)
    {
        StartElectionRound("head of my cell lost", false);
    }
}

void
ErgrEmhcRouting::OnBeacon(const ErgrBeaconHeader& hdr)
{
    NeighbourEntry entry;
    entry.id = hdr.nodeId;
    entry.cellId = hdr.cellId;
    entry.isHead = hdr.IsHead();
    entry.headRank = hdr.HeadRank();
    entry.isSink = hdr.IsSink();
    entry.hmin = hdr.hmin;
    entry.pos = Vector(hdr.pos[0], hdr.pos[1], hdr.pos[2]);
    entry.vel = Vector(hdr.velMmps[0] / 1000.0, hdr.velMmps[1] / 1000.0, hdr.velMmps[2] / 1000.0);
    entry.beaconTime = Simulator::Now();
    entry.battery = hdr.battery / 255.0;
    entry.queueLen = hdr.queueLen;
    entry.nextBeaconIn = Seconds(hdr.nextBeaconDs / 10.0);

    bool wasHeadInMyCell = false;
    if (const NeighbourEntry* old = m_table.Find(hdr.nodeId))
    {
        wasHeadInMyCell = old->isHead && old->cellId == m_cell;
    }
    TableUpdateResult result = m_table.Update(entry);

    if (result.headStatusChanged && entry.cellId != m_cell && !entry.isSink)
    {
        m_selector->OnKnownChange(ChangeEvent::HeadChangedInNeighbourCell, Simulator::Now());
    }
    bool headLeftMyCell = wasHeadInMyCell && (!entry.isHead || entry.cellId != m_cell);
    if (headLeftMyCell && !m_isSink)
    {
        StartElectionRound("head left my cell", false);
    }
    if (entry.isHead && entry.cellId == m_cell && m_isHead && CountBetterHeadsInCell() >= m_headsPerCell)
    {
        BecomeMember("outranked by better heads");
    }
}

// =========================================================== election ======

// D§3 P3 score: beta * E/E0 + (1 - beta) * (1 - dc/dmax). Higher is better.
double
ErgrEmhcRouting::ScoreOf(double batteryFraction, const Vector& pos, uint16_t cell, bool firstElection) const
{
    double beta = firstElection ? 0.0 : m_electionBeta; // beta = 0 at start-up (D§3 P3)
    Vector centre = Grid().CellCentre(cell);
    double dmax = Grid().MaxCentreDistanceM();
    double centrality = 1.0 - std::min(1.0, Distance(pos, centre) / dmax);
    return beta * batteryFraction + (1.0 - beta) * centrality;
}

double
ErgrEmhcRouting::SelfScore(bool firstElection)
{
    return ScoreOf(ResidualFraction(), EstimatedPosition(), m_cell, firstElection);
}

// Rank of this node among the members of its cell (1 = best), from the beacons heard so far.
int
ErgrEmhcRouting::RankInCell(bool firstElection)
{
    Time now = Simulator::Now();
    double mine = SelfScore(firstElection);
    int better = 0;
    for (const auto& entry : m_table.Rows())
    {
        const NeighbourEntry& row = entry.second;
        if (row.isSink || row.cellId != m_cell)
        {
            continue;
        }
        double theirs = ScoreOf(row.battery, row.PredictedPos(now), row.cellId, firstElection);
        if (theirs > mine || (theirs == mine && row.id < m_id))
        {
            better++;
        }
    }
    return better + 1;
}

int
ErgrEmhcRouting::CountHeadsInCell()
{
    int count = 0;
    for (const auto& entry : m_table.Rows())
    {
        if (entry.second.isHead && entry.second.cellId == m_cell)
        {
            count++;
        }
    }
    return count;
}

int
ErgrEmhcRouting::CountBetterHeadsInCell()
{
    Time now = Simulator::Now();
    double mine = SelfScore(false);
    int better = 0;
    for (const auto& entry : m_table.Rows())
    {
        const NeighbourEntry& row = entry.second;
        if (!row.isHead || row.cellId != m_cell)
        {
            continue;
        }
        double theirs = ScoreOf(row.battery, row.PredictedPos(now), row.cellId, false);
        if (theirs > mine || (theirs == mine && row.id < m_id))
        {
            better++;
        }
    }
    return better;
}

void
ErgrEmhcRouting::OnElectionEpoch()
{
    if (!IsAlive())
    {
        return;
    }
    bool first = (m_electionEpochs == 0);
    m_electionEpochs++;
    UpdateSelfEstimate();
    UpdateOwnCell();
    StartElectionRound("periodic", first);
    Simulator::Schedule(Seconds(m_electionPeriodS), &ErgrEmhcRouting::OnElectionEpoch, this);
}

// Spec D§3 P3: nodes rank themselves; those ranked <= k claim in slot (rank-1) * 1 s + jitter.
void
ErgrEmhcRouting::StartElectionRound(const char* reason, bool firstElection)
{
    if (m_isSink || !IsAlive())
    {
        return;
    }
    Time now = Simulator::Now();
    bool periodic = std::string(reason) == "periodic";
    if (!periodic && (now - m_lastElectionTrigger).GetSeconds() < m_beaconPeriodS)
    {
        return; // debounce triggered elections
    }
    m_lastElectionTrigger = now;

    int rank = RankInCell(firstElection);
    if (rank > m_headsPerCell)
    {
        if (m_isHead)
        {
            BecomeMember("outranked at election");
        }
        return;
    }
    if (m_isHead && m_rank == rank)
    {
        return; // already holding this rank: nothing to claim
    }
    m_helloEvent.Cancel();
    double delayS = (rank - 1) * m_electionSlotS + m_uniform->GetValue(0.0, m_electionJitterS);
    m_electionPending = true;
    m_helloEvent = Simulator::Schedule(Seconds(delayS), &ErgrEmhcRouting::OnHelloSlot, this,
                                       static_cast<uint8_t>(rank));
}

void
ErgrEmhcRouting::OnHelloSlot(uint8_t rank)
{
    m_electionPending = false;
    if (!IsAlive() || m_isSink)
    {
        return;
    }
    BecomeHead(rank);
}

void
ErgrEmhcRouting::BecomeHead(uint8_t rank)
{
    bool wasHead = m_isHead;
    m_isHead = true;
    m_rank = rank;
    if (!wasHead)
    {
        m_metrics->OnHeadChange(m_id, true, m_cell);
        std::ostringstream details;
        details << "cell=" << m_cell << " rank=" << int(rank);
        m_metrics->OnEvent(m_id, "HEAD_ELECTED", details.str());
    }
    ErgrElectionHeader hdr;
    hdr.kind = ErgrElectionHeader::Hello;
    hdr.nodeId = m_id;
    hdr.cellId = m_cell;
    hdr.rank = rank;
    hdr.scoreQ = static_cast<uint16_t>(std::round(65535.0 * std::max(0.0, std::min(1.0, SelfScore(false)))));
    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(hdr);
    TransmitFrame(packet, FrameType::Election, kNone, params::phy::kNumPowerLevels - 1);
}

void
ErgrEmhcRouting::BecomeMember(const char* reason)
{
    if (!m_isHead)
    {
        return;
    }
    m_isHead = false;
    m_rank = 0;
    m_metrics->OnHeadChange(m_id, false, m_cell);
    m_metrics->OnEvent(m_id, "HEAD_RETIRED", reason);
    ErgrElectionHeader hdr;
    hdr.kind = ErgrElectionHeader::Retire;
    hdr.nodeId = m_id;
    hdr.cellId = m_cell;
    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(hdr);
    TransmitFrame(packet, FrameType::Election, kNone, params::phy::kNumPowerLevels - 1);
}

void
ErgrEmhcRouting::OnElectionFrame(const ErgrElectionHeader& hdr)
{
    if (hdr.nodeId == m_id)
    {
        return;
    }
    NeighbourEntry* row = m_table.FindMutable(hdr.nodeId);
    if (row)
    {
        row->isHead = (hdr.kind == ErgrElectionHeader::Hello);
        row->headRank = row->isHead ? hdr.rank : 0;
        row->cellId = hdr.cellId;
    }
    if (hdr.cellId != m_cell)
    {
        return;
    }
    if (hdr.kind == ErgrElectionHeader::Hello)
    {
        // Step-down rule (D§3 P3): a head that hears k better heads in its cell retires.
        if (m_isHead && CountBetterHeadsInCell() >= m_headsPerCell)
        {
            BecomeMember("outranked by better heads");
        }
    }
    else if (!m_isSink && CountHeadsInCell() < m_headsPerCell)
    {
        StartElectionRound("head retired", false);
    }
}

// ============================================================ traffic ======

void
ErgrEmhcRouting::TrafficTick()
{
    if (!IsAlive())
    {
        return;
    }
    OriginatePacket();
    Simulator::Schedule(Seconds(m_interArrival->GetValue()), &ErgrEmhcRouting::TrafficTick, this);
}

void
ErgrEmhcRouting::OriginatePacket()
{
    HeldPacket pkt;
    pkt.key = E2EKey{m_id, ++m_ownSeq};
    pkt.genTimeMs = static_cast<uint32_t>(Simulator::Now().GetMilliSeconds());
    pkt.prevHop = kNone;
    RecomputeHmin();
    // Spec §5.2: Hpkt = Hmin(source). A member's hop budget is set by the head it hands over to.
    pkt.hpkt = (m_hmin == kUnknownHmin) ? 0 : m_hmin;
    m_metrics->OnPacketCreated(pkt.key, m_id);
    if (m_hmin == kUnknownHmin && m_isHead)
    {
        Drop(pkt, DropReason::NoEligibleForwarder);
        return;
    }
    RoutePacket(pkt);
}

// ========================================================== forwarding =====

std::vector<Candidate>
ErgrEmhcRouting::GetEligible(const HeldPacket& pkt, Time now) const
{
    std::vector<Candidate> eligible;
    Vector myPos = EstimatedPosition();
    for (const auto& entry : m_table.Rows())
    {
        const NeighbourEntry& row = entry.second;
        if (row.id == m_id || static_cast<int32_t>(row.id) == pkt.prevHop || pkt.excluded.count(row.id))
        {
            continue;
        }
        bool inG1 = row.isSink ||
                    (row.isHead &&
                     Grid().AreNeighbours(m_cell, row.cellId, m_faceOnly));
        if (!inG1 || row.hmin > pkt.hpkt)
        {
            continue; // spec §5.1: Hmin_j <= Hpkt
        }
        Vector predicted = row.PredictedPos(now);
        if (Distance(predicted, myPos) > m_txRangeM)
        {
            continue; // K§2.1 predicted distance <= R
        }
        if (m_strictProgress && !row.isSink && !(row.hmin < m_hmin || predicted.z < myPos.z))
        {
            continue; // K§2.1 loop guard: closer in hops or shallower
        }
        eligible.push_back(Candidate{row.id, &row});
    }
    return eligible; // already sorted by id (map order)
}

void
ErgrEmhcRouting::RoutePacket(HeldPacket pkt)
{
    if (!IsAlive())
    {
        return;
    }
    RecomputeHmin();
    if (!m_isHead && !m_isSink)
    {
        HandOffToHead(pkt);
        return;
    }
    Time now = Simulator::Now();
    std::vector<Candidate> eligible = GetEligible(pkt, now);
    if (eligible.empty())
    {
        HandleVoid(pkt);
        return;
    }
    PacketCtx ctx;
    ctx.key = pkt.key;
    ctx.hpkt = pkt.hpkt;
    ctx.attempt = pkt.sentOnce ? 2 : 1;
    ctx.myId = m_id;
    ctx.myHmin = m_hmin;
    ctx.myPos = EstimatedPosition();
    ctx.myVel = m_estVel;
    ctx.myBattery = ResidualFraction();
    ctx.dataBytes = m_payloadBytes + 17;
    Decision decision = m_selector->Select(ctx, eligible, now);
    if (!decision.valid)
    {
        HandleVoid(pkt);
        return;
    }
    SendData(pkt, decision.fwdId, decision.powerLevel, decision.ackRequest);
    m_selector->OnSent(decision, now);
}

// Members do not choose forwarders: they give their packet to a head of their own cell, in turn (K§2.1).
void
ErgrEmhcRouting::HandOffToHead(HeldPacket pkt)
{
    std::vector<const NeighbourEntry*> heads;
    for (const auto& entry : m_table.Rows())
    {
        const NeighbourEntry& row = entry.second;
        if (row.isHead && row.cellId == m_cell && !pkt.excluded.count(row.id) &&
            static_cast<int32_t>(row.id) != pkt.prevHop && row.hmin != kUnknownHmin)
        {
            heads.push_back(&row);
        }
    }
    if (heads.empty())
    {
        HandleVoid(pkt);
        return;
    }
    const NeighbourEntry* head = heads[m_headRoundRobin++ % heads.size()];
    pkt.hpkt = head->hmin; // the head restarts the hop budget from its own Hmin
    SendData(pkt, head->id, params::phy::kNumPowerLevels - 1, false);
}

void
ErgrEmhcRouting::SendData(HeldPacket pkt, uint16_t forwarderId, uint8_t powerLevel, bool ackRequest)
{
    ErgrDataHeader hdr;
    hdr.senderId = m_id;
    hdr.forwarderId = forwarderId;
    hdr.sourceId = pkt.key.source;
    hdr.sourceSeq = pkt.key.seq;
    hdr.hpkt = pkt.hpkt;
    hdr.hopSeq = ++m_hopSeq[forwarderId];
    hdr.SetFlags(ackRequest, false, powerLevel);
    hdr.genTimeMs = pkt.genTimeMs;

    Ptr<Packet> packet = Create<Packet>(m_payloadBytes);
    packet->AddHeader(hdr);
    m_metrics->OnDataSent(pkt.key, m_id, pkt.sentOnce);
    TransmitFrame(packet, FrameType::Data, forwarderId, powerLevel);

    pkt.sentOnce = true;
    pkt.lastForwarder = forwarderId;
    Remember(pkt);
}

void
ErgrEmhcRouting::Remember(HeldPacket pkt)
{
    Forget(pkt.key);
    E2EKey key = pkt.key;
    pkt.expiry = Simulator::Schedule(Seconds(m_nackWindowS), &ErgrEmhcRouting::Forget, this, key);
    m_sent[key] = pkt;
}

void
ErgrEmhcRouting::Forget(const E2EKey& key)
{
    auto it = m_sent.find(key);
    if (it != m_sent.end())
    {
        it->second.expiry.Cancel();
        m_sent.erase(it);
    }
}

void
ErgrEmhcRouting::Drop(const HeldPacket& pkt, DropReason reason)
{
    m_metrics->OnDrop(m_id, pkt.key, reason);
    Forget(pkt.key);
}

// Spec §5.2 void handling: tell the previous hop (NACK) so it can try another forwarder.
void
ErgrEmhcRouting::HandleVoid(const HeldPacket& pkt)
{
    if (pkt.prevHop == kNone)
    {
        Drop(pkt, DropReason::NoEligibleForwarder);
        return;
    }
    ErgrNackHeader nack;
    nack.nackerId = m_id;
    nack.upstreamId = static_cast<uint16_t>(pkt.prevHop);
    nack.sourceId = pkt.key.source;
    nack.sourceSeq = pkt.key.seq;
    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(nack);
    std::ostringstream details;
    details << "pkt=" << pkt.key.source << ":" << pkt.key.seq << " hpkt=" << int(pkt.hpkt);
    m_metrics->OnEvent(m_id, "VOID_NACK", details.str());
    TransmitFrame(packet, FrameType::Nack, pkt.prevHop, params::phy::kNumPowerLevels - 1);
}

// Spec §9.2: DATA addressed to me.
void
ErgrEmhcRouting::OnData(const ErgrDataHeader& hdr)
{
    E2EKey key{hdr.sourceId, hdr.sourceSeq};
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
    HeldPacket pkt;
    pkt.key = key;
    pkt.genTimeMs = hdr.genTimeMs;
    pkt.hpkt = hdr.hpkt > 0 ? hdr.hpkt - 1 : 0; // one hop used
    pkt.prevHop = hdr.senderId;
    RoutePacket(pkt);
}

void
ErgrEmhcRouting::OnNack(const ErgrNackHeader& hdr)
{
    if (hdr.upstreamId != m_id)
    {
        return;
    }
    E2EKey key{hdr.sourceId, hdr.sourceSeq};
    auto it = m_sent.find(key);
    if (it == m_sent.end())
    {
        return; // window expired
    }
    HeldPacket pkt = it->second;
    Forget(key);
    pkt.excluded.insert(hdr.nackerId);
    pkt.voidRetries++;
    if (pkt.voidRetries > m_voidRetryLimit)
    {
        Drop(pkt, DropReason::MaxTxReached);
        return;
    }
    pkt.hpkt = (pkt.hpkt < 254) ? pkt.hpkt + 1 : pkt.hpkt; // spec §5.2: retry with Hpkt + 1
    RoutePacket(pkt);
}

// ============================================================ frame I/O ====

void
ErgrEmhcRouting::TransmitFrame(Ptr<Packet> packet, FrameType type, int32_t receiver, uint8_t powerLevel)
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
    m_metrics->OnFrameTx(m_id, type, bytes, powerLevel, receiver);
    SendDown(packet, AquaSimAddress::GetBroadcast(), Seconds(0));
}

bool
ErgrEmhcRouting::Recv(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
    if (!m_started || !IsAlive())
    {
        return false; // a dead node receives nothing
    }
    AquaSimHeader ash;
    packet->RemoveHeader(ash);
    if (ash.GetDirection() == AquaSimHeader::DOWN)
    {
        return false; // no application packets: traffic is generated inside this class
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
    case FrameType::Beacon: {
        ErgrBeaconHeader hdr;
        packet->RemoveHeader(hdr);
        m_metrics->OnFrameRx(m_id, FrameType::Beacon, bytes, true, hdr.nodeId);
        OnBeacon(hdr);
        break;
    }
    case FrameType::Election: {
        ErgrElectionHeader hdr;
        packet->RemoveHeader(hdr);
        m_metrics->OnFrameRx(m_id, FrameType::Election, bytes, true, hdr.nodeId);
        OnElectionFrame(hdr);
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
        NS_LOG_WARN("unknown frame type " << int(PeekFrameType(packet)));
        break;
    }
    return true;
}

} // namespace cbergr
} // namespace ns3
