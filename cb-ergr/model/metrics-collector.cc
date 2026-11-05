/*
 * metrics-collector.cc: counters, consistency checks and CSV writers.
 * Implements spec §13.1 (summary), kickoff §2.6 (nodes/packets/events/frames, reason codes, checks).
 */

#include "metrics-collector.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace ns3
{
namespace cbergr
{

NS_LOG_COMPONENT_DEFINE("CbErgrMetrics");
NS_OBJECT_ENSURE_REGISTERED(MetricsCollector);

const char*
DropReasonName(DropReason r)
{
    switch (r)
    {
    case DropReason::NoEligibleForwarder:
        return "NO_ELIGIBLE_FORWARDER";
    case DropReason::MaxTxReached:
        return "MAX_TX_REACHED";
    case DropReason::HopBudgetExhausted:
        return "HOP_BUDGET_EXHAUSTED";
    case DropReason::QueueFull:
        return "QUEUE_FULL";
    case DropReason::NodeDiedHolding:
        return "NODE_DIED_HOLDING";
    case DropReason::TtlExpired:
        return "TTL_EXPIRED";
    default:
        return "?";
    }
}

const char*
RxFailReasonName(RxFailReason r)
{
    switch (r)
    {
    case RxFailReason::Collision:
        return "COLLISION";
    case RxFailReason::PerLoss:
        return "PER_LOSS";
    case RxFailReason::HalfDuplex:
        return "HALF_DUPLEX";
    case RxFailReason::OutOfRange:
        return "OUT_OF_RANGE";
    case RxFailReason::ReceiverAsleep:
        return "RECEIVER_ASLEEP";
    case RxFailReason::ReceiverDead:
        return "RECEIVER_DEAD";
    default:
        return "?";
    }
}

namespace
{
const FrameType kAllTypes[] = {FrameType::Data,
                               FrameType::Ack,
                               FrameType::Beacon,
                               FrameType::Election,
                               FrameType::Handover,
                               FrameType::Nack};

double
NowS()
{
    return Simulator::Now().GetSeconds();
}

int
Idx(FrameType t)
{
    return static_cast<int>(t);
}

std::string
Lower(const char* s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}
} // namespace

TypeId
MetricsCollector::GetTypeId()
{
    static TypeId tid = TypeId("ns3::cbergr::MetricsCollector")
                            .SetParent<Object>()
                            .AddConstructor<MetricsCollector>();
    return tid;
}

void
MetricsCollector::Configure(const Config& cfg)
{
    m_cfg = cfg;
    m_loss.Configure(m_cfg.bitRateBps, m_cfg.preambleS, m_cfg.warmupS, m_cfg.simTimeS);
    m_loss.EnableCounterfactual(m_cfg.logCounterfactual);
    m_loss.SetDeathProbe([this](uint32_t node) {
        auto it = m_nodes.find(node);
        return (it != m_nodes.end() && it->second.energy) ? it->second.energy->GetDeathTimeS() : -1.0;
    });
    std::filesystem::create_directories(m_cfg.outDir);
    if (m_cfg.logDecisions)
    {
        m_decisionsFile.open(m_cfg.outDir + "/decisions.csv");
        m_decisionsFile << "time_s,node,fwd,fwd_cell,hop_seq,power_level,attempt,ar,learn,n_candidates,chosen_rank";
        for (int i = 1; i <= kMaxFeatureDim; ++i)
        {
            m_decisionsFile << ",x" << i;
        }
        m_decisionsFile << ",p_mean,width,score,lambda_e,label,label_source,label_delay_s,truth_received\n";
    }
    if (m_cfg.logFrames)
    {
        m_framesFile.open(m_cfg.outDir + "/frames.csv");
        m_framesFile << "time_s,node,event,frame_type,sender,receiver,bytes,power_level,distance_m,"
                        "snr_db,fail_reason\n";
    }
}

bool
MetricsCollector::Counting() const
{
    return NowS() >= m_cfg.warmupS;
}

void
MetricsCollector::RegisterNode(uint32_t nodeId, bool isSink, Ptr<AquaSimEnergyModel> energy)
{
    NodeStats& n = m_nodes[nodeId];
    n.isSink = isSink;
    n.energy = energy;
}

void
MetricsCollector::SnapshotEnergyAtWarmup()
{
    for (auto& entry : m_nodes)
    {
        NodeStats& n = entry.second;
        if (n.energy)
        {
            n.txAtWarmup = n.energy->GetTxEnergyJ();
            n.rxAtWarmup = n.energy->GetRxEnergyJ();
            n.idleAtWarmup = n.energy->GetIdleEnergyJ();
        }
    }
}

// ------------------------------------------------------------ frame hooks --

void
MetricsCollector::OnFrameTx(uint32_t node,
                            FrameType type,
                            uint32_t bytes,
                            uint8_t powerLevel,
                            int32_t receiver)
{
    if (m_cfg.logFrames && m_framesFile.is_open())
    {
        m_framesFile << NowS() << ',' << node << ",TX_START," << FrameTypeName(type) << ',' << node
                     << ',' << receiver << ',' << bytes << ',' << int(powerLevel) << ",,,\n";
    }
    if (!Counting())
    {
        return;
    }
    m_loss.OnFrameTx(node, type, bytes, receiver, NowS());
    NodeStats& n = Node(node);
    n.framesTx[Idx(type)]++;
    n.bytesTx[Idx(type)] += bytes;
    if (m_cfg.bitRateBps > 0.0 && !n.isSink) // sinks have no battery: like energy_tx_j, ordinary nodes only
    {
        m_txEnergyByType[Idx(type)] += m_cfg.txPowerFullW * params::phy::kTxPowerLevelScale[std::min<int>(powerLevel, 2)] *
                                       (m_cfg.preambleS + bytes * 8.0 / m_cfg.bitRateBps);
    }
    m_totalFramesTx[Idx(type)]++;
    m_totalBytesTx[Idx(type)] += bytes;
}

void
MetricsCollector::OnFrameRx(uint32_t node, FrameType type, uint32_t bytes, bool addressedToMe, int32_t sender)
{
    if (m_cfg.logFrames && m_framesFile.is_open())
    {
        m_framesFile << NowS() << ',' << node << ",RX_OK," << FrameTypeName(type) << ',' << sender
                     << ',' << node << ',' << bytes << ",,,,\n";
    }
    if (!Counting())
    {
        return;
    }
    NodeStats& n = Node(node);
    if (m_cfg.bitRateBps > 0.0 && !n.isSink)
    {
        m_rxEnergyByType[Idx(type)] += m_cfg.rxPowerW * (m_cfg.preambleS + bytes * 8.0 / m_cfg.bitRateBps);
    }
    if (addressedToMe)
    {
        n.framesRxOk[Idx(type)]++;
        n.bytesRxOk[Idx(type)] += bytes;
    }
    else
    {
        n.framesOverheard[Idx(type)]++;
    }
}

void
MetricsCollector::OnLinkEval(uint32_t txNode,
                             uint32_t rxNode,
                             FrameType type,
                             uint32_t bytes,
                             double distM,
                             double snrDb,
                             bool ok,
                             RxFailReason reason,
                             uint64_t dataId)
{
    if (m_cfg.logFrames && m_framesFile.is_open() && !ok)
    {
        m_framesFile << NowS() << ',' << rxNode << ",RX_FAIL," << FrameTypeName(type) << ',' << txNode
                     << ',' << rxNode << ',' << bytes << ",," << distM << ',' << snrDb << ','
                     << RxFailReasonName(reason) << '\n';
    }
    m_loss.OnLinkEval(txNode, rxNode, type, bytes, distM, ok, NowS(), dataId);
    if (type == FrameType::Data)
    {
        SnrStats& snr = m_linkSnr[{txNode, rxNode}];
        snr.sum += snrDb;
        snr.n++;
    }
    if (ok || !Counting())
    {
        return;
    }
    Node(rxNode).rxFail[Idx(type)][static_cast<int>(reason)]++;
}

void
MetricsCollector::OnPhyRxError(uint32_t node)
{
    if (Counting())
    {
        Node(node).phyRxErrors++;
    }
}

// ----------------------------------------------------------- packet hooks --

void
MetricsCollector::OnPacketCreated(E2EKey key, uint32_t node)
{
    PacketRecord& rec = m_packets[key];
    rec.key = key;
    rec.createdS = NowS();
    rec.lastActivityS = rec.createdS;
    rec.path = std::to_string(node);
    Node(node).dataOriginated += Counting() ? 1 : 0;
}

void
MetricsCollector::OnPacketAtNode(E2EKey key, uint32_t node)
{
    auto it = m_packets.find(key);
    if (it == m_packets.end())
    {
        return;
    }
    it->second.path += "-" + std::to_string(node);
    it->second.hops++;
    it->second.lastActivityS = NowS();
    Node(node).dataForwarded += Counting() ? 1 : 0;
}

void
MetricsCollector::OnDataSent(E2EKey key, uint32_t node, bool isRetry)
{
    m_loss.OnDataKey(node, key);
    auto it = m_packets.find(key);
    if (it == m_packets.end())
    {
        return;
    }
    it->second.dataTx++;
    it->second.retx += isRetry ? 1 : 0;
    it->second.lastActivityS = NowS();
    if (Counting())
    {
        NodeStats& n = Node(node);
        n.attempts++;
        (isRetry ? n.retransmissions : n.firstAttempts)++;
    }
}

void
MetricsCollector::OnDelivered(E2EKey key, uint32_t sinkNode)
{
    auto it = m_packets.find(key);
    if (it == m_packets.end() || it->second.delivered)
    {
        return;
    }
    it->second.delivered = true;
    it->second.deliveredS = NowS();
    it->second.sinkNode = sinkNode;
    it->second.lastActivityS = NowS();
    it->second.path += "-" + std::to_string(sinkNode);
    it->second.hops++;
}

void
MetricsCollector::OnDrop(uint32_t node, E2EKey key, DropReason reason)
{
    if (Counting())
    {
        Node(node).drops[static_cast<int>(reason)]++;
    }
    auto it = m_packets.find(key);
    if (it == m_packets.end() || it->second.delivered || it->second.dropped)
    {
        return;
    }
    it->second.dropped = true;
    it->second.dropReason = reason;
    it->second.dropNode = node;
    it->second.lastActivityS = NowS();
}

void
MetricsCollector::OnDuplicate(uint32_t node)
{
    if (Counting())
    {
        Node(node).dataDuplicates++;
    }
}

// -------------------------------------------------------- feedback hooks --

void
MetricsCollector::OnAttemptSent(uint32_t sender,
                                uint16_t fwd,
                                uint16_t hopSeq,
                                uint8_t attempt,
                                bool ackRequested,
                                bool learn,
                                bool tracked,
                                double predictedDistM,
                                const Decision& decision)
{
    Outstanding& o = m_outstanding[HopId(sender, fwd, hopSeq)];
    o.attempt = attempt;
    o.received = false;
    o.sentS = NowS();
    if (m_outstanding.size() > 300000)
    {
        for (auto it = m_outstanding.begin(); it != m_outstanding.end();)
        {
            it = (NowS() - it->second.sentS > 900.0) ? m_outstanding.erase(it) : std::next(it);
        }
    }
    if (tracked)
    {
        m_trackedAttempts++;   // consistency counters ignore the warm-up
    }
    if (!Counting())
    {
        return;
    }
    LinkStats& link = m_links[{sender, fwd}];
    link.attempts++;
    link.retx += attempt > 1 ? 1 : 0;
    link.distSumM += predictedDistM;
    link.predSum += decision.pMean;
    link.firstS = link.firstS < 0 ? NowS() : link.firstS;
    link.lastS = NowS();
    if (ackRequested)
    {
        Node(sender).arRequested++;
        m_arAttempts++;
    }
    m_totalAttempts++;
}

void
MetricsCollector::OnAttemptDiscarded(uint32_t node)
{
    m_discardedAttempts++;
}

void
MetricsCollector::OnHopReceived(uint32_t sender, uint32_t fwd, uint16_t hopSeq)
{
    auto it = m_outstanding.find(HopId(sender, fwd, hopSeq));
    if (it == m_outstanding.end())
    {
        return;
    }
    it->second.received = true;
    if (Counting())
    {
        NodeStats& n = Node(sender);
        (it->second.attempt > 1 ? n.hopSuccessRetry : n.hopSuccessFirst)++;
        m_links[{sender, fwd}].truthSuccess++;
    }
}

bool
MetricsCollector::HopWasReceived(uint32_t sender, uint32_t fwd, uint16_t hopSeq) const
{
    auto it = m_outstanding.find(HopId(sender, fwd, hopSeq));
    return it != m_outstanding.end() && it->second.received;
}

void
MetricsCollector::OnLabel(uint32_t node,
                          uint16_t fwd,
                          uint16_t hopSeq,
                          int label,
                          LabelSource source,
                          double delayS,
                          uint8_t attempt,
                          bool ackRequested,
                          bool learn,
                          const Decision& decision)
{
    bool truth = HopWasReceived(node, fwd, hopSeq);
    m_outstanding.erase(HopId(node, fwd, hopSeq));
    WriteDecisionLine(node, fwd, hopSeq, label, source, delayS, attempt, ackRequested, learn, truth, decision);
    m_labelsAllTime++;   // consistency counter, ignores the warm-up
    if (!Counting())
    {
        return;
    }
    const int src = static_cast<int>(source);
    NodeStats& n = Node(node);
    if (label < 0)
    {
        // Censored: no label reaches the learner. Count it, and how many had in fact arrived (logging only).
        n.labelsBySource[src]++;
        m_labelsBySource[src]++;
        m_censored++;
        m_censoredReceived += truth ? 1 : 0;
        return;
    }
    n.labelsBySource[src]++;
    n.labelsByValue[label]++;
    n.labelDelaySumS += delayS;
    n.ackTimeouts += source == LabelSource::AckTimeout ? 1 : 0;
    LinkStats& link = m_links[{node, fwd}];
    link.labelsBySource[src]++;
    (label ? link.labelOnes : link.labelZeros)++;
    m_totalLabels++;
    m_labelsBySource[src]++;
    m_labelDelaySumS += delayS;
    m_labelDelays.push_back(delayS);
    if (label == 0)
    {
        m_labelZeros++;
        m_falseNegatives += truth ? 1 : 0;   // label 0 although the forwarder did get the frame
    }
}

void
MetricsCollector::OnBeaconParts(uint32_t base, uint32_t receipts, uint32_t blob, uint32_t q)
{
    if (!Counting())
    {
        return;
    }
    m_bcnBase += base;
    m_bcnReceipts += receipts;
    m_bcnBlob += blob;
    m_bcnQ += q;
    m_bcnCount++;
}

void
MetricsCollector::OnDecisionDetail(double regretOracle, double regretVsFw, double gapTop2, bool twoViable)
{
    if (!Counting())
    {
        return;
    }
    DetailBin& b = m_detailBins[static_cast<int>(NowS() / 100.0)];
    b.n++;
    b.regretOracle += regretOracle;
    b.regretVsFw += regretVsFw;
    b.gap += gapTop2;
    b.twoViable += twoViable ? 1 : 0;
    b.gapSmall += (twoViable && gapTop2 < 0.05) ? 1 : 0;
}

void
MetricsCollector::WriteCost()
{
    std::ofstream f(m_cfg.outDir + "/cost.csv");
    f << std::setprecision(10) << "key,value\n";
    static const char* kinds[] = {"select", "sent", "label"};
    for (int k = 0; k < 3; ++k)
    {
        std::vector<float>& v = m_cpuNs[k];
        if (v.empty())
        {
            continue;
        }
        std::sort(v.begin(), v.end());
        double sum = 0.0;
        for (float x : v)
        {
            sum += x;
        }
        f << "cpu_" << kinds[k] << "_calls," << v.size() << "\ncpu_" << kinds[k] << "_us_mean," << sum / v.size() / 1000.0 << "\ncpu_" << kinds[k]
          << "_us_p95," << v[static_cast<size_t>(0.95 * (v.size() - 1))] / 1000.0 << "\ncpu_" << kinds[k] << "_us_max," << v.back() / 1000.0 << '\n';
    }
    const double delivered = std::max<double>(1.0, PacketsDelivered());
    f << "beacons_counted," << m_bcnCount << "\nbeacon_bytes_base," << m_bcnBase << "\nbeacon_bytes_receipts," << m_bcnReceipts
      << "\nbeacon_bytes_blob," << m_bcnBlob << "\nbeacon_bytes_q," << m_bcnQ << '\n';
    f << "beacon_base_bytes_per_delivered," << m_bcnBase / delivered << "\nbeacon_receipt_bytes_per_delivered," << m_bcnReceipts / delivered
      << "\nbeacon_blob_bytes_per_delivered," << m_bcnBlob / delivered << "\nbeacon_q_bytes_per_delivered," << m_bcnQ / delivered << '\n';
    if (m_diagProbe)
    {
        for (const auto& kv : m_diagProbe())
        {
            f << kv.first << ',' << kv.second << '\n';
        }
    }
    if (m_neighbourProbe)
    {
        std::vector<uint32_t> sizes = m_neighbourProbe();
        if (!sizes.empty())
        {
            std::sort(sizes.begin(), sizes.end());
            double sum = 0.0;
            for (uint32_t s : sizes)
            {
                sum += s;
            }
            f << "neighbour_rows_mean," << sum / sizes.size() << "\nneighbour_rows_p95," << sizes[static_cast<size_t>(0.95 * (sizes.size() - 1))]
              << "\nneighbour_rows_max," << sizes.back() << '\n';
        }
    }
    f << "peak_pending_attempts," << m_peakPending << '\n';
    for (const auto& kv : m_costNotes)
    {
        f << kv.first << ',' << kv.second << '\n';
    }
    if (!m_detailBins.empty())
    {
        std::ofstream r(m_cfg.outDir + "/regret_by_time.csv");
        r << "bin_start_s,decisions,regret_vs_oracle,regret_vs_fw,gap_top2_mean,share_two_viable,share_two_viable_gap_lt_0.05\n";
        for (const auto& kv : m_detailBins)
        {
            const DetailBin& b = kv.second;
            r << kv.first * 100 << ',' << b.n << ',' << b.regretOracle / b.n << ',' << b.regretVsFw / b.n << ',' << b.gap / b.n << ','
              << double(b.twoViable) / b.n << ',' << double(b.gapSmall) / b.n << '\n';
        }
    }
}

void
MetricsCollector::OnPendingSize(size_t n)
{
    m_peakPending = std::max<uint64_t>(m_peakPending, n);
}

void
MetricsCollector::OnAckTimeoutFired(uint32_t node, uint16_t fwd, uint16_t hopSeq)
{
    if (!Counting())
    {
        return;
    }
    Node(node).ackTimeouts++;
    m_timeoutsFired++;
    m_timeoutsFalse += HopWasReceived(node, fwd, hopSeq) ? 1 : 0;
}

void
MetricsCollector::OnDuplicateLabel(uint32_t node)
{
    if (Counting())
    {
        Node(node).duplicateLabels++;
        m_duplicateLabelsTotal++;
    }
}

void
MetricsCollector::OnRegret(double regret, bool multiViable)
{
    if (!Counting())
    {
        return;
    }
    m_regretSum += regret;
    m_regretCount++;
    if (multiViable)
    {
        m_regretSumMulti += regret;
        m_regretCountMulti++;
    }
}

void
MetricsCollector::OnAckSent(uint32_t node)
{
    if (Counting())
    {
        Node(node).acksSent++;
    }
}

void
MetricsCollector::OnAckReceived(uint32_t node)
{
    if (Counting())
    {
        Node(node).acksReceived++;
    }
}

void
MetricsCollector::WriteDecisionLine(uint32_t node,
                                    uint16_t fwd,
                                    uint16_t hopSeq,
                                    int label,
                                    LabelSource src,
                                    double delayS,
                                    uint8_t attempt,
                                    bool ar,
                                    bool learn,
                                    bool truth,
                                    const Decision& d)
{
    if (!m_decisionsFile.is_open())
    {
        return;
    }
    static const char* names[] = {"ACK", "ACK_TIMEOUT", "BITMAP", "EXPIRY", "VOID", "CENSORED"};
    m_decisionsFile << NowS() - delayS << ',' << node << ',' << fwd << ',' << d.fwdCell << ',' << hopSeq << ',' << int(d.powerLevel) << ','
                    << int(attempt) << ',' << ar << ',' << learn << ',' << int(d.numCandidates) << ','
                    << int(d.chosenRank);
    for (int i = 0; i < kMaxFeatureDim; ++i)
    {
        m_decisionsFile << ',' << d.x[i];
    }
    m_decisionsFile << ',' << d.pMean << ',' << d.width << ',' << d.score << ',' << d.lambdaE << ','
                    << label << ',' << names[static_cast<int>(src)] << ',' << delayS << ',' << truth << '\n';
}

// ----------------------------------------------------------------- events --

void
MetricsCollector::OnCoverageSample(uint32_t occupiedCells, uint32_t headedCells, uint32_t nodes, uint32_t nodesInHeadlessCells)
{
    if (!Counting())
    {
        return;
    }
    m_covOccupied += occupiedCells;
    m_covHeaded += headedCells;
    m_covNodes += nodes;
    m_covNodesHeadless += nodesInHeadlessCells;
}

void
MetricsCollector::OnDataDelivered(E2EKey key, uint32_t sender, uint32_t fwd)
{
    m_loss.OnDataDelivered(key, sender, fwd, NowS());
}

void
MetricsCollector::OnNoHeadGiveUp(uint32_t node, uint16_t cell)
{
    if (!Counting())
    {
        return;
    }
    m_noHeadGiveUps++;
    const bool headExists = m_cellHasHead && m_cellHasHead(cell);
    if (headExists)
    {
        m_noHeadGiveUpsWithHead++;
    }
    OnEvent(node, "NO_HEAD_GIVE_UP", "cell=" + std::to_string(cell) + " cellHasHead=" + (headExists ? "1" : "0"));
}

void
MetricsCollector::OnDeadDeclared(uint32_t node, uint32_t subject, bool byQuorum)
{
    if (!Counting())
    {
        return;
    }
    m_deadDeclarations++;
    (byQuorum ? m_deadByQuorum : m_deadByExpiry)++;
    auto it = m_nodes.find(subject);
    const double deathS = (it != m_nodes.end() && it->second.energy) ? it->second.energy->GetDeathTimeS() : -1.0;
    if (deathS < 0.0 || deathS > NowS())
    {
        m_deadFalse++; // the subject was alive
        return;
    }
    if (m_deadFirstLatencyS.find(subject) == m_deadFirstLatencyS.end())
    {
        m_deadFirstLatencyS[subject] = NowS() - deathS;
    }
}

void
MetricsCollector::OnEvent(uint32_t node, const std::string& kind, const std::string& details)
{
    if (kind == "VOID_NACK" && Counting())
    {
        m_voidNacks++;
    }
    if (m_cfg.logEvents)
    {
        m_events.push_back({NowS(), node, kind, details});
    }
}

void
MetricsCollector::OnHeadChange(uint32_t node, bool isHead, uint16_t cellId)
{
    NodeStats& n = Node(node);
    double now = NowS();
    if (isHead && n.headSinceS < 0.0)
    {
        n.headSinceS = now;
        n.timesElected++;
        if (Counting())
        {
            m_electionsAfterWarmup++;
        }
    }
    else if (!isHead && n.headSinceS >= 0.0)
    {
        double tenure = now - n.headSinceS;
        n.headTimeS += tenure;
        m_tenuresS.push_back(tenure);
        n.headSinceS = -1.0;
    }
    n.lastCell = cellId;
}

// -------------------------------------------------------------- finalising --

void
MetricsCollector::Finalize()
{
    const double endS = m_cfg.simTimeS > 0 ? m_cfg.simTimeS : NowS();
    auto deathProbe = [this](uint32_t node) {
        auto it = m_nodes.find(node);
        return (it != m_nodes.end() && it->second.energy) ? it->second.energy->GetDeathTimeS() : -1.0;
    };

    // close open head tenures
    for (auto& entry : m_nodes)
    {
        NodeStats& n = entry.second;
        if (n.headSinceS >= 0.0)
        {
            double tenure = endS - n.headSinceS;
            n.headTimeS += tenure;
            m_tenuresS.push_back(tenure);
            n.headSinceS = -1.0;
        }
    }

    m_loss.Finalize(deathProbe);

    // classify packets created inside the measured window
    std::vector<double> delays;
    for (auto& entry : m_packets)
    {
        PacketRecord& rec = entry.second;
        if (rec.createdS < m_cfg.warmupS)
        {
            continue;
        }
        m_createdCounted++;
        if (rec.delivered)
        {
            m_deliveredCounted++;
            delays.push_back(rec.deliveredS - rec.createdS);
        }
        else if (rec.dropped)
        {
            m_droppedCounted++;
            m_dropCounts[static_cast<int>(rec.dropReason)]++;
        }
        else if (endS - rec.lastActivityS >= m_cfg.packetTtlS)
        {
            // Nobody holds it and nobody reported a drop: it was lost on the channel (ground truth).
            rec.dropped = true;
            rec.dropReason = DropReason::TtlExpired;
            m_droppedCounted++;
            m_dropCounts[static_cast<int>(DropReason::TtlExpired)]++;
        }
        else
        {
            m_inFlight++;
        }
    }
    uint32_t resolved = m_createdCounted - m_inFlight;
    m_pdr = resolved > 0 ? static_cast<double>(m_deliveredCounted) / resolved : 0.0;
    if (!delays.empty())
    {
        std::sort(delays.begin(), delays.end());
        double sum = 0;
        for (double d : delays)
        {
            sum += d;
        }
        m_delayMeanS = sum / delays.size();
        m_delayP95S = delays[std::min(delays.size() - 1, static_cast<size_t>(0.95 * delays.size()))];
    }

    // energy over the measured window, node lifetimes, fairness (sinks excluded)
    std::vector<double> deaths;
    std::vector<double> residual;
    for (auto& entry : m_nodes)
    {
        NodeStats& n = entry.second;
        if (n.isSink || !n.energy)
        {
            continue;
        }
        m_txJ += n.energy->GetTxEnergyJ() - n.txAtWarmup;
        m_rxJ += n.energy->GetRxEnergyJ() - n.rxAtWarmup;
        m_idleJ += n.energy->GetIdleEnergyJ() - n.idleAtWarmup;
        if (n.energy->GetDeathTimeS() >= 0.0)
        {
            deaths.push_back(n.energy->GetDeathTimeS());
        }
        residual.push_back(std::max(0.0, n.energy->GetEnergy()));
    }
    double totalJ = m_txJ + m_rxJ + m_idleJ;
    m_rxEnergyShare = totalJ > 0 ? m_rxJ / totalJ : 0.0;
    std::sort(deaths.begin(), deaths.end());
    if (!deaths.empty())
    {
        m_fndS = deaths.front();
    }
    if (!residual.empty() && deaths.size() >= (residual.size() + 1) / 2)
    {
        m_hndS = deaths[(residual.size() + 1) / 2 - 1];
    }
    double sum = 0, sumSq = 0;
    for (double e : residual)
    {
        sum += e;
        sumSq += e * e;
    }
    m_jain = (sumSq > 0 && !residual.empty()) ? (sum * sum) / (residual.size() * sumSq) : 1.0;
    if (!m_tenuresS.empty())
    {
        double t = 0;
        for (double v : m_tenuresS)
        {
            t += v;
        }
        m_tenureMeanS = t / m_tenuresS.size();
    }

    std::vector<std::string> problems = CheckConsistency();
    for (const std::string& p : problems)
    {
        NS_LOG_ERROR("consistency check failed: " << p);
        std::cerr << "CONSISTENCY ERROR: " << p << std::endl;
    }
    NS_ASSERT_MSG(problems.empty(), "metrics consistency check failed (see messages above)");

    std::sort(m_labelDelays.begin(), m_labelDelays.end());
    WriteSummary();
    if (m_cfg.logLinks)
    {
        WriteLinks();
    }
    if (m_decisionsFile.is_open())
    {
        m_decisionsFile.close();
    }
    if (m_cfg.logNodes)
    {
        WriteNodes();
    }
    if (m_cfg.logPackets)
    {
        WritePackets();
    }
    if (m_cfg.logEvents)
    {
        WriteEvents();
    }
    if (m_loss.Enabled())
    {
        WriteLossBudget();
        WriteCost();
        if (m_cfg.logCounterfactual)
        {
            std::ofstream cf(m_cfg.outDir + "/counterfactual.csv");
            cf << LossBudget::CounterfactualHeader() << '\n';
            for (const std::string& row : m_loss.CounterfactualRows())
            {
                cf << row << '\n';
            }
        }
    }
    if (m_framesFile.is_open())
    {
        m_framesFile.close();
    }
}

std::vector<std::string>
MetricsCollector::CheckConsistency() const
{
    std::vector<std::string> problems;

    // 1. Sum over nodes of frames/bytes sent equals the global totals (independent counting paths).
    for (FrameType t : kAllTypes)
    {
        uint64_t frames = 0, bytes = 0;
        for (const auto& entry : m_nodes)
        {
            frames += entry.second.framesTx[Idx(t)];
            bytes += entry.second.bytesTx[Idx(t)];
        }
        if (frames != m_totalFramesTx[Idx(t)] || bytes != m_totalBytesTx[Idx(t)])
        {
            problems.push_back(std::string("frames/bytes sent mismatch for ") + FrameTypeName(t));
        }
    }

    // 2. Every counted packet is exactly one of: delivered, dropped (with a reason), in flight.
    uint32_t reasonSum = 0;
    for (uint32_t c : m_dropCounts)
    {
        reasonSum += c;
    }
    if (reasonSum != m_droppedCounted)
    {
        problems.push_back("drop reason counts do not add up to dropped packets");
    }
    if (m_deliveredCounted + m_droppedCounted + m_inFlight != m_createdCounted)
    {
        problems.push_back("delivered + dropped + in flight != created");
    }

    // 3. Every tracked DATA attempt has exactly one label, is still pending, or was discarded.
    uint64_t pendingAtEnd = 0;
    for (const auto& probe : m_pendingProbes)
    {
        pendingAtEnd += probe();
    }
    if (m_trackedAttempts != m_labelsAllTime + pendingAtEnd + m_discardedAttempts)
    {
        problems.push_back("tracked attempts (" + std::to_string(m_trackedAttempts) + ") != labels (" +
                           std::to_string(m_labelsAllTime) + ") + pending (" + std::to_string(pendingAtEnd) +
                           ") + discarded (" + std::to_string(m_discardedAttempts) + ")");
    }

    // 4. Retransmissions = attempts - first attempts, per node.
    for (const auto& entry : m_nodes)
    {
        const NodeStats& n = entry.second;
        if (n.retransmissions != n.attempts - n.firstAttempts)
        {
            problems.push_back("retransmissions != attempts - first attempts at node " + std::to_string(entry.first));
        }
    }

    // 5. A delivered packet cannot also carry a drop record that was counted as a drop.
    for (const auto& entry : m_packets)
    {
        if (entry.second.delivered && entry.second.retx > entry.second.dataTx)
        {
            problems.push_back("retransmissions exceed transmissions for a packet");
        }
    }
    return problems;
}

// ----------------------------------------------------------------- writers --

void
MetricsCollector::WriteSummary()
{
    std::ofstream f(m_cfg.outDir + "/summary.csv");
    f << std::setprecision(10);
    const double deliveredD = std::max<double>(1.0, m_deliveredCounted);
    const double windowS = std::max(1.0, (m_cfg.simTimeS > 0 ? m_cfg.simTimeS : NowS()) - m_cfg.warmupS);

    f << "selector,flags,seed,run,warmup_s,sim_time_s,packets_created,packets_delivered,"
         "packets_in_flight,packets_dropped,pdr,delay_mean_s,delay_p95_s,data_tx_per_delivered";
    for (FrameType t : {FrameType::Beacon, FrameType::Ack, FrameType::Election, FrameType::Handover,
                        FrameType::Nack})
    {
        f << ",ctrl_bytes_" << Lower(FrameTypeName(t)) << "_per_delivered";
    }
    f << ",ctrl_bytes_total_per_delivered,ack_frames_per_delivered,energy_per_delivered_j,"
         "energy_tx_j,energy_rx_j,energy_idle_j,rx_energy_share,fnd_s,hnd_s,jain_residual_energy,"
         "elections_per_hour,head_tenure_mean_s,attempts_total,retx_per_delivered,hop_success_truth,"
         "ar_rate,labels_total,label_ack_frac,label_acktimeout_frac,label_bitmap_frac,label_expiry_frac,"
         "label_void_frac,label_censored_frac,label_delay_mean_s,label_delay_p95_s,false_negative_rate,duplicate_labels,"
         "decisions_scored,regret_mean,regret_mean_multi_viable,share_multi_viable";
    const FrameType energyTypes[] = {FrameType::Data, FrameType::Ack, FrameType::Beacon, FrameType::Election,
                                     FrameType::Handover, FrameType::Nack};
    f << ",censored_labels,censored_received,ack_timeouts_fired,ack_timeouts_false,peak_pending_attempts";
    f << ",mac_drops,mac_deferrals,solicits_sent,solicits_answered,nohead_giveups,nohead_giveups_cell_had_head,cells_with_head_share,nodes_in_headless_cells_share";
    f << ",dead_declarations,dead_false_declarations,dead_by_quorum,dead_by_expiry,dead_heads_detected,dead_first_latency_mean_s";
    for (FrameType t : energyTypes)
    {
        f << ",energy_" << Lower(FrameTypeName(t)) << "_tx_per_delivered_j,energy_" << Lower(FrameTypeName(t))
          << "_rx_per_delivered_j";
    }
    for (int r = 0; r < static_cast<int>(DropReason::kCount); ++r)
    {
        f << ",drop_" << Lower(DropReasonName(static_cast<DropReason>(r)));
    }
    f << ",params\n";

    f << m_cfg.selectorName << ',' << m_cfg.flags << ',' << m_cfg.seed << ',' << m_cfg.run << ','
      << m_cfg.warmupS << ',' << m_cfg.simTimeS << ',' << m_createdCounted << ',' << m_deliveredCounted
      << ',' << m_inFlight << ',' << m_droppedCounted << ',' << m_pdr << ',' << m_delayMeanS << ','
      << m_delayP95S << ',' << m_totalFramesTx[Idx(FrameType::Data)] / deliveredD;
    double ctrlTotal = 0;
    for (FrameType t : {FrameType::Beacon, FrameType::Ack, FrameType::Election, FrameType::Handover,
                        FrameType::Nack})
    {
        double perDelivered = m_totalBytesTx[Idx(t)] / deliveredD;
        ctrlTotal += perDelivered;
        f << ',' << perDelivered;
    }
    f << ',' << ctrlTotal << ',' << m_totalFramesTx[Idx(FrameType::Ack)] / deliveredD << ','
      << (m_txJ + m_rxJ + m_idleJ) / deliveredD << ',' << m_txJ << ',' << m_rxJ << ',' << m_idleJ << ','
      << m_rxEnergyShare << ',' << m_fndS << ',' << m_hndS << ',' << m_jain << ','
      << m_electionsAfterWarmup / (windowS / 3600.0) << ',' << m_tenureMeanS;
    uint64_t attemptsAll = 0, retxAll = 0, hopOk = 0;
    for (const auto& entry : m_nodes)
    {
        attemptsAll += entry.second.attempts;
        retxAll += entry.second.retransmissions;
        hopOk += entry.second.hopSuccessFirst + entry.second.hopSuccessRetry;
    }
    const double labels = std::max<double>(1.0, m_totalLabels);
    double delayMean = m_totalLabels ? m_labelDelaySumS / m_totalLabels : 0.0;
    double delayP95 = m_labelDelays.empty()
                          ? 0.0
                          : m_labelDelays[std::min(m_labelDelays.size() - 1,
                                                   static_cast<size_t>(0.95 * m_labelDelays.size()))];
    f << ',' << attemptsAll << ',' << retxAll / deliveredD << ',' << (attemptsAll ? double(hopOk) / attemptsAll : 0.0)
      << ',' << (attemptsAll ? double(m_arAttempts) / attemptsAll : 0.0) << ',' << m_totalLabels;
    for (uint64_t c : m_labelsBySource)
    {
        f << ',' << c / labels;
    }
    f << ',' << delayMean << ',' << delayP95 << ',' << (m_labelZeros ? double(m_falseNegatives) / m_labelZeros : 0.0)
      << ',' << m_duplicateLabelsTotal << ',' << m_regretCount << ','
      << (m_regretCount ? m_regretSum / m_regretCount : 0.0) << ','
      << (m_regretCountMulti ? m_regretSumMulti / m_regretCountMulti : 0.0) << ','
      << (m_regretCount ? double(m_regretCountMulti) / m_regretCount : 0.0);
    double firstLatencySum = 0.0;
    for (const auto& entry : m_deadFirstLatencyS)
    {
        firstLatencySum += entry.second;
    }
    f << ',' << m_censored << ',' << m_censoredReceived << ',' << m_timeoutsFired << ',' << m_timeoutsFalse << ',' << m_peakPending;
    f << ',' << m_macDrops << ',' << m_macDeferrals << ',' << m_solicitsSent << ',' << m_solicitAnswered << ',' << m_noHeadGiveUps << ',' << m_noHeadGiveUpsWithHead
      << ',' << (m_covOccupied > 0 ? m_covHeaded / m_covOccupied : -1.0) << ',' << (m_covNodes > 0 ? m_covNodesHeadless / m_covNodes : -1.0);
    f << ',' << m_deadDeclarations << ',' << m_deadFalse << ',' << m_deadByQuorum << ',' << m_deadByExpiry << ','
      << m_deadFirstLatencyS.size() << ','
      << (m_deadFirstLatencyS.empty() ? -1.0 : firstLatencySum / m_deadFirstLatencyS.size());
    for (FrameType t : energyTypes)
    {
        f << ',' << m_txEnergyByType[Idx(t)] / deliveredD << ',' << m_rxEnergyByType[Idx(t)] / deliveredD;
    }
    for (uint32_t c : m_dropCounts)
    {
        f << ',' << c;
    }
    f << ",\"" << m_cfg.scenarioParams << "\"\n";
}

// PHY error events that were not explained by a PER loss are collisions or half-duplex losses.
uint64_t
MetricsCollector::CollisionsOf(const NodeStats& n) const
{
    uint64_t perLosses = 0;
    for (const auto& byReason : n.rxFail)
    {
        perLosses += byReason[static_cast<int>(RxFailReason::PerLoss)];
    }
    return n.phyRxErrors > perLosses ? n.phyRxErrors - perLosses : 0;
}

void
MetricsCollector::WriteNodes()
{
    std::ofstream f(m_cfg.outDir + "/nodes.csv");
    f << std::setprecision(8);
    f << "node,is_sink,last_cell,head_time_s,times_elected,death_time_s,energy_final_j,energy_tx_j,"
         "energy_rx_j,energy_idle_j,rx_collision_or_halfduplex,data_originated,data_accepted,data_duplicates";
    for (FrameType t : kAllTypes)
    {
        std::string n = Lower(FrameTypeName(t));
        f << ",frames_tx_" << n << ",bytes_tx_" << n << ",frames_rx_ok_" << n << ",overheard_" << n
          << ",lost_per_" << n << ",lost_halfduplex_" << n;
    }
    for (int r = 0; r < static_cast<int>(DropReason::kCount); ++r)
    {
        f << ",drop_" << Lower(DropReasonName(static_cast<DropReason>(r)));
    }
    f << ",attempts,first_attempts,retransmissions,hop_success_first,hop_success_retry,hop_success_rate_truth,"
         "ar_requested,acks_sent,acks_received,ack_timeouts,labels_ack,labels_acktimeout,labels_bitmap,"
         "labels_expiry,labels_void,labels_censored,labels_1,labels_0,label_delay_mean_s,duplicate_labels";
    f << '\n';
    for (const auto& entry : m_nodes)
    {
        const NodeStats& n = entry.second;
        f << entry.first << ',' << n.isSink << ',' << n.lastCell << ',' << n.headTimeS << ','
          << n.timesElected << ',' << (n.energy ? n.energy->GetDeathTimeS() : -1.0) << ','
          << (n.energy ? n.energy->GetEnergy() : 0.0) << ','
          << (n.energy ? n.energy->GetTxEnergyJ() : 0.0) << ','
          << (n.energy ? n.energy->GetRxEnergyJ() : 0.0) << ','
          << (n.energy ? n.energy->GetIdleEnergyJ() : 0.0) << ',' << CollisionsOf(n) << ','
          << n.dataOriginated << ',' << n.dataForwarded << ',' << n.dataDuplicates;
        for (FrameType t : kAllTypes)
        {
            int i = Idx(t);
            f << ',' << n.framesTx[i] << ',' << n.bytesTx[i] << ',' << n.framesRxOk[i] << ','
              << n.framesOverheard[i] << ',' << n.rxFail[i][static_cast<int>(RxFailReason::PerLoss)] << ','
              << n.rxFail[i][static_cast<int>(RxFailReason::HalfDuplex)];
        }
        for (uint64_t c : n.drops)
        {
            f << ',' << c;
        }
        uint64_t nLabels = n.labelsByValue[0] + n.labelsByValue[1];
        f << ',' << n.attempts << ',' << n.firstAttempts << ',' << n.retransmissions << ','
          << n.hopSuccessFirst << ',' << n.hopSuccessRetry << ','
          << (n.attempts ? double(n.hopSuccessFirst + n.hopSuccessRetry) / n.attempts : 0.0) << ','
          << n.arRequested << ',' << n.acksSent << ',' << n.acksReceived << ',' << n.ackTimeouts;
        for (uint64_t c : n.labelsBySource)
        {
            f << ',' << c;
        }
        f << ',' << n.labelsByValue[1] << ',' << n.labelsByValue[0] << ','
          << (nLabels ? n.labelDelaySumS / nLabels : 0.0) << ',' << n.duplicateLabels << '\n';
    }
}

void
MetricsCollector::WriteLinks()
{
    std::ofstream f(m_cfg.outDir + "/links.csv");
    f << std::setprecision(8);
    f << "sender,forwarder,attempts,successes_truth,labels_1,labels_0,labels_ack,labels_acktimeout,"
         "labels_bitmap,labels_expiry,labels_void,labels_censored,retransmissions,mean_distance_m,mean_snr_db,"
         "mean_predicted_success,first_use_s,last_use_s\n";
    for (const auto& entry : m_links)
    {
        const LinkStats& l = entry.second;
        auto snr = m_linkSnr.find(entry.first);
        double meanSnr = (snr != m_linkSnr.end() && snr->second.n) ? snr->second.sum / snr->second.n : 0.0;
        f << entry.first.first << ',' << entry.first.second << ',' << l.attempts << ',' << l.truthSuccess << ','
          << l.labelOnes << ',' << l.labelZeros;
        for (uint64_t c : l.labelsBySource)
        {
            f << ',' << c;
        }
        f << ',' << l.retx << ',' << (l.attempts ? l.distSumM / l.attempts : 0.0) << ',' << meanSnr << ','
          << (l.attempts ? l.predSum / l.attempts : 0.0) << ',' << l.firstS << ',' << l.lastS << '\n';
    }
}

// One cause for every packet that was not delivered (see loss-budget.h): its drop reason if the protocol dropped it
// knowingly, else the cause of its last failed hop, else "lost_after_receipt" / "never_sent".
std::string
MetricsCollector::LossCauseOf(const PacketRecord& r) const
{
    if (r.delivered)
    {
        return "";
    }
    if (r.dropped && r.dropReason != DropReason::TtlExpired)
    {
        return std::string("drop_") + DropReasonName(r.dropReason);
    }
    std::string hop = m_loss.PacketHopCause(r.key);
    if (hop.empty())
    {
        return r.dropped ? "never_sent" : "";
    }
    return r.dropped || r.createdS < m_cfg.warmupS ? hop : "";
}

void
MetricsCollector::WriteLossBudget()
{
    std::ofstream f(m_cfg.outDir + "/loss_budget.csv");
    f << std::setprecision(10) << "group,key,value\n";
    f << "hop,attempts," << m_loss.HopsTotal() << "\nhop,delivered," << m_loss.HopsDelivered() << '\n';
    for (const auto& c : m_loss.HopCauses())
    {
        f << "hop," << c.first << ',' << c.second << '\n';
    }
    // packets created in the window and resolved, by cause
    std::map<std::string, uint64_t> causes;
    uint64_t resolved = 0;
    for (const auto& entry : m_packets)
    {
        const PacketRecord& r = entry.second;
        if (r.createdS < m_cfg.warmupS)
        {
            continue;
        }
        if (r.delivered)
        {
            causes["delivered"]++;
            resolved++;
        }
        else if (r.dropped)
        {
            causes[LossCauseOf(r)]++;
            resolved++;
        }
    }
    f << "packet,resolved," << resolved << '\n';
    for (const auto& c : causes)
    {
        f << "packet," << c.first << ',' << c.second << '\n';
    }
    double total = 0.0;
    for (FrameType t : {FrameType::Data, FrameType::Ack, FrameType::Beacon, FrameType::Election, FrameType::Handover, FrameType::Nack})
    {
        total += m_loss.AirtimeS(t);
    }
    const double window = std::max(1.0, m_loss.WindowS());
    for (FrameType t : {FrameType::Data, FrameType::Ack, FrameType::Beacon, FrameType::Election, FrameType::Handover, FrameType::Nack})
    {
        f << "airtime_share," << Lower(FrameTypeName(t)) << ',' << (total > 0 ? m_loss.AirtimeS(t) / total : 0.0) << '\n';
        f << "airtime_erlang," << Lower(FrameTypeName(t)) << ',' << m_loss.AirtimeS(t) / window << '\n';
    }
    f << "airtime_erlang,total," << total / window << '\n';
    f << "routing,void_nacks," << m_voidNacks << '\n';
}

void
MetricsCollector::WritePackets()
{
    std::ofstream f(m_cfg.outDir + "/packets.csv");
    f << std::setprecision(8);
    f << "source,seq,created_s,status,delivered_s,sink,delay_s,hops,data_tx,retx,path,drop_node,"
         "drop_reason,loss_cause\n";
    for (const auto& entry : m_packets)
    {
        const PacketRecord& r = entry.second;
        const char* status = r.delivered ? "delivered" : (r.dropped ? "dropped" : "in_flight");
        f << r.key.source << ',' << r.key.seq << ',' << r.createdS << ',' << status << ','
          << (r.delivered ? r.deliveredS : -1.0) << ',' << (r.delivered ? int(r.sinkNode) : -1) << ','
          << (r.delivered ? r.deliveredS - r.createdS : -1.0) << ',' << r.hops << ',' << r.dataTx << ','
          << r.retx << ',' << r.path << ',' << (r.dropped ? int(r.dropNode) : -1) << ','
          << (r.dropped ? DropReasonName(r.dropReason) : "") << ',' << LossCauseOf(r) << '\n';
    }
}

void
MetricsCollector::WriteEvents()
{
    std::ofstream f(m_cfg.outDir + "/events.csv");
    f << std::setprecision(8);
    f << "time_s,node,event,details\n";
    for (const EventRow& e : m_events)
    {
        f << e.t << ',' << e.node << ',' << e.kind << ',' << e.details << '\n';
    }
}

} // namespace cbergr
} // namespace ns3
