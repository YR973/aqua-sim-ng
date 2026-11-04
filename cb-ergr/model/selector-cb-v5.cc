/*
 * selector-cb-v5.cc: CB-ERGR v5 (spec §8.1-§8.11, kickoff §2.3). Features, Thompson sampling, score,
 * ACK gate, send-time evidence, label update, dual energy price and model sharing between co-heads.
 */

#include "selector-cb-v5.h"

#include "cb-ergr-params.h"

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/enum.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace cbergr
{

NS_LOG_COMPONENT_DEFINE("SelectorCbV5");
NS_OBJECT_ENSURE_REGISTERED(SelectorCbV5);

namespace
{
using namespace params::feature;

// Quantisers for the beacon blob (spec §6.3).
int16_t
QuantiseTheta(double theta)
{
    double q = std::round(theta / params::sharing::kThetaScale);
    return static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, q)));
}

uint16_t
QuantiseDiag(double evidence)
{
    double q = std::round(evidence / params::sharing::kDiagScale);
    return static_cast<uint16_t>(std::max(0.0, std::min(65535.0, q))); // saturating
}
} // namespace

TypeId
SelectorCbV5::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::SelectorCbV5")
            .SetParent<ForwarderSelector>()
            .AddConstructor<SelectorCbV5>()
            .AddAttribute("Gamma", "Forgetting factor per ForgetPeriodS.", DoubleValue(params::learner::kForgetGamma),
                          MakeDoubleAccessor(&SelectorCbV5::m_gamma), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("T0", "Forgetting period (s).", DoubleValue(params::learner::kForgetPeriodS),
                          MakeDoubleAccessor(&SelectorCbV5::m_forgetPeriodS), MakeDoubleChecker<double>(1e-3))
            .AddAttribute("Lambda0", "Prior precision.", DoubleValue(params::learner::kLambda0),
                          MakeDoubleAccessor(&SelectorCbV5::m_lambda0), MakeDoubleChecker<double>(1e-6))
            .AddAttribute("ExplorationScale", "Thompson sampling scale V.", DoubleValue(params::learner::kExplorationScale),
                          MakeDoubleAccessor(&SelectorCbV5::m_explorationScale), MakeDoubleChecker<double>(0.0))
            .AddAttribute("KappaP", "Weight of hop progress in the score.", DoubleValue(params::learner::kKappaP),
                          MakeDoubleAccessor(&SelectorCbV5::m_kappaP), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("Mu", "Weight of the residual-energy penalty.", DoubleValue(params::learner::kMu),
                          MakeDoubleAccessor(&SelectorCbV5::m_mu), MakeDoubleChecker<double>(0.0))
            .AddAttribute("Kappa", "Exponent of the residual-energy penalty.", DoubleValue(params::learner::kKappa),
                          MakeDoubleAccessor(&SelectorCbV5::m_kappa), MakeDoubleChecker<double>(0.0))
            .AddAttribute("TauW", "ACK request threshold on the model width.", DoubleValue(params::learner::kAckWidthThreshold),
                          MakeDoubleAccessor(&SelectorCbV5::m_tauW), MakeDoubleChecker<double>(0.0))
            .AddAttribute("QAudit", "Probability of an audit ACK request.", DoubleValue(params::learner::kAuditRate),
                          MakeDoubleAccessor(&SelectorCbV5::m_qAudit), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("EnablePower", "Choose the transmit power level too (ablation; default off).",
                          BooleanValue(params::learner::kEnablePower),
                          MakeBooleanAccessor(&SelectorCbV5::m_enablePower), MakeBooleanChecker())
            .AddAttribute("EnableLifetime", "Use the energy price and battery penalty.",
                          BooleanValue(params::learner::kEnableLifetime),
                          MakeBooleanAccessor(&SelectorCbV5::m_enableLifetime), MakeBooleanChecker())
            .AddAttribute("EnableSharing", "Share the model between the heads of a cell.",
                          BooleanValue(params::learner::kEnableSharing),
                          MakeBooleanAccessor(&SelectorCbV5::m_enableSharing), MakeBooleanChecker())
            .AddAttribute("MarginFromRssi", "Use the measured SNR of the candidate's last beacon for the margin feature.",
                          BooleanValue(params::learner::kMarginFromRssi),
                          MakeBooleanAccessor(&SelectorCbV5::m_marginFromRssi), MakeBooleanChecker())
            .AddAttribute("AckMode", "When to request hop ACKs.", EnumValue(AckMode::Gated),
                          MakeEnumAccessor<AckMode>(&SelectorCbV5::m_ackMode),
                          MakeEnumChecker(AckMode::Every, "Every", AckMode::Gated, "Gated", AckMode::BitmapOnly,
                                          "BitmapOnly"))
            .AddAttribute("LambdaInit", "Initial energy price.", DoubleValue(params::price::kLambdaInit),
                          MakeDoubleAccessor(&SelectorCbV5::m_lambdaInit), MakeDoubleChecker<double>(0.0))
            .AddAttribute("Eta", "Step size of the energy price.", DoubleValue(params::price::kEta),
                          MakeDoubleAccessor(&SelectorCbV5::m_eta), MakeDoubleChecker<double>(0.0))
            .AddAttribute("TargetLifetimeS", "Lifetime the energy budget is spread over (s).",
                          DoubleValue(params::price::kTargetLifetimeS),
                          MakeDoubleAccessor(&SelectorCbV5::m_targetLifetimeS), MakeDoubleChecker<double>(1.0))
            .AddAttribute("PriceTickS", "Energy price update interval (s).", DoubleValue(params::timers::kPriceTickS),
                          MakeDoubleAccessor(&SelectorCbV5::m_priceTickS), MakeDoubleChecker<double>(1.0))
            .AddAttribute("TauSync", "Log-det growth that triggers sending a model blob.",
                          DoubleValue(params::sharing::kTauSync),
                          MakeDoubleAccessor(&SelectorCbV5::m_tauSync), MakeDoubleChecker<double>(0.0))
            .AddAttribute("CosGate", "Minimum cosine similarity to accept a peer's model.",
                          DoubleValue(params::sharing::kCosGate),
                          MakeDoubleAccessor(&SelectorCbV5::m_cosGate), MakeDoubleChecker<double>(-1.0, 1.0))
            .AddAttribute("MatureTrace", "A node whose trace(A) exceeds this applies the cosine gate.",
                          DoubleValue(params::sharing::kMatureTrace),
                          MakeDoubleAccessor(&SelectorCbV5::m_matureTrace), MakeDoubleChecker<double>(0.0))
            .AddAttribute("BetaInflate", "Covariance inflation factor after a head is lost.",
                          DoubleValue(params::sharing::kBetaInflate),
                          MakeDoubleAccessor(&SelectorCbV5::m_betaInflate), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("BetaHandover", "Scale applied to A when a head takes over WITHOUT a planned handover.",
                          DoubleValue(params::sharing::kBetaHandover),
                          MakeDoubleAccessor(&SelectorCbV5::m_betaHandover), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("HandoverKeepS", "How long a received handover waits for this node to become head (s).",
                          DoubleValue(params::timers::kHandoverKeepS),
                          MakeDoubleAccessor(&SelectorCbV5::m_handoverKeepS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("TxPowerFullW", "Electrical transmit power at the full level (W), for the energy term.",
                          DoubleValue(params::energy::kTxPowerFullW),
                          MakeDoubleAccessor(&SelectorCbV5::m_txPowerFullW), MakeDoubleChecker<double>(0.0));
    return tid;
}

SelectorCbV5::SelectorCbV5()
    : m_gamma(params::learner::kForgetGamma),
      m_forgetPeriodS(params::learner::kForgetPeriodS),
      m_lambda0(params::learner::kLambda0),
      m_explorationScale(params::learner::kExplorationScale),
      m_kappaP(params::learner::kKappaP),
      m_mu(params::learner::kMu),
      m_kappa(params::learner::kKappa),
      m_tauW(params::learner::kAckWidthThreshold),
      m_qAudit(params::learner::kAuditRate),
      m_enablePower(params::learner::kEnablePower),
      m_enableLifetime(params::learner::kEnableLifetime),
      m_enableSharing(params::learner::kEnableSharing),
      m_marginFromRssi(params::learner::kMarginFromRssi),
      m_ackMode(AckMode::Gated),
      m_lambdaInit(params::price::kLambdaInit),
      m_eta(params::price::kEta),
      m_targetLifetimeS(params::price::kTargetLifetimeS),
      m_priceTickS(params::timers::kPriceTickS),
      m_tauSync(params::sharing::kTauSync),
      m_cosGate(params::sharing::kCosGate),
      m_matureTrace(params::sharing::kMatureTrace),
      m_betaInflate(params::sharing::kBetaInflate),
      m_handoverKeepS(params::timers::kHandoverKeepS),
      m_betaHandover(params::sharing::kBetaHandover),
      m_txPowerFullW(params::energy::kTxPowerFullW)
{
    m_normal = CreateObject<NormalRandomVariable>();
    m_uniform = CreateObject<UniformRandomVariable>();
}

int64_t
SelectorCbV5::AssignStreams(int64_t stream)
{
    m_normal->SetStream(stream);
    m_uniform->SetStream(stream + 1);
    return 2;
}

void
SelectorCbV5::SetServices(const SelectorServices& services)
{
    m_services = services;
    m_features = std::make_unique<FeatureBuilder>(services.link, m_marginFromRssi);
    CbLearnerModel<kDim>::Config cfg;
    cfg.gamma = m_gamma;
    cfg.forgetPeriodS = m_forgetPeriodS;
    cfg.lambda0 = m_lambda0;
    cfg.explorationScale = m_explorationScale;
    m_model = CbLearnerModel<kDim>(cfg);
}

void
SelectorCbV5::Start()
{
    if (!m_enableLifetime || !m_services.residualEnergyJ)
    {
        return;
    }
    EnergyPrice::Config cfg;
    cfg.lambdaInit = m_lambdaInit;
    cfg.eta = m_eta;
    cfg.initialEnergyJ = m_services.initialEnergyJ;
    cfg.targetLifetimeS = m_targetLifetimeS;
    cfg.tickS = m_priceTickS;
    m_price = std::make_unique<EnergyPrice>(cfg, m_services.residualEnergyJ());
    Simulator::Schedule(Seconds(m_priceTickS), &SelectorCbV5::PriceTick, this);
}

// Spec §8.9: every PriceTickS the dual price follows the node's spend rate.
void
SelectorCbV5::PriceTick()
{
    m_price->Tick(m_services.residualEnergyJ());
    Simulator::Schedule(Seconds(m_priceTickS), &SelectorCbV5::PriceTick, this);
}

// ============================================================ Selection ====

std::vector<int>
SelectorCbV5::EnabledLevels() const
{
    const int full = params::phy::kNumPowerLevels - 1;
    if (!m_enablePower)
    {
        return {full}; // power arm off: always the full level
    }
    std::vector<int> levels;
    for (int p = 0; p <= full; ++p)
    {
        levels.push_back(p);
    }
    return levels;
}

double
SelectorCbV5::TxEnergyJ(int level, uint32_t dataBytes) const
{
    double airtimeS = m_services.airtimeS ? m_services.airtimeS(dataBytes) : 0.0;
    return m_txPowerFullW * params::phy::kTxPowerLevelScale[level] * airtimeS;
}

double
SelectorCbV5::BatteryPenalty(double batteryFraction) const
{
    double e = std::max(batteryFraction, params::learner::kBatteryFloor);
    return std::pow(1.0 / e, m_kappa) - 1.0;
}

double
SelectorCbV5::PredictedMarginDb(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const
{
    return m_features->PredictedMarginDb(pkt, nb, level, now);
}

SelectorCbV5::Features
SelectorCbV5::BuildFeatures(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const
{
    return m_features->Build(pkt, nb, level, now);
}

double
SelectorCbV5::Score(double pSuccess, const Features& x, double txEnergyJ, double batteryFraction) const
{
    const double lambdaE = (m_enableLifetime && m_price) ? m_price->Lambda() : 0.0;
    const double mu = m_enableLifetime ? m_mu : 0.0;
    const double progressWeight = 1.0 - m_kappaP + m_kappaP * x[kHopProgress];
    const double batteryCost = BatteryPenalty(batteryFraction);
    return pSuccess * progressWeight - lambdaE * txEnergyJ - mu * batteryCost;
}

bool
SelectorCbV5::NeedsAck(const PacketCtx& pkt, double width)
{
    if (m_ackMode == AckMode::Every)
    {
        return true;
    }
    if (pkt.attempt > 1)
    {
        return true; // retries always ask (spec §9.12), in every mode
    }
    if (m_ackMode == AckMode::BitmapOnly)
    {
        return false;
    }
    const bool priority = (pkt.hpkt > pkt.myHmin) || pkt.priority;   // spec §8.6
    const bool unsure = width > m_tauW;
    const bool audit = m_uniform->GetValue() < m_qAudit;             // unbiased sample of labels
    return unsure || priority || audit;
}

Decision
SelectorCbV5::Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now)
{
    Decision best;
    if (eligible.empty())
    {
        return best;
    }
    // 1. Forget (decision time, K§2.3) and draw ONE Thompson sample for the whole decision.
    m_model.BeginDecision(now.GetSeconds());
    Features z;
    for (int i = 0; i < kDim; ++i)
    {
        z[i] = m_normal->GetValue();
    }
    const Features thetaTilde = m_model.SampleTheta(z);
    const double batteryOwn = pkt.myBattery;
    (void)batteryOwn;

    // 2. Score every (forwarder, level) option.
    struct Option
    {
        const Candidate* cand;
        int level;
        Features x;
        double pHat;
        double pMean;
        double txEnergyJ;
        double score;
    };
    std::vector<Option> options;
    const std::vector<int> levels = EnabledLevels();
    for (const Candidate& c : eligible)
    {
        for (int level : levels)
        {
            Option o;
            o.cand = &c;
            o.level = level;
            o.x = BuildFeatures(pkt, *c.nb, level, now);
            o.pHat = Sigmoid(thetaTilde.Dot(o.x));
            o.pMean = m_model.PredictMean(o.x);
            o.txEnergyJ = TxEnergyJ(level, pkt.dataBytes);
            o.score = Score(o.pHat, o.x, o.txEnergyJ, c.nb->battery);
            options.push_back(o);
        }
    }

    // 3. Pick the best; ties go to the cheaper transmission, then to the lower id (spec §8.5).
    const Option* chosen = &options.front();
    for (const Option& o : options)
    {
        const bool better = o.score > chosen->score + 1e-12;
        const bool tie = std::fabs(o.score - chosen->score) <= 1e-12;
        const bool cheaper = o.txEnergyJ < chosen->txEnergyJ - 1e-15 ||
                             (std::fabs(o.txEnergyJ - chosen->txEnergyJ) <= 1e-15 && o.cand->id < chosen->cand->id);
        if (better || (tie && cheaper))
        {
            chosen = &o;
        }
    }

    // 4. Fill the decision (features are kept for the label update and for the logs).
    best.fwdId = chosen->cand->id;
    best.powerLevel = static_cast<uint8_t>(chosen->level);
    for (int i = 0; i < kDim; ++i)
    {
        best.x[i] = chosen->x[i];
    }
    best.pMean = chosen->pMean;
    best.width = m_model.Width(chosen->x);
    best.score = chosen->score;
    best.lambdaE = (m_enableLifetime && m_price) ? m_price->Lambda() : 0.0;
    best.ackRequest = NeedsAck(pkt, best.width);
    best.valid = true;
    // Rank of the chosen option by its mean success among the options at the chosen level (1 = best mean).
    int rank = 1;
    for (const Option& o : options)
    {
        if (o.level == chosen->level && o.pMean > chosen->pMean)
        {
            rank++;
        }
    }
    best.chosenRank = static_cast<uint8_t>(rank);
    return best;
}

// ============================================================= Learning ====

// Spec §8.7: count the attempt as evidence at send time, with the mean prediction.
void
SelectorCbV5::OnSent(const Decision& d, Time now)
{
    Features x;
    for (int i = 0; i < kDim; ++i)
    {
        x[i] = d.x[i];
    }
    m_model.AddSendEvidence(x);
}

// Spec §8.8: one Newton step on the logistic loss when the label arrives (no extra forgetting: K§2.3).
void
SelectorCbV5::OnLabel(const Decision& d, int y, LabelSource src, Time now)
{
    Features x;
    for (int i = 0; i < kDim; ++i)
    {
        x[i] = d.x[i];
    }
    m_model.UpdateWithLabel(x, y);
}

// K§2.3 / D§3 P8: widen the uncertainty only when a head is lost (a death or an unplanned loss).
void
SelectorCbV5::OnKnownChange(ChangeEvent e, Time now)
{
    if (e == ChangeEvent::NodeLost)
    {
        m_model.InflateAfterLoss(m_betaInflate);
    }
}

// ============================================================== Sharing ====

// Planned handover (spec §6.4, K§2.3): the outgoing head sends theta and the full A, unscaled.
bool
SelectorCbV5::OnHandoverOut(ErgrHandoverHeader& h, Time now)
{
    if (!m_enableSharing)
    {
        return false;
    }
    // The model includes evidence the cell has shared, so the successor gets the whole cell's knowledge.
    m_model.BeginDecision(now.GetSeconds());
    const auto snapshot = m_model.ExportSnapshot();
    if (snapshot.A.Trace() < kDim * m_lambda0 + 1e-6)
    {
        return false; // nothing learned yet
    }
    const int dim = kDim;
    int k = 0;
    for (int i = 0; i < dim; ++i)
    {
        h.theta[i] = QuantiseTheta(snapshot.theta[i]);
        h.diag[i] = QuantiseDiag(snapshot.A.At(i, i));
        for (int j = i + 1; j < dim; ++j)
        {
            double q = std::round(snapshot.A.At(i, j) / params::sharing::kDiagScale);
            h.offDiag[k++] = static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, q)));
        }
    }
    return true;
}

void
SelectorCbV5::OnHandoverIn(const ErgrHandoverHeader& h, Time now)
{
    if (!m_enableSharing)
    {
        return;
    }
    const int dim = kDim;
    CbLearnerModel<kDim>::Snapshot snap;
    int k = 0;
    for (int i = 0; i < dim; ++i)
    {
        snap.theta[i] = h.theta[i] * params::sharing::kThetaScale;
        snap.A.At(i, i) = h.diag[i] * params::sharing::kDiagScale;
        for (int j = i + 1; j < dim; ++j)
        {
            double v = h.offDiag[k++] * params::sharing::kDiagScale;
            snap.A.At(i, j) = v;
            snap.A.At(j, i) = v;
        }
    }
    m_pendingHandover.snapshot = snap;
    m_pendingHandover.receivedAt = now;
    m_pendingHandover.valid = true;
}

// A node that has just become head starts from the best knowledge it can get (K§2.3, D§3 P8):
//  - a handover it was sent (planned): adopted unscaled;
//  - otherwise, the model the cell last shared on beacons (the old head is gone, unplanned): adopted with A x 0.7.
void
SelectorCbV5::OnRoleChange(bool isHead, Time now)
{
    if (!isHead || !m_enableSharing)
    {
        return;
    }
    const bool fresh = m_pendingHandover.valid &&
                       (now - m_pendingHandover.receivedAt).GetSeconds() <= m_handoverKeepS;
    if (fresh)
    {
        m_model.ImportHandover(m_pendingHandover.snapshot, true, m_betaHandover, now.GetSeconds());
        m_pendingHandover.valid = false;
        return;
    }
    m_pendingHandover.valid = false;
    // No planned handover: use the strongest blob the cell shared (diag(A) only, so build A = lambda0 I + diag(evidence)).
    const StoredBlob* best = nullptr;
    double bestEvidence = 0.0;
    for (const auto& entry : m_blobs)
    {
        double evidence = 0.0;
        for (int i = 0; i < kDim; ++i)
        {
            evidence += entry.second.blob.diagA[i];
        }
        if (evidence > bestEvidence)
        {
            bestEvidence = evidence;
            best = &entry.second;
        }
    }
    if (!best || m_model.TraceOwn() > kDim * m_lambda0 + 1e-6)
    {
        return; // nothing shared, or this node already has its own evidence
    }
    CbLearnerModel<kDim>::Snapshot snap;
    snap.A = Mat<kDim>::ScaledIdentity(m_lambda0);
    for (int i = 0; i < kDim; ++i)
    {
        snap.theta[i] = best->blob.theta[i];
        snap.A.At(i, i) += best->blob.diagA[i];
    }
    m_model.ImportHandover(snap, false, m_betaHandover, now.GetSeconds());
}

// Spec §8.10: send the model on a beacon when it has grown enough since the last time.
void
SelectorCbV5::OnBeaconTx(ErgrBeaconHeader& beacon, Time now)
{
    if (!m_enableSharing || !(m_services.isHead && m_services.isHead()))
    {
        return;
    }
    const double logdet = m_model.LogDetOwn();
    if (logdet - m_logdetAtLastSync <= m_tauSync)
    {
        return;
    }
    const auto& theta = m_model.Theta();
    const auto evidence = m_model.OwnEvidenceDiag();
    beacon.hasBlob = true;
    for (int i = 0; i < kDim; ++i)
    {
        beacon.blob.theta[i] = QuantiseTheta(theta[i]);
        beacon.blob.diagA[i] = QuantiseDiag(evidence[i]);
    }
    m_logdetAtLastSync = logdet;
}

void
SelectorCbV5::OnBeaconRx(const ErgrBeaconHeader& beacon, Time now)
{
    if (!m_enableSharing || !beacon.hasBlob || beacon.nodeId == m_services.nodeId ||
        !m_services.cellId || beacon.cellId != m_services.cellId())
    {
        return; // only models of the same cell are shared
    }
    StoredBlob stored;
    for (int i = 0; i < kDim; ++i)
    {
        stored.blob.theta[i] = beacon.blob.theta[i] * params::sharing::kThetaScale;
        stored.blob.diagA[i] = beacon.blob.diagA[i] * params::sharing::kDiagScale;
    }
    stored.receivedAt = now;
    m_blobs[beacon.nodeId] = stored;
    RefreshCellPrior(now);
}

// Spec §8.10 + K§2.3: drop stale blobs, recompute thetaCell from the accepted ones, and let each
// accepted source contribute its evidence once (its newest message replaces the previous one).
void
SelectorCbV5::RefreshCellPrior(Time now)
{
    const double keepS = params::timers::kBlobKeepBeacons * m_services.beaconPeriodS;
    for (auto it = m_blobs.begin(); it != m_blobs.end();)
    {
        if ((now - it->second.receivedAt).GetSeconds() > keepS)
        {
            m_model.DropSource(it->first);
            it = m_blobs.erase(it);
        }
        else
        {
            ++it;
        }
    }
    std::vector<PeerBlob<kDim>> peers;
    std::vector<uint16_t> peerIds;
    for (const auto& entry : m_blobs)
    {
        if (IsPeerAccepted(m_model.Theta(), m_model.TraceOwn(), entry.second.blob, m_matureTrace, m_cosGate))
        {
            peers.push_back(entry.second.blob);
            peerIds.push_back(entry.first);
            m_model.MergeSharedEvidence(entry.first, entry.second.blob.diagA, entry.second.receivedAt.GetSeconds());
        }
        else
        {
            m_model.DropSource(entry.first);
        }
    }
    Features prior = m_model.CellPrior();
    if (ComputeCellPrior(m_model.Theta(), m_model.TraceOwn(), peers, m_matureTrace, m_cosGate, prior))
    {
        m_model.SetCellPrior(prior);
    }
}

} // namespace cbergr
} // namespace ns3
