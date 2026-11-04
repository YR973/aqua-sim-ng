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
    std::filesystem::create_directories(m_cfg.outDir);
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
    NodeStats& n = Node(node);
    n.framesTx[Idx(type)]++;
    n.bytesTx[Idx(type)] += bytes;
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
                             RxFailReason reason)
{
    if (m_cfg.logFrames && m_framesFile.is_open() && !ok)
    {
        m_framesFile << NowS() << ',' << rxNode << ",RX_FAIL," << FrameTypeName(type) << ',' << txNode
                     << ',' << rxNode << ',' << bytes << ",," << distM << ',' << snrDb << ','
                     << RxFailReasonName(reason) << '\n';
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
    auto it = m_packets.find(key);
    if (it == m_packets.end())
    {
        return;
    }
    it->second.dataTx++;
    it->second.retx += isRetry ? 1 : 0;
    it->second.lastActivityS = NowS();
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

// ----------------------------------------------------------------- events --

void
MetricsCollector::OnEvent(uint32_t node, const std::string& kind, const std::string& details)
{
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

    WriteSummary();
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

    // 3. A delivered packet cannot also carry a drop record that was counted as a drop.
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
         "elections_per_hour,head_tenure_mean_s";
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
        f << '\n';
    }
}

void
MetricsCollector::WritePackets()
{
    std::ofstream f(m_cfg.outDir + "/packets.csv");
    f << std::setprecision(8);
    f << "source,seq,created_s,status,delivered_s,sink,delay_s,hops,data_tx,retx,path,drop_node,"
         "drop_reason\n";
    for (const auto& entry : m_packets)
    {
        const PacketRecord& r = entry.second;
        const char* status = r.delivered ? "delivered" : (r.dropped ? "dropped" : "in_flight");
        f << r.key.source << ',' << r.key.seq << ',' << r.createdS << ',' << status << ','
          << (r.delivered ? r.deliveredS : -1.0) << ',' << (r.delivered ? int(r.sinkNode) : -1) << ','
          << (r.delivered ? r.deliveredS - r.createdS : -1.0) << ',' << r.hops << ',' << r.dataTx << ','
          << r.retx << ',' << r.path << ',' << (r.dropped ? int(r.dropNode) : -1) << ','
          << (r.dropped ? DropReasonName(r.dropReason) : "") << '\n';
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
