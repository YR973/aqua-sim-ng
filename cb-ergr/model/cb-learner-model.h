/*
 * cb-learner-model.h: the CB-ERGR learner's maths, with no ns-3 dependency.
 * Implements spec §8.3-§8.8, §8.10-§8.11 and the kickoff changes K§2.3
 * (incremental sharing, planned vs unplanned handover, decision-time discounting).
 *
 * The model is a discounted Bayesian logistic regression:
 *   thetaHat  weights of the one-hop success model (D numbers)
 *   A         precision matrix (D x D); A^-1 is the uncertainty of thetaHat
 * Prediction: p = sigmoid(theta . x). Exploration: Thompson sampling from
 * N(thetaHat, V^2 A^-1) (Laplace approximation).
 *
 * "Clock" rule (K§2.3): forgetting is applied at DECISION time only, in
 * BeginDecision(). Late labels do not move the clock, so label delay cannot
 * bias how much the model forgets.
 *
 * Sharing rule (K§2.3, D§3 P8): the effective precision is
 *     A = A_own + sum over sources s of diag(E_s),
 * where E_s is source s's evidence (A_s - lambda0 * I). A new message from
 * source s REPLACES its previous contribution, so receiving the same
 * message twice cannot double count.
 */

#ifndef CB_ERGR_CB_LEARNER_MODEL_H
#define CB_ERGR_CB_LEARNER_MODEL_H

#include "cb-ergr-params.h"
#include "small-matrix.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace ns3
{
namespace cbergr
{

template <int D>
class CbLearnerModel
{
  public:
    using VecD = Vec<D>;
    using MatD = Mat<D>;

    /// Tunable settings; defaults come from cb-ergr-params.h.
    struct Config
    {
        double gamma = params::learner::kForgetGamma;          // Γ, spec §8.3
        double forgetPeriodS = params::learner::kForgetPeriodS; // T0
        double lambda0 = params::learner::kLambda0;            // prior precision
        double explorationScale = params::learner::kExplorationScale; // V, §8.4
    };

    /// Model state exchanged on head handover (spec §6.4, §8.10).
    struct Snapshot
    {
        VecD theta;
        MatD A;
    };

    explicit CbLearnerModel(const Config& cfg = Config())
        : m_cfg(cfg)
    {
        m_A = MatD::ScaledIdentity(cfg.lambda0);
        Refactor();
    }

    // --- Clock and forgetting ---

    /**
     * Spec §8.3 + K§2.3. Call once per forwarding decision, before Sample/Width.
     * Decays A and thetaHat toward the cell prior, then refreshes the factor of the
     * effective precision (own + shared evidence).
     */
    void BeginDecision(double nowS)
    {
        double elapsedS = std::max(0.0, nowS - m_lastDiscountS);
        double w = std::pow(m_cfg.gamma, elapsedS / m_cfg.forgetPeriodS); // weight kept
        MatD prior = MatD::ScaledIdentity(m_cfg.lambda0);
        m_A.Blend(w, prior, 1.0 - w);                  // A = w*A + (1-w)*lambda0*I
        for (int i = 0; i < D; ++i)
        {
            m_theta[i] = w * m_theta[i] + (1.0 - w) * m_thetaCell[i];
        }
        m_lastDiscountS = nowS;
        m_clockS = nowS;
        Refactor();
    }

    // --- Selection ---

    /// Spec §8.4: thetaTilde = thetaHat + V * L^-T z, with z ~ N(0, I) supplied by the caller.
    VecD SampleTheta(const VecD& z) const
    {
        VecD offset = BackSolve(m_L, z); // covariance of offset is (L L^T)^-1 = A^-1
        VecD sample = m_theta;
        sample.AddScaled(offset, m_cfg.explorationScale);
        return sample;
    }

    /// Spec §8.6: width = sqrt(x^T A^-1 x) = |L^-1 x|. Large width = model unsure about x.
    double Width(const VecD& x) const { return ForwardSolve(m_L, x).Norm(); }

    /// Mean predicted success probability for features x (no exploration noise).
    double PredictMean(const VecD& x) const { return Sigmoid(m_theta.Dot(x)); }

    // --- Learning ---

    /// Spec §8.7: send-time evidence, A += pBar(1-pBar) x x^T with pBar from the mean model.
    void AddSendEvidence(const VecD& x)
    {
        double pBar = PredictMean(x);
        AddOuter(m_A, x, pBar * (1.0 - pBar));
        Refactor();
    }

    /// Spec §8.8: one Newton step, thetaHat += A^-1 (y - p) x. y is 1 (received) or 0.
    void UpdateWithLabel(const VecD& x, int y)
    {
        double residual = static_cast<double>(y) - PredictMean(x);
        VecD gradient;
        for (int i = 0; i < D; ++i)
        {
            gradient[i] = residual * x[i];
        }
        VecD step = SolveSPD(m_L, gradient);
        m_theta.AddScaled(step, 1.0);
    }

    // --- Sharing ---

    /// Overwrite thetaHat (used when a new head starts from the cell prior).
    void SetTheta(const VecD& theta) { m_theta = theta; }

    /// log det of the node's OWN precision (spec §8.10: decides when a blob is worth sending).
    double LogDetOwn() const
    {
        MatD copy = m_A;
        MatD L;
        CholeskyWithFallback(copy, L, m_cfg.lambda0);
        return LogDetFromChol(L);
    }

    /// Cell prior thetaCell: the value forgetting decays toward (spec §8.3, §8.10).
    void SetCellPrior(const VecD& thetaCell) { m_thetaCell = thetaCell; }
    const VecD& CellPrior() const { return m_thetaCell; }

    /// Own evidence on the diagonal, diag(A_own - lambda0 I): what goes in the beacon blob.
    VecD OwnEvidenceDiag() const
    {
        VecD diag;
        for (int i = 0; i < D; ++i)
        {
            diag[i] = std::max(0.0, m_A.At(i, i) - m_cfg.lambda0);
        }
        return diag;
    }

    /**
     * K§2.3 incremental sharing. Store source 'srcId's evidence, REPLACING its previous
     * contribution (so repeated messages never double count). Evidence ages with the same
     * forgetting as own evidence.
     */
    void MergeSharedEvidence(uint32_t srcId, const VecD& evidenceDiag, double receivedS)
    {
        m_sources[srcId] = SourceEvidence{evidenceDiag, receivedS};
        Refactor();
    }

    /// Remove a source (it left the cell or died).
    void DropSource(uint32_t srcId)
    {
        m_sources.erase(srcId);
        Refactor();
    }

    // --- Handover and loss ---

    /// Effective (own + shared) state, handed to the next head on a planned handover.
    Snapshot ExportSnapshot() const { return Snapshot{m_theta, EffectivePrecision()}; }

    /**
     * Take over a cell's model. K§2.3: a PLANNED handover keeps A unscaled; an UNPLANNED
     * takeover (old head died) scales A by betaHandover because the data is stale.
     */
    void ImportHandover(const Snapshot& s, bool planned, double betaHandover, double nowS)
    {
        m_theta = s.theta;
        m_A = s.A;
        if (!planned)
        {
            m_A.Blend(betaHandover, MatD(), 0.0);
        }
        m_sources.clear(); // already inside s.A
        m_lastDiscountS = nowS;
        m_clockS = nowS;
        Refactor();
    }

    /// Spec §8.11 + D§3 P8: widen uncertainty after a death / unplanned head loss only.
    void InflateAfterLoss(double betaInflate)
    {
        MatD prior = MatD::ScaledIdentity(m_cfg.lambda0);
        m_A.Blend(betaInflate, prior, 1.0 - betaInflate);
        Refactor();
    }

    // --- Inspection (tests, logging, beacon rule) ---

    const VecD& Theta() const { return m_theta; }
    const MatD& OwnPrecision() const { return m_A; }
    double LogDetEffective() const { return LogDetFromChol(m_L); }
    double TraceOwn() const { return m_A.Trace(); }
    int NumResets() const { return m_resets; }

    /// Effective precision A_own + sum of aged shared diagonals, at the current clock.
    MatD EffectivePrecision() const
    {
        MatD eff = m_A;
        for (const auto& entry : m_sources)
        {
            const SourceEvidence& src = entry.second;
            double elapsedS = std::max(0.0, m_clockS - src.receivedS);
            double w = std::pow(m_cfg.gamma, elapsedS / m_cfg.forgetPeriodS);
            for (int i = 0; i < D; ++i)
            {
                eff.At(i, i) += w * src.evidenceDiag[i];
            }
        }
        return eff;
    }

  private:
    struct SourceEvidence
    {
        VecD evidenceDiag;
        double receivedS;
    };

    /// Recompute the Cholesky factor of the effective precision. Spec §8.12 fallback inside.
    void Refactor()
    {
        MatD eff = EffectivePrecision();
        CholeskyStatus status = CholeskyWithFallback(eff, m_L, m_cfg.lambda0);
        if (status == CholeskyStatus::Reset)
        {
            // Numerical trouble: forget everything learned about uncertainty.
            ++m_resets;
            m_A = MatD::ScaledIdentity(m_cfg.lambda0);
            m_sources.clear();
        }
    }

    Config m_cfg;
    VecD m_theta;            // thetaHat
    VecD m_thetaCell;        // cell prior (zeros until shared)
    MatD m_A;                // own precision, includes the lambda0 * I prior
    MatD m_L;                // Cholesky factor of the effective precision
    std::map<uint32_t, SourceEvidence> m_sources;
    double m_lastDiscountS = 0.0;
    double m_clockS = 0.0;
    int m_resets = 0;
};

/// One same-cell peer's shared model, as received on a beacon (spec §8.10).
template <int D>
struct PeerBlob
{
    Vec<D> theta;
    Vec<D> diagA;
};

/**
 * Spec §8.10: is a peer's model accepted? Yes if this node is still immature (trace(A_self) < matureTrace)
 * or the peer's theta points the same way as ours (cosine > cosGate).
 */
template <int D>
bool
IsPeerAccepted(const Vec<D>& thetaSelf,
               double traceSelf,
               const PeerBlob<D>& peer,
               double matureTrace,
               double cosGate)
{
    double normSelf = thetaSelf.Norm();
    double normPeer = peer.theta.Norm();
    double cosine = 0.0;
    if (normSelf > 0.0 && normPeer > 0.0)
    {
        cosine = thetaSelf.Dot(peer.theta) / (normSelf * normPeer);
    }
    return (traceSelf < matureTrace) || (cosine > cosGate);
}

/**
 * Spec §8.10: combine accepted peer blobs into thetaCell, an average of the accepted thetas weighted by
 * s_k = sum(diagA_k). Returns false (and leaves 'out' alone) if nobody is accepted.
 */
template <int D>
bool
ComputeCellPrior(const Vec<D>& thetaSelf,
                 double traceSelf,
                 const std::vector<PeerBlob<D>>& peers,
                 double matureTrace,
                 double cosGate,
                 Vec<D>& out)
{
    Vec<D> weighted;
    double weightSum = 0.0;
    for (const PeerBlob<D>& peer : peers)
    {
        if (!IsPeerAccepted(thetaSelf, traceSelf, peer, matureTrace, cosGate))
        {
            continue;
        }
        double s = 0.0;
        for (int i = 0; i < D; ++i)
        {
            s += peer.diagA[i];
        }
        weighted.AddScaled(peer.theta, s);
        weightSum += s;
    }
    if (weightSum <= 0.0)
    {
        return false;
    }
    for (int i = 0; i < D; ++i)
    {
        out[i] = weighted[i] / weightSum;
    }
    return true;
}

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_CB_LEARNER_MODEL_H */
