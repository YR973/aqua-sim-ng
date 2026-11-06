/*
 * selector-cb-v5.h: CB-ERGR v5, the contextual-bandit forwarder selector (spec §8; kickoff §2.3).
 *
 * Per packet, a head scores every eligible (forwarder j, power level p) pair:
 *     score = pHat * (1 - KappaP + KappaP * x2) - lambdaE * Etx(p) - Mu * phi(battery_j)
 * where pHat = sigmoid(thetaTilde . x(j, p)) and thetaTilde is ONE Thompson sample from the model
 * (Laplace approximation of a discounted logistic regression). The best pair wins.
 *
 *   - Selection: BuildFeatures, Score, NeedsAck, Select (Thompson sampling + ACK gate)
 *   - Learning : OnSent (send-time evidence), OnLabel (one Newton step), energy price timer
 *   - Sharing  : OnBeaconTx / OnBeaconRx (model blob between the heads of a cell)
 *
 * The model maths lives in cb-learner-model.h; this class only wires it to routing.
 * It reads nothing but what a real node knows (G1 table, own energy, measured beacon SNR).
 */

#ifndef CB_ERGR_SELECTOR_CB_V5_H
#define CB_ERGR_SELECTOR_CB_V5_H

#include "cb-features.h"
#include "cb-learner-model.h"
#include "energy-price.h"
#include "forwarder-selector.h"

#include "ns3/event-id.h"
#include "ns3/random-variable-stream.h"

#include <map>
#include <string>
#include <vector>

namespace ns3
{
namespace cbergr
{

/// Which uncertainty the gated mode compares with its threshold (E21, 5 Nov 2026).
enum class AckGate
{
    Logit,   ///< spec §8.6: width(x) = sqrt(x^T A^-1 x) > TauW (TauW = 0.3 is the spec value)
    ProbStd, ///< ablation: std of the success probability, p (1 - p) width(x), > ProbStdThreshold
    ThompsonDisagree ///< decision-relevant: draw M extra posterior samples, ask for an ACK only if one of their argmax differs from the chosen option
};

/// When to ask for a hop ACK (spec §10, R11).
enum class AckMode
{
    Every,       ///< every attempt
    Gated,       ///< when the model is unsure, on a random audit, for priority packets and retries
    BitmapOnly   ///< only on retries; first attempts are labelled from receipt bitmaps
};

class SelectorCbV5 : public ForwarderSelector
{
  public:
    static constexpr int kDim = params::feature::kFullDim;   // 10: x1..x9 and the channel-aware x10 (zero and unused unless ChannelFeatures)
    using Features = Vec<kDim>;

    static TypeId GetTypeId();
    SelectorCbV5();

    void SetServices(const SelectorServices& services) override;
    void Start() override;
    int64_t AssignStreams(int64_t stream) override;

    // --- Selection ---
    Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) override;

    /// Spec §8.1: the 9 features of forwarding to candidate 'nb' at power level 'level', all in [0, 1].
    Features BuildFeatures(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const;

    /// Spec §8.2: predicted SNR margin (dB) of the link to 'nb' at 'level'.
    double PredictedMarginDb(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const;

    /// Spec §8.5: the score of one (forwarder, power) option given the sampled success probability.
    double Score(double pSuccess, const Features& x, double txEnergyJ, double batteryFraction) const;

    /// Spec §8.6: should this attempt ask for an ACK? 'width' is the model's uncertainty at x.
    /// 'disagree' is only used by AckGate=ThompsonDisagree (some extra posterior sample prefers another forwarder).
    bool NeedsAck(const PacketCtx& pkt, double width, double pMean = -1.0, bool disagree = false);

    // --- Learning ---
    void OnSent(const Decision& d, Time now) override;
    void OnLabel(const Decision& d, int y, LabelSource src, Time now) override;
    void Diagnostics(std::map<std::string, double>& out) const override;
    bool WantsTruthLabels() const override { return m_truthLabels; }
    void OnKnownChange(ChangeEvent e, Time now) override;

    // --- Sharing ---
    void OnRoleChange(bool isHead, Time now) override;
    bool OnHandoverOut(ErgrHandoverHeader& h, Time now) override;
    void OnHandoverIn(const ErgrHandoverHeader& h, Time now) override;
    void OnBeaconTx(ErgrBeaconHeader& beacon, Time now) override;
    void OnBeaconRx(const ErgrBeaconHeader& beacon, Time now) override;

    // --- Inspection (tests) ---
    const CbLearnerModel<kDim>& Model() const { return m_model; }
    double LambdaE() const { return m_price ? m_price->Lambda() : m_lambdaInit; }

  private:
    /// A model received in a HANDOVER frame, waiting until this node becomes head.
    struct PendingHandover
    {
        CbLearnerModel<kDim>::Snapshot snapshot;
        Time receivedAt;
        bool valid = false;
    };

    struct StoredBlob
    {
        PeerBlob<kDim> blob;
        Time receivedAt;
    };

    /// Etx(p) = TxPowerFullW * scale[p] * airtime(dataBytes), joules (spec §8.5).
    double TxEnergyJ(int level, uint32_t dataBytes) const;
    /// phi(e) = (1 / max(e, floor))^Kappa - 1 (spec §8.5).
    double BatteryPenalty(double batteryFraction) const;
    std::vector<int> EnabledLevels() const;
    void PriceTick();
    void RefreshCellPrior(Time now);

    // Attributes
    double m_gamma, m_forgetPeriodS, m_lambda0, m_explorationScale;
    double m_kappaP, m_mu, m_kappa;
    double m_tauW, m_qAudit;
    bool m_enablePower, m_enableLifetime, m_enableSharing, m_marginFromRssi;
    AckMode m_ackMode;
    AckGate m_ackGate;
    double m_probStdThreshold;
    uint64_t m_decisions = 0, m_labels = 0, m_nanDecisions = 0;
    int m_disagreeDraws;
    double m_lambdaInit, m_eta, m_targetLifetimeS, m_priceTickS;
    double m_tauSync, m_cosGate, m_matureTrace, m_betaInflate;
    double m_handoverKeepS;
    std::string m_priorWeightsText;   ///< warm start (D§3 P7): "w1,...,w9" from scripts/fit_a1.py, empty = cold start
    double m_txPowerFullW;

    SelectorServices m_services;
    std::unique_ptr<FeatureBuilder> m_features;
    CbLearnerModel<kDim> m_model;
    std::unique_ptr<EnergyPrice> m_price;
    double m_logdetAtLastSync = 0.0;
    std::map<uint16_t, StoredBlob> m_blobs;
    std::map<uint16_t, StoredBlob> m_neighbourBlobs;   ///< models heard from heads of neighbouring cells (NeighbourPrior)
    bool m_neighbourPrior;
    bool m_truthLabels = false;       ///< DIAGNOSTIC ONLY (never a method): learn from the ground-truth hop outcome
    bool m_channelFeatures = false;   ///< use x10 (needs ChannelAwareBeacons); otherwise it is 0 and the model is 9-dimensional on the wire
    int WireDim() const { return m_channelFeatures ? 10 : 9; }
    PendingHandover m_pendingHandover;
    double m_betaHandover;
    Ptr<NormalRandomVariable> m_normal;
    Ptr<UniformRandomVariable> m_uniform;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SELECTOR_CB_V5_H */
