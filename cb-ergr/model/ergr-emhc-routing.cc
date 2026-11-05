/*
 * ergr-emhc-routing.cc: ERGR-EMHC-g per-node routing (spec §5, §9; kickoff §2.1).
 * Sections: self state, beacons, election, traffic and forwarding, frame I/O.
 * All forwarding decisions go through the ForwarderSelector; this file never checks which one it is.
 */

#include "ergr-emhc-routing.h"

#include "cb-ergr-params.h"
#include "tx-power-level-tag.h"

#include "ns3/aqua-sim-phy-cmn.h"

#include "ns3/aqua-sim-address.h"
#include "ns3/aqua-sim-header.h"
#include "ns3/aqua-sim-net-device.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <chrono>
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
            .AddAttribute("EqualHminFallback", "With no strictly better forwarder, allow heads of equal Hmin while Hpkt allows.",
                          BooleanValue(params::grid::kEqualHminFallback),
                          MakeBooleanAccessor(&ErgrEmhcRouting::m_equalHminFallback), MakeBooleanChecker())
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
            .AddAttribute("MaxTx", "Attempts per packet and node before dropping (feedback mode).",
                          UintegerValue(params::timers::kMaxTx),
                          MakeUintegerAccessor(&ErgrEmhcRouting::m_maxTx), MakeUintegerChecker<int>(1))
            .AddAttribute("AckProcS", "ACK timeout term: forwarder processing time (s).",
                          DoubleValue(params::timers::kAckProcS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_ackProcS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("AckGuardS", "ACK timeout term: guard time (s).",
                          DoubleValue(params::timers::kAckGuardS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_ackGuardS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("PhyRxDelayAirtimes", "Extra airtimes the PHY holds a received frame before the MAC sees it (measured: 1).",
                          DoubleValue(params::timers::kPhyRxDelayAirtimes),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_phyRxDelayAirtimes), MakeDoubleChecker<double>(0.0))
            .AddAttribute("RetriesAlwaysAck", "Retries always request an ACK (spec §9.12).",
                          BooleanValue(params::timers::kRetriesAlwaysAck),
                          MakeBooleanAccessor(&ErgrEmhcRouting::m_retriesAlwaysAck), MakeBooleanChecker())
            .AddAttribute("KeepRecordAfterTimeout", "Keep the learning record after an ACK timeout so a late ACK or bitmap can label it (false = old rule).",
                          BooleanValue(params::feedback::kKeepRecordAfterTimeout),
                          MakeBooleanAccessor(&ErgrEmhcRouting::m_keepRecord), MakeBooleanChecker())
            .AddAttribute("CensoredLabels", "Attempts with no evidence by expiry: Drop (not shown to the learner) or Zero (counted as 0, old rule).",
                          StringValue(params::feedback::kCensoredAsZero ? "Zero" : "Drop"),
                          MakeStringAccessor(&ErgrEmhcRouting::m_censoredPolicy), MakeStringChecker())
            .AddAttribute("ExpiryBeacons", "Label expiry (MExpiry) in beacon periods.",
                          DoubleValue(params::timers::kExpiryBeacons),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_expiryBeacons), MakeDoubleChecker<double>(1.0))
            .AddAttribute("ReceiptKeepBeacons", "Senders stay in outgoing receipt lists this many beacon periods.",
                          DoubleValue(params::timers::kReceiptKeepBeacons),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_receiptKeepBeacons), MakeDoubleChecker<double>(1.0))
            .AddAttribute("SuspectTimeouts", "Consecutive ACK timeouts before a forwarder is suspected.",
                          UintegerValue(params::timers::kSuspectTimeouts),
                          MakeUintegerAccessor(&ErgrEmhcRouting::m_suspectTimeouts), MakeUintegerChecker<int>(1))
            .AddAttribute("LeanBeacons", "N-C: members beacon only before elections; head beacons stretch while nothing changes.",
                          BooleanValue(params::timers::kLeanBeacons),
                          MakeBooleanAccessor(&ErgrEmhcRouting::m_leanBeacons), MakeBooleanChecker())
            .AddAttribute("TrickleMaxFactor", "Largest beacon interval of a stable head, in base periods.",
                          DoubleValue(params::timers::kTrickleMaxFactor),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_trickleMaxFactor), MakeDoubleChecker<double>(1.0))
            .AddAttribute("PreElectionLeadS", "Members announce themselves this long before an election round (lean mode).",
                          DoubleValue(params::timers::kPreElectionLeadS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_preElectionLeadS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("MacDelayAllowanceS", "Added to the ACK timeout: backoff the MAC may impose on the DATA and on the ACK (s).",
                          DoubleValue(params::timers::kMacDelayAllowanceS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_macDelayAllowanceS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("Solicit", "E34: a member without a head row holds its packet and asks the heads of its cell to beacon.",
                          BooleanValue(params::solicit::kEnabled), MakeBooleanAccessor(&ErgrEmhcRouting::m_solicit),
                          MakeBooleanChecker())
            .AddAttribute("SolicitWaitS", "Wait for an answer before asking again (s).", DoubleValue(params::solicit::kWaitS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_solicitWaitS), MakeDoubleChecker<double>(0.1))
            .AddAttribute("SolicitRetries", "Asks before the held packets are given up.", UintegerValue(params::solicit::kRetries),
                          MakeUintegerAccessor(&ErgrEmhcRouting::m_solicitRetries), MakeUintegerChecker<int>(1))
            .AddAttribute("SolicitReplyJitterS", "A head waits U(0, this) before answering a solicit (s).",
                          DoubleValue(params::solicit::kReplyJitterS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_solicitReplyJitterS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("SolicitReplyMinGapS", "A head answers at most once in this time (s).",
                          DoubleValue(params::solicit::kReplyMinGapS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_solicitReplyMinGapS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("QuorumDetection", "D§3 P4: declare a silent head dead after its beacon is overdue and a quorum agrees.",
                          BooleanValue(params::deadhead::kQuorumDetection),
                          MakeBooleanAccessor(&ErgrEmhcRouting::m_quorumDetection), MakeBooleanChecker())
            .AddAttribute("DeadCheckPeriodS", "How often overdue heads are looked for (s).",
                          DoubleValue(params::deadhead::kCheckPeriodS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_deadCheckPeriodS), MakeDoubleChecker<double>(0.1))
            .AddAttribute("DeadGuardS", "Guard past the advertised next beacon before a head counts as overdue (s).",
                          DoubleValue(params::deadhead::kGuardS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_deadGuardS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("DeadQuorum", "Observers (including me) needed to declare a head dead.",
                          UintegerValue(params::deadhead::kQuorum),
                          MakeUintegerAccessor(&ErgrEmhcRouting::m_deadQuorum), MakeUintegerChecker<int>(1))
            .AddAttribute("DeadReportTtlS", "How long a received dead-head report counts (s).",
                          DoubleValue(params::deadhead::kReportTtlS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_deadReportTtlS), MakeDoubleChecker<double>(1.0))
            .AddAttribute("DeadReportRepeatS", "Minimum time between two reports of the same silent head by one node (s).",
                          DoubleValue(params::deadhead::kReportRepeatS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_deadReportRepeatS), MakeDoubleChecker<double>(1.0))
            .AddAttribute("DeadReportJitterS", "Random delay before a report is sent (s).",
                          DoubleValue(params::deadhead::kReportJitterS),
                          MakeDoubleAccessor(&ErgrEmhcRouting::m_deadReportJitterS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("AutoElection", "Run the head election (false only in tests that call ForceRole).",
                          BooleanValue(params::timers::kAutoElection),
                          MakeBooleanAccessor(&ErgrEmhcRouting::m_autoElection), MakeBooleanChecker())
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
      m_equalHminFallback(params::grid::kEqualHminFallback),
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
      m_maxTx(params::timers::kMaxTx),
      m_suspectTimeouts(params::timers::kSuspectTimeouts),
      m_leanBeacons(params::timers::kLeanBeacons),
      m_trickleMaxFactor(params::timers::kTrickleMaxFactor),
      m_preElectionLeadS(params::timers::kPreElectionLeadS),
      m_ackProcS(params::timers::kAckProcS),
      m_ackGuardS(params::timers::kAckGuardS),
      m_phyRxDelayAirtimes(params::timers::kPhyRxDelayAirtimes),
      m_retriesAlwaysAck(params::timers::kRetriesAlwaysAck),
      m_macDelayAllowanceS(params::timers::kMacDelayAllowanceS),
      m_solicit(params::solicit::kEnabled),
      m_solicitWaitS(params::solicit::kWaitS),
      m_solicitReplyJitterS(params::solicit::kReplyJitterS),
      m_solicitReplyMinGapS(params::solicit::kReplyMinGapS),
      m_solicitRetries(params::solicit::kRetries),
      m_solicitMaxWaiting(params::solicit::kMaxWaiting),
      m_quorumDetection(params::deadhead::kQuorumDetection),
      m_deadCheckPeriodS(params::deadhead::kCheckPeriodS),
      m_deadGuardS(params::deadhead::kGuardS),
      m_deadReportTtlS(params::deadhead::kReportTtlS),
      m_deadReportRepeatS(params::deadhead::kReportRepeatS),
      m_deadReportJitterS(params::deadhead::kReportJitterS),
      m_deadQuorum(params::deadhead::kQuorum),
      m_autoElection(params::timers::kAutoElection),
      m_expiryBeacons(params::timers::kExpiryBeacons),
      m_receiptKeepBeacons(params::timers::kReceiptKeepBeacons),
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
    int64_t used = 2;
    if (m_selector)
    {
        used += m_selector->AssignStreams(stream + used);
    }
    return used;
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

void
ErgrEmhcRouting::Kill()
{
    std::ostringstream details;
    details << "cell=" << m_cell << " head=" << m_isHead;
    m_metrics->OnEvent(m_id, "NODE_KILLED", details.str());
    GetNetDevice()->GetPhy()->EM()->SetEnergy(0.0);
}

void
ErgrEmhcRouting::InjectRawFrame(uint16_t forwarderId, uint8_t powerLevel)
{
    ErgrDataHeader hdr;
    hdr.senderId = m_id;
    hdr.forwarderId = forwarderId;
    hdr.hopSeq = NextHopSeq(forwarderId);
    hdr.SetFlags(false, false, powerLevel);
    Ptr<Packet> packet = Create<Packet>(m_payloadBytes);
    packet->AddHeader(hdr);
    TransmitFrame(packet, FrameType::Data, forwarderId, powerLevel);
}

void
ErgrEmhcRouting::ForceRole(uint8_t rank)
{
    if (rank == 0)
    {
        m_isHead = false;
        m_rank = 0;
        return;
    }
    m_isHead = true;
    m_rank = rank;
    m_metrics->OnHeadChange(m_id, true, m_cell);
    m_selector->OnRoleChange(true, Simulator::Now());
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
    m_beaconIntervalS = m_beaconPeriodS;
    if (m_selector->UsesFeedback() && !m_isSink)
    {
        FeedbackManager::Config fc;
        fc.myId = m_id;
        fc.beaconPeriodS = m_beaconPeriodS;
        fc.maxTx = m_maxTx;
        fc.expiryS = m_expiryBeacons * m_beaconPeriodS;
        fc.receiptKeepS = m_receiptKeepBeacons * m_beaconPeriodS;
        fc.keepRecordAfterTimeout = m_keepRecord;
        fc.censoredAsZero = (m_censoredPolicy == "Zero");
        FeedbackManager::Hooks hooks;
        hooks.retry = [this](const HeldPacket& held) {
            if (!IsAlive())
            {
                m_metrics->OnDrop(m_id, held.key, DropReason::NodeDiedHolding);
                return;
            }
            RoutePacket(held);
        };
        hooks.drop = [this](const HeldPacket& held, DropReason reason) { Drop(held, reason); };
        hooks.forwarderTimedOut = [this](uint16_t id) { NoteForwarderTimeout(id); };
        hooks.forwarderAcked = [this](uint16_t id) { NoteForwarderAlive(id); };
        m_feedback = std::make_unique<FeedbackManager>(fc, m_selector, m_metrics, hooks);
    }
    {
        SelectorServices services;
        services.nodeId = m_id;
        services.beaconPeriodS = m_beaconPeriodS;
        services.link = m_linkModel;
        Ptr<AquaSimPhyCmn> phy = DynamicCast<AquaSimPhyCmn>(GetNetDevice()->GetPhy());
        services.initialEnergyJ = GetNetDevice()->GetPhy()->EM()->GetInitialEnergy();
        services.residualEnergyJ = [this]() { return GetNetDevice()->GetPhy()->EM()->GetEnergy(); };
        services.airtimeS = [phy](uint32_t bytes) { return phy->CalcTxTime(bytes).GetSeconds(); };
        services.cellId = [this]() { return m_cell; };
        services.isHead = [this]() { return m_isHead; };
        services.isNeighbourCell = [this](uint16_t cell) { return Grid().AreNeighbours(m_cell, cell, m_faceOnly); };
        m_selector->SetServices(services);
        m_selector->Start();
        if (m_shadow)
        {
            m_shadow->SetServices(services);
            m_shadow->Start();
        }
    }
    UpdateSelfEstimate();
    UpdateOwnCell();

    // Beacons: random first phase so nodes do not all transmit together (spec §5.2).
    Simulator::Schedule(Seconds(m_uniform->GetValue(0.0, m_beaconPeriodS)), &ErgrEmhcRouting::BeaconTick, this);
    if (m_quorumDetection && !m_isSink)
    {
        Simulator::Schedule(Seconds(m_uniform->GetValue(0.0, m_deadCheckPeriodS)), &ErgrEmhcRouting::DeadCheckTick, this);
    }

    if (!m_isSink)
    {
        if (m_autoElection)
        {
            Simulator::Schedule(Seconds(m_firstElectionS + m_cell * m_electionOffsetPerCellS),
                                &ErgrEmhcRouting::OnElectionEpoch, this);
        }
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
    m_memberBeaconDue = true; // lean mode: announce the new cell
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
            if (m_isHead)
            {
                m_isHead = false; // a dead head ends its tenure silently: no handover (unplanned loss)
                m_rank = 0;
                m_metrics->OnHeadChange(m_id, false, m_cell);
            }
        }
        return; // a dead node stops all activity
    }
    UpdateSelfEstimate();
    UpdateOwnCell();
    ExpireNeighbours();
    RecomputeHmin();
    if (m_solicit && !m_isSink && !m_isHead && m_autoElection && CountHeadsInCell() == 0 &&
        Simulator::Now().GetSeconds() > m_firstElectionS + m_electionOffsetPerCellS * m_cell)
    {
        // E34: my cell has no head that I know of (heads left, or never elected): run an election round (debounced).
        StartElectionRound("no head in my cell", false);
    }
    if (!m_leanBeacons)
    {
        SendBeacon();
    }
    else if (m_isHead || m_isSink)
    {
        // N-C: heads and sinks beacon at the current interval; it doubles (up to the limit) while nothing changes.
        if ((Simulator::Now() - m_lastBeaconTx).GetSeconds() >= m_beaconIntervalS - 1e-6)
        {
            UpdateTrickleInterval();
            SendBeacon();
        }
    }
    else if (m_memberBeaconDue)
    {
        m_memberBeaconDue = false; // members announce themselves at start-up, on a cell change, and before elections
        SendBeacon();
    }
    Simulator::Schedule(Seconds(m_beaconPeriodS), &ErgrEmhcRouting::BeaconTick, this);
}

// The part of the state whose change resets the lean-beacon interval: own role, cell, Hmin and the set of
// heads/sinks with their cells and Hmin. Positions and model changes are deliberately left out (D§4.2 N-C).
uint64_t
ErgrEmhcRouting::StateFingerprint()
{
    uint64_t h = 1469598103934665603ULL; // FNV-1a
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    mix(m_isHead ? 1 + m_rank : 0);
    mix(m_cell);
    mix(m_hmin);
    for (const auto& entry : m_table.Rows())
    {
        const NeighbourEntry& row = entry.second;
        if (row.isHead || row.isSink)
        {
            mix(row.id);
            mix(row.cellId);
            mix(row.hmin);
        }
    }
    return h;
}

void
ErgrEmhcRouting::UpdateTrickleInterval()
{
    uint64_t fingerprint = StateFingerprint();
    if (fingerprint != m_lastFingerprint)
    {
        m_beaconIntervalS = m_beaconPeriodS; // something changed: back to the short interval
    }
    else
    {
        m_beaconIntervalS = std::min(2.0 * m_beaconIntervalS, m_trickleMaxFactor * m_beaconPeriodS);
    }
    m_lastFingerprint = fingerprint;
}

// What this node tells its neighbours about when to expect its next beacon (used for expiry, D§3 P4).
double
ErgrEmhcRouting::AdvertisedIntervalS() const
{
    if (!m_leanBeacons)
    {
        return m_beaconPeriodS;
    }
    return (m_isHead || m_isSink) ? m_beaconIntervalS : m_electionPeriodS;
}

// N-C: a member beacons just before each election round so that the others can rank it.
void
ErgrEmhcRouting::SendPreElectionBeacon()
{
    if (!IsAlive() || m_isHead || m_isSink)
    {
        return;
    }
    UpdateSelfEstimate();
    SendBeacon();
}

void
ErgrEmhcRouting::SendBeacon()
{
    m_lastBeaconTx = Simulator::Now();
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
    hdr.nextBeaconDs = static_cast<uint16_t>(std::min(65535.0, std::round(AdvertisedIntervalS() * 10.0)));
    if (m_feedback)
    {
        hdr.receipts = m_feedback->BuildReceipts(Simulator::Now());
    }

    m_selector->OnBeaconTx(hdr, Simulator::Now()); // may attach a model blob (spec §8.10)
    m_metrics->OnBeaconParts(hdr.BaseBytes(), hdr.ReceiptBytes(), hdr.BlobBytes(), hdr.QBytes());
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
    std::set<uint16_t> headsAnywhere, relevantBefore;
    for (const auto& entry : m_table.Rows())
    {
        if (entry.second.isHead)
        {
            headsAnywhere.insert(entry.first);
            if (IsRelevantHead(entry.second))
            {
                relevantBefore.insert(entry.first);
            }
        }
    }
    Time timeout = Seconds(m_neighbourTimeoutBeacons * m_beaconPeriodS);
    std::vector<uint16_t> removed = m_table.Expire(Simulator::Now(), timeout, m_neighbourTimeoutBeacons);
    bool lostOwnCellHead = false;
    for (uint16_t id : removed)
    {
        if (headsBefore.count(id) > 0 || headsAnywhere.count(id) > 0)
        {
            const bool relevant = headsBefore.count(id) > 0 || relevantBefore.count(id) > 0;
            // Only the loss of a head counts as a known change (K§2.3). A head whose beacons stopped for
            // 3 periods is treated as dead (single observer: the quorum of D§3 P4 is not implemented).
            m_metrics->OnEvent(m_id, "HEAD_DEAD", "id=" + std::to_string(id) + " beacons overdue (table expiry)");
            if (relevant)
            {
                m_metrics->OnDeadDeclared(m_id, id, false); // same population as the quorum path: own and neighbouring cells
            }
            m_selector->OnKnownChange(ChangeEvent::NodeLost, Simulator::Now());
        }
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
    entry.hasQ = hdr.hasQ;
    entry.advertisedQ = hdr.qValue * params::selector::kQlValueScale;
    double snrDb = 0.0;
    if (m_snrMeter && m_snrMeter(hdr.nodeId, m_id, snrDb))
    {
        entry.lastSnrDb = snrDb;   // measured on this beacon (MarginFromRssi, K§2.3)
        entry.hasSnr = true;
    }

    bool wasHeadInMyCell = false;
    if (const NeighbourEntry* old = m_table.Find(hdr.nodeId))
    {
        wasHeadInMyCell = old->isHead && old->cellId == m_cell;
    }
    NoteForwarderAlive(hdr.nodeId); // any beacon is a sign of life (D§3 P4)
    m_deadReports.erase(hdr.nodeId);
    m_lastDeadReportSent.erase(hdr.nodeId);
    TableUpdateResult result = m_table.Update(entry);
    m_selector->OnBeaconRx(hdr, Simulator::Now());
    if (m_feedback)
    {
        m_feedback->OnBeaconReceipts(hdr); // label pending attempts to this node from its receipt list
    }

    if (result.headStatusChanged && entry.cellId != m_cell && !entry.isSink)
    {
        m_selector->OnKnownChange(ChangeEvent::HeadChangedInNeighbourCell, Simulator::Now());
    }
    if (!m_waitingForHead.empty() && entry.isHead && entry.cellId == m_cell && entry.hmin != kUnknownHmin)
    {
        Simulator::ScheduleNow(&ErgrEmhcRouting::FlushWaitingForHead, this);
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
    if (m_leanBeacons)
    {
        Simulator::Schedule(Seconds(std::max(0.0, m_electionPeriodS - m_preElectionLeadS)),
                            &ErgrEmhcRouting::SendPreElectionBeacon, this);
    }
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
    if (m_metrics->GetConfig().logEvents)
    {
        m_metrics->OnEvent(m_id, "ELECTION_ROUND",
                           std::string(reason) + " cell=" + std::to_string(m_cell) + " rank=" + std::to_string(rank));
    }
    if (rank > m_headsPerCell)
    {
        // D§3 P3 step-down rule: a head retires when it hears >= k BETTER HEADS, not merely when k better-ranked members
        // exist in its table: those members may not claim (they only claim in a round they run themselves, and rows of
        // nodes that have left are stale). Retiring on rank alone left cells with no head at all (6 Nov 2026, see CHANGELOG).
        if (m_isHead)
        {
            if (CountBetterHeadsInCell() >= m_headsPerCell)
            {
                BecomeMember("outranked at election");
            }
            return;
        }
        // A member ranked below k does not return: ranks come from beacon rows that can be stale (nodes that have left the
        // cell still rank above everybody), and in the 6 Nov 2026 run a cell of 9 nodes stayed headless for 1800 s because
        // every member saw 5-7 "better" ghosts. It claims in its own, later slot, and only if fewer than k heads of the
        // cell have been heard by then (OnHelloSlot). With consistent views nothing changes: ranks <= k claim first.
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
    if (rank > m_headsPerCell && CountHeadsInCell() >= m_headsPerCell)
    {
        return; // the better-ranked members did claim: no back-up needed
    }
    BecomeHead(rank > m_headsPerCell ? static_cast<uint8_t>(m_headsPerCell) : rank);
}

void
ErgrEmhcRouting::BecomeHead(uint8_t rank)
{
    bool wasHead = m_isHead;
    m_isHead = true;
    m_rank = rank;
    if (!wasHead)
    {
        m_selector->OnRoleChange(true, Simulator::Now()); // adopt a handed-over or shared model
        if (!m_waitingForHead.empty())
        {
            Simulator::ScheduleNow(&ErgrEmhcRouting::FlushWaitingForHead, this); // packets held while this node asked for a head
        }
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
    SendHandoverToSuccessor(); // planned handover: this head is alive and chose to stop (or was outranked)
    m_selector->OnRoleChange(false, Simulator::Now());
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
    if (hdr.kind == ErgrElectionHeader::Solicit)
    {
        // A member of my cell has no head row: answer with a beacon (rate limited; one beacon serves every asker).
        const Time now = Simulator::Now();
        if (m_solicit && m_isHead && hdr.cellId == m_cell &&
            (now - m_lastSolicitReply).GetSeconds() >= m_solicitReplyMinGapS)
        {
            m_lastSolicitReply = now;
            Simulator::Schedule(Seconds(m_uniform->GetValue(0.0, m_solicitReplyJitterS)), [this]() {
                if (IsAlive() && m_isHead)
                {
                    m_metrics->OnEvent(m_id, "SOLICIT_ANSWER", "beacon");
                    SendBeacon();
                }
            });
        }
        return;
    }
    if (hdr.kind == ErgrElectionHeader::DeadReport)
    {
        OnDeadReport(hdr); // says nothing about the reporter's own role
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
    Vector myPos = EstimatedPosition();
    // Pass 0: the spec rule. Pass 1 (only if pass 0 found nothing): heads with the SAME Hmin as mine, like the paper's NG2T,
    // while the hop budget still allows (the hop costs one unit of Hpkt, so Hmin_j < Hpkt).
    for (int pass = 0; pass < (m_equalHminFallback ? 2 : 1); ++pass)
    {
        std::vector<Candidate> eligible;
        std::vector<Candidate> suspects;
        for (const auto& entry : m_table.Rows())
        {
            const NeighbourEntry& row = entry.second;
            if (row.id == m_id || static_cast<int32_t>(row.id) == pkt.prevHop || pkt.excluded.count(row.id))
            {
                continue;
            }
            bool inG1 = row.isSink || (row.isHead && Grid().AreNeighbours(m_cell, row.cellId, m_faceOnly));
            if (!inG1 || row.hmin > pkt.hpkt)
            {
                continue; // spec §5.1: Hmin_j <= Hpkt
            }
            Vector predicted = row.PredictedPos(now);
            if (Distance(predicted, myPos) > m_txRangeM)
            {
                continue; // K§2.1 predicted distance <= R
            }
            if (pass == 0)
            {
                if (m_strictProgress && !row.isSink && !(row.hmin < m_hmin || predicted.z < myPos.z))
                {
                    continue; // K§2.1 loop guard: closer in hops or shallower
                }
            }
            else if (row.isSink || row.hmin != m_hmin || row.hmin >= pkt.hpkt)
            {
                continue; // fallback: equal Hmin only, and only while Hpkt allows another hop
            }
            // D§3 P4: a suspect forwarder is skipped for one beacon interval ...
            (IsSuspect(row.id, now) ? suspects : eligible).push_back(Candidate{row.id, &row});
        }
        // ... but it is still better than no forwarder at all: use suspects only as a last resort.
        if (!eligible.empty())
        {
            return eligible; // each list is sorted by id (map order)
        }
        if (!suspects.empty())
        {
            return suspects;
        }
    }
    return {};
}

void
ErgrEmhcRouting::RoutePacket(HeldPacket pkt)
{
    if (!IsAlive())
    {
        m_metrics->OnDrop(m_id, pkt.key, DropReason::NodeDiedHolding);
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
    ctx.attempt = pkt.attempt + 1;
    ctx.myId = m_id;
    ctx.myHmin = m_hmin;
    ctx.myPos = EstimatedPosition();
    ctx.myVel = m_estVel;
    ctx.myBattery = ResidualFraction();
    ctx.myEnergyJ = GetNetDevice()->GetPhy()->EM()->GetEnergy();
    ctx.dataBytes = m_payloadBytes + 17;
    Decision decision;
    if (m_metrics->MeasureCpu())
    {
        const auto t0 = std::chrono::steady_clock::now();
        decision = m_selector->Select(ctx, eligible, now);
        m_metrics->OnCpuSample(MetricsCollector::kCpuSelect,
                               std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count());
    }
    else
    {
        decision = m_selector->Select(ctx, eligible, now);
    }
    if (!decision.valid)
    {
        HandleVoid(pkt);
        return;
    }
    decision.numCandidates = static_cast<uint8_t>(eligible.size());
    if (const NeighbourEntry* chosen = m_table.Find(decision.fwdId))
    {
        decision.fwdCell = chosen->cellId;
    }
    if (m_decisionObserver)
    {
        m_decisionObserver(ctx, eligible, decision, now);
    }
    if (m_metrics->LogCounterfactual())
    {
        std::vector<uint16_t> ids;
        for (const Candidate& c : eligible)
        {
            ids.push_back(c.id);
        }
        int shadowChoice = -1;
        if (m_shadow)
        {
            Decision sd = m_shadow->Select(ctx, eligible, now); // simulator-side what-if: not used, not learned from
            shadowChoice = sd.valid ? sd.fwdId : -1;
        }
        m_metrics->OnCandidates(m_id, ids, shadowChoice);
    }
    SendData(pkt, decision.fwdId, decision.powerLevel, decision.ackRequest, &decision);
    if (m_metrics->MeasureCpu())
    {
        const auto t0 = std::chrono::steady_clock::now();
        m_selector->OnSent(decision, now);
        m_metrics->OnCpuSample(MetricsCollector::kCpuSent,
                               std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count());
    }
    else
    {
        m_selector->OnSent(decision, now);
    }
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
        if (m_metrics->GetConfig().logEvents)
        {
            // Diagnostic for "member without a usable head": what does the table say about my own cell?
            int headRowsHere = 0, headRowsHereUnknownHmin = 0, rowsHere = 0, headRowsElsewhere = 0;
            for (const auto& entry : m_table.Rows())
            {
                const NeighbourEntry& row = entry.second;
                if (row.cellId == m_cell)
                {
                    rowsHere++;
                    headRowsHere += row.isHead ? 1 : 0;
                    headRowsHereUnknownHmin += (row.isHead && row.hmin == kUnknownHmin) ? 1 : 0;
                }
                else
                {
                    headRowsElsewhere += row.isHead ? 1 : 0;
                }
            }
            std::ostringstream d;
            d << "cell=" << m_cell << " rowsHere=" << rowsHere << " headRowsHere=" << headRowsHere
              << " headRowsUnknownHmin=" << headRowsHereUnknownHmin << " headRowsElsewhere=" << headRowsElsewhere
              << " rows=" << m_table.Size();
            m_metrics->OnEvent(m_id, "NO_HEAD_FOR_HANDOFF", d.str());
        }
        if (m_solicit && !KnowsHeadOfMyCell())
        {
            // (If a head row exists but is excluded for this packet, e.g. the previous hop, it is a normal void.)
            QueueForHead(pkt); // E34: hold the packet and ask the heads of my cell to beacon
            return;
        }
        HandleVoid(pkt);
        return;
    }
    const NeighbourEntry* head = heads[m_headRoundRobin++ % heads.size()];
    pkt.hpkt = head->hmin; // the head restarts the hop budget from its own Hmin
    if (m_metrics->LogCounterfactual())
    {
        std::vector<uint16_t> ids;
        for (const NeighbourEntry* h : heads)
        {
            ids.push_back(h->id);
        }
        m_metrics->OnCandidates(m_id, ids, -2); // -2: a member hand-off, not a forwarder choice
    }
    SendData(pkt, head->id, params::phy::kNumPowerLevels - 1, m_feedback != nullptr, nullptr);
}

// Random start (spec §7): the first hopSeq towards a forwarder is random, then it counts up.
uint16_t
ErgrEmhcRouting::NextHopSeq(uint16_t fwdId)
{
    auto it = m_hopSeq.find(fwdId);
    if (it == m_hopSeq.end())
    {
        it = m_hopSeq.emplace(fwdId, static_cast<uint16_t>(m_uniform->GetInteger(0, 65535))).first;
    }
    return ++it->second;
}

// Spec §9.1 step 7: airtime(DATA) + 2 * d / 1500 + airtime(ACK) + TProc + TGuard, extended by the
// extra airtime(s) the Aqua-Sim PHY holds each frame before delivering it (CONFIRM E17).
double
ErgrEmhcRouting::AckTimeoutS(uint16_t fwdId, double dataBytes)
{
    Ptr<AquaSimPhyCmn> phy = DynamicCast<AquaSimPhyCmn>(GetNetDevice()->GetPhy());
    double dataAirS = phy->CalcTxTime(static_cast<uint32_t>(dataBytes)).GetSeconds();
    double ackAirS = phy->CalcTxTime(7).GetSeconds(); // ACK header is 7 B
    double distM = m_txRangeM;                         // worst case if the neighbour is unknown
    if (const NeighbourEntry* nb = m_table.Find(fwdId))
    {
        distM = Distance(nb->PredictedPos(Simulator::Now()), EstimatedPosition());
    }
    double airS = (1.0 + m_phyRxDelayAirtimes) * (dataAirS + ackAirS);
    return airS + 2.0 * distM / params::timers::kAckSoundSpeedMps + m_ackProcS + m_ackGuardS + m_macDelayAllowanceS;
}

void
ErgrEmhcRouting::SendData(HeldPacket pkt,
                          uint16_t forwarderId,
                          uint8_t powerLevel,
                          bool ackRequest,
                          const Decision* decision)
{
    pkt.attempt++;
    const bool isRetry = pkt.attempt > 1;
    const bool tracked = (m_feedback != nullptr);
    // Spec §9.12: retries always request an ACK (only meaningful when feedback is on).
    const bool ar = tracked && (ackRequest || (isRetry && m_retriesAlwaysAck));

    ErgrDataHeader hdr;
    hdr.senderId = m_id;
    hdr.forwarderId = forwarderId;
    hdr.sourceId = pkt.key.source;
    hdr.sourceSeq = pkt.key.seq;
    hdr.hpkt = pkt.hpkt;
    hdr.hopSeq = NextHopSeq(forwarderId);
    hdr.SetFlags(ar, false, powerLevel);
    hdr.genTimeMs = pkt.genTimeMs;

    Ptr<Packet> packet = Create<Packet>(m_payloadBytes);
    packet->AddHeader(hdr);
    const double dataBytes = packet->GetSize();

    double predictedDistM = 0.0;
    if (const NeighbourEntry* nb = m_table.Find(forwarderId))
    {
        predictedDistM = Distance(nb->PredictedPos(Simulator::Now()), EstimatedPosition());
    }
    Decision logged;
    if (decision)
    {
        logged = *decision;
    }
    else
    {
        logged.fwdId = forwarderId;
        logged.powerLevel = powerLevel;
        logged.valid = true;
    }
    logged.ackRequest = ar;

    m_metrics->OnDataSent(pkt.key, m_id, isRetry);
    m_metrics->OnAttemptSent(m_id, forwarderId, hdr.hopSeq, pkt.attempt, ar, decision != nullptr, tracked,
                             predictedDistM, logged);
    TransmitFrame(packet, FrameType::Data, forwarderId, powerLevel);

    pkt.lastForwarder = forwarderId;
    pkt.lastHopSeq = hdr.hopSeq;
    if (tracked)
    {
        PendingAttempt pa;
        pa.decision = logged;
        pa.hop = HopKey{forwarderId, hdr.hopSeq};
        pa.pkt = pkt.key;
        pa.ackRequested = ar;
        pa.attempt = pkt.attempt;
        pa.learn = (decision != nullptr);
        pa.predictedDistM = predictedDistM;
        m_feedback->Track(pa, pkt, ar ? AckTimeoutS(forwarderId, dataBytes) : 0.0);
    }
    Remember(pkt);
}

void
ErgrEmhcRouting::SendAck(uint16_t toSender, uint16_t hopSeq)
{
    ErgrAckHeader ack;
    ack.ackerId = m_id;
    ack.senderId = toSender;
    ack.hopSeq = hopSeq;
    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(ack);
    m_metrics->OnAckSent(m_id);
    TransmitFrame(packet, FrameType::Ack, toSender, params::phy::kNumPowerLevels - 1); // ACKs use full power
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
    m_metrics->OnHopReceived(hdr.senderId, m_id, hdr.hopSeq);
    m_metrics->OnDataDelivered(E2EKey{hdr.sourceId, hdr.sourceSeq}, hdr.senderId, m_id); // ground truth, logging only
    if (m_feedback)
    {
        m_feedback->MarkReceived(hdr.senderId, hdr.hopSeq, Simulator::Now()); // spec §9.2 step 1
    }
    if (hdr.GetAckRequest())
    {
        SendAck(hdr.senderId, hdr.hopSeq); // spec §9.2 step 2: ACK even for duplicates (the first ACK may have been lost)
    }
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

// D§3 P4 "suspect": two ACK timeouts in a row without any sign of life exclude the forwarder for one beacon interval.
void
ErgrEmhcRouting::NoteForwarderTimeout(uint16_t fwdId)
{
    int& count = m_consecutiveTimeouts[fwdId];
    count++;
    if (count >= m_suspectTimeouts)
    {
        m_suspectUntil[fwdId] = Simulator::Now() + Seconds(m_beaconPeriodS);
        m_metrics->OnEvent(m_id, "HEAD_SUSPECT", "fwd=" + std::to_string(fwdId));
        count = 0;
    }
}

// An ACK or a beacon from the forwarder clears suspicion.
void
ErgrEmhcRouting::NoteForwarderAlive(uint16_t fwdId)
{
    m_consecutiveTimeouts.erase(fwdId);
    m_suspectUntil.erase(fwdId);
}

bool
ErgrEmhcRouting::IsSuspect(uint16_t fwdId, Time now) const
{
    auto it = m_suspectUntil.find(fwdId);
    return it != m_suspectUntil.end() && it->second > now;
}

// ------------------------------------------------------------------------------------------------
// Head solicitation (E34). With lean beacons a member can wait up to 100 s for the next head beacon, and a missed beacon
// leaves it without a head row; its packets used to be dropped at once. Now it holds them (at most kMaxWaiting), broadcasts
// a SOLICIT for its cell every SolicitWaitS, and gives up only after SolicitRetries asks. Any beacon from a head of the own
// cell (the answer, or the head's normal beacon) releases the held packets.
// ------------------------------------------------------------------------------------------------

bool
ErgrEmhcRouting::KnowsHeadOfMyCell() const
{
    for (const auto& entry : m_table.Rows())
    {
        const NeighbourEntry& row = entry.second;
        if (row.isHead && row.cellId == m_cell && row.hmin != kUnknownHmin)
        {
            return true;
        }
    }
    return false;
}

void
ErgrEmhcRouting::QueueForHead(const HeldPacket& pkt)
{
    if (static_cast<int>(m_waitingForHead.size()) >= m_solicitMaxWaiting)
    {
        HeldPacket oldest = m_waitingForHead.front();
        m_waitingForHead.pop_front();
        m_metrics->OnNoHeadGiveUp(m_id, m_cell);
        Drop(oldest, DropReason::NoEligibleForwarder);
    }
    m_waitingForHead.push_back(pkt);
    if (!m_solicitTimer.IsRunning())
    {
        m_solicitAsks = 0;
        SolicitTick();
    }
}

void
ErgrEmhcRouting::SendSolicit()
{
    ErgrElectionHeader hdr;
    hdr.kind = ErgrElectionHeader::Solicit;
    hdr.nodeId = m_id;
    hdr.cellId = m_cell;
    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(hdr);
    m_metrics->OnSolicit(false);
    m_metrics->OnEvent(m_id, "SOLICIT", "cell=" + std::to_string(m_cell) + " ask=" + std::to_string(m_solicitAsks));
    TransmitFrame(packet, FrameType::Election, kNone, params::phy::kNumPowerLevels - 1);
}

void
ErgrEmhcRouting::SolicitTick()
{
    if (m_waitingForHead.empty() || !IsAlive())
    {
        return;
    }
    if (KnowsHeadOfMyCell() || m_isHead)
    {
        FlushWaitingForHead(); // a head (possibly this node itself, just elected) is known: route the held packets
        return;
    }
    if (m_solicitAsks >= m_solicitRetries)
    {
        // Give up: nobody answered. (Against ground truth this is only counted as an error if the cell does have a head.)
        std::deque<HeldPacket> given;
        given.swap(m_waitingForHead);
        for (HeldPacket& pkt : given)
        {
            m_metrics->OnNoHeadGiveUp(m_id, m_cell);
            Drop(pkt, DropReason::NoEligibleForwarder);
        }
        m_solicitAsks = 0;
        StartElectionRound("nobody answered my solicit", false); // the cell may really have no head: elect one
        return;
    }
    m_solicitAsks++;
    SendSolicit();
    m_solicitTimer = Simulator::Schedule(Seconds(m_solicitWaitS), &ErgrEmhcRouting::SolicitTick, this);
}

void
ErgrEmhcRouting::FlushWaitingForHead()
{
    if (m_waitingForHead.empty() || !IsAlive() || !(KnowsHeadOfMyCell() || m_isHead))
    {
        return;
    }
    m_solicitTimer.Cancel();
    if (m_solicitAsks > 0)
    {
        m_metrics->OnSolicit(true);
        m_metrics->OnEvent(m_id, "HEAD_FOUND_AFTER_SOLICIT", "held=" + std::to_string(m_waitingForHead.size()));
    }
    m_solicitAsks = 0;
    std::deque<HeldPacket> held;
    held.swap(m_waitingForHead);
    for (HeldPacket& pkt : held)
    {
        RoutePacket(pkt);
    }
}

// ------------------------------------------------------------------------------------------------
// Quorum dead-head detection (D§3 P4). A head row is OVERDUE when now > beaconTime + advertised next interval + guard
// (the guard is dropped for a forwarder that is already suspect from ACK timeouts: that is the "upstream head with timeout
// evidence"). An overdue row makes this node an observer. It broadcasts a DEAD-REPORT (once per DeadReportRepeatS) and
// declares the head dead when it counts DeadQuorum observers: itself plus fresh reports from other nodes. A node whose own
// row is NOT overdue ignores reports (it still hears the head), so a single node with a bad link cannot kill a live head.
// Declaring dead = remove the row, NodeLost to the selector, and a new election if it was a head of my cell.
// ------------------------------------------------------------------------------------------------

// The observers of D§3 P4 are the co-head and the members of the silent head's cell, plus upstream heads that have
// timeout evidence against it. So a node watches the heads of its OWN cell, and a head of a neighbouring cell only while it
// is suspect (two ACK timeouts). Heads further away are never forwarded to and cannot trigger an election.
bool
ErgrEmhcRouting::IsRelevantHead(const NeighbourEntry& row) const
{
    if (!row.isHead || row.isSink)
    {
        return false;
    }
    if (row.cellId == m_cell)
    {
        return true;
    }
    return IsSuspect(row.id, Simulator::Now()) &&
           const_cast<ErgrEmhcRouting*>(this)->Grid().AreNeighbours(m_cell, row.cellId, m_faceOnly);
}

bool
ErgrEmhcRouting::IsOverdue(const NeighbourEntry& row, Time now) const
{
    const Time due = row.beaconTime + row.nextBeaconIn;
    const Time guard = IsSuspect(row.id, now) ? Seconds(0) : Seconds(m_deadGuardS);
    return now > due + guard;
}

void
ErgrEmhcRouting::DeadCheckTick()
{
    if (!IsAlive())
    {
        return;
    }
    const Time now = Simulator::Now();
    std::vector<uint16_t> overdue;
    for (const auto& entry : m_table.Rows())
    {
        if (IsRelevantHead(entry.second) && IsOverdue(entry.second, now))
        {
            overdue.push_back(entry.first);
        }
    }
    for (uint16_t subject : overdue)
    {
        // Observers = me + other nodes' reports that are still fresh.
        auto& reports = m_deadReports[subject];
        for (auto it = reports.begin(); it != reports.end();)
        {
            it = ((now - it->second).GetSeconds() > m_deadReportTtlS) ? reports.erase(it) : std::next(it);
        }
        if (static_cast<int>(reports.size()) + 1 >= m_deadQuorum)
        {
            DeclareDead(subject, "quorum observers=" + std::to_string(reports.size() + 1));
            continue;
        }
        auto last = m_lastDeadReportSent.find(subject);
        if (last == m_lastDeadReportSent.end() || (now - last->second).GetSeconds() >= m_deadReportRepeatS)
        {
            m_lastDeadReportSent[subject] = now;
            Simulator::Schedule(Seconds(m_uniform->GetValue(0.0, m_deadReportJitterS)), &ErgrEmhcRouting::SendDeadReport, this,
                                subject);
        }
    }
    Simulator::Schedule(Seconds(m_deadCheckPeriodS), &ErgrEmhcRouting::DeadCheckTick, this);
}

void
ErgrEmhcRouting::SendDeadReport(uint16_t subject)
{
    const NeighbourEntry* row = m_table.Find(subject);
    if (!IsAlive() || !row || !IsOverdue(*row, Simulator::Now()))
    {
        return; // the head spoke up (or was already removed) during the jitter delay
    }
    ErgrElectionHeader hdr;
    hdr.kind = ErgrElectionHeader::DeadReport;
    hdr.nodeId = m_id;
    hdr.cellId = row->cellId;
    hdr.rank = 0;
    hdr.scoreQ = subject;
    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(hdr);
    m_metrics->OnEvent(m_id, "DEAD_REPORT", "id=" + std::to_string(subject));
    TransmitFrame(packet, FrameType::Election, kNone, params::phy::kNumPowerLevels - 1);
}

void
ErgrEmhcRouting::OnDeadReport(const ErgrElectionHeader& hdr)
{
    if (!m_quorumDetection || m_isSink)
    {
        return;
    }
    const uint16_t subject = hdr.scoreQ;
    const Time now = Simulator::Now();
    const NeighbourEntry* row = m_table.Find(subject);
    if (!row || !IsRelevantHead(*row) || !IsOverdue(*row, now))
    {
        return; // I do not track it, or I still hear it: my own evidence outweighs the report
    }
    m_deadReports[subject][hdr.nodeId] = now;
    auto& reports = m_deadReports[subject];
    for (auto it = reports.begin(); it != reports.end();)
    {
        it = ((now - it->second).GetSeconds() > m_deadReportTtlS) ? reports.erase(it) : std::next(it);
    }
    if (static_cast<int>(reports.size()) + 1 >= m_deadQuorum)
    {
        DeclareDead(subject, "quorum observers=" + std::to_string(reports.size() + 1));
    }
}

void
ErgrEmhcRouting::DeclareDead(uint16_t subject, const std::string& how)
{
    const NeighbourEntry* row = m_table.Find(subject);
    const bool wasHeadOfMyCell = row && row->isHead && row->cellId == m_cell;
    m_metrics->OnEvent(m_id, "HEAD_DEAD", "id=" + std::to_string(subject) + " " + how);
    m_metrics->OnDeadDeclared(m_id, subject, true);
    m_table.Remove(subject);
    m_deadReports.erase(subject);
    m_lastDeadReportSent.erase(subject);
    m_selector->OnKnownChange(ChangeEvent::NodeLost, Simulator::Now());
    if (wasHeadOfMyCell && !m_isSink)
    {
        StartElectionRound("head of my cell dead (quorum)", false);
    }
}

// Planned handover (K§2.3): the outgoing head sends its model to the best-ranked non-head member of its cell.
void
ErgrEmhcRouting::SendHandoverToSuccessor()
{
    ErgrHandoverHeader hdr;
    if (!m_selector->OnHandoverOut(hdr, Simulator::Now()))
    {
        return;
    }
    Time now = Simulator::Now();
    const NeighbourEntry* successor = nullptr;
    double bestScore = -1.0;
    for (const auto& entry : m_table.Rows())
    {
        const NeighbourEntry& row = entry.second;
        if (row.isSink || row.isHead || row.cellId != m_cell)
        {
            continue;
        }
        double score = ScoreOf(row.battery, row.PredictedPos(now), row.cellId, false);
        if (score > bestScore)
        {
            bestScore = score;
            successor = &row;
        }
    }
    if (!successor)
    {
        return;
    }
    hdr.fromId = m_id;
    hdr.toId = successor->id;
    hdr.cellId = m_cell;
    Ptr<Packet> packet = Create<Packet>(0);
    packet->AddHeader(hdr);
    m_metrics->OnEvent(m_id, "HANDOVER_OUT", "to=" + std::to_string(successor->id));
    TransmitFrame(packet, FrameType::Handover, successor->id, params::phy::kNumPowerLevels - 1);
}

void
ErgrEmhcRouting::OnHandover(const ErgrHandoverHeader& hdr)
{
    m_metrics->OnEvent(m_id, "HANDOVER_IN", "from=" + std::to_string(hdr.fromId));
    m_selector->OnHandoverIn(hdr, Simulator::Now());
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
    if (m_feedback)
    {
        m_feedback->OnVoid(hdr.nackerId, pkt.lastHopSeq); // a void counts as a failure label if none arrived yet
    }
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
    if (powerLevel < params::phy::kNumPowerLevels - 1)
    {
        packet->AddPacketTag(TxPowerLevelTag(powerLevel)); // reduced power: read by the channel and the energy hook
    }
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
    case FrameType::Ack: {
        ErgrAckHeader hdr;
        packet->RemoveHeader(hdr);
        bool mine = hdr.senderId == m_id;
        m_metrics->OnFrameRx(m_id, FrameType::Ack, bytes, mine, hdr.ackerId);
        if (mine && m_feedback)
        {
            m_feedback->OnAck(hdr.ackerId, hdr.hopSeq);
        }
        break;
    }
    case FrameType::Handover: {
        ErgrHandoverHeader hdr;
        packet->RemoveHeader(hdr);
        bool mine = hdr.toId == m_id;
        m_metrics->OnFrameRx(m_id, FrameType::Handover, bytes, mine, hdr.fromId);
        if (mine)
        {
            OnHandover(hdr);
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
        NS_LOG_WARN("unknown frame type " << int(PeekFrameType(packet)));
        break;
    }
    return true;
}

} // namespace cbergr
} // namespace ns3
