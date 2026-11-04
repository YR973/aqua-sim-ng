/*
 * selector-fixed-weight.h: "FW" (kickoff §2.4, review R5). The same 9 features as CB-ERGR, but the logistic
 * weights are set by hand once and never learned, and there is no exploration: argmax of
 *     sigmoid(w . x) * (1 - KappaP + KappaP * x2)
 * With the same feedback as CB (ACK on every attempt), the difference between FW and CB is exactly the value
 * of learning the weights online.
 */

#ifndef CB_ERGR_SELECTOR_FIXED_WEIGHT_H
#define CB_ERGR_SELECTOR_FIXED_WEIGHT_H

#include "cb-features.h"
#include "forwarder-selector.h"

#include <array>
#include <string>
namespace ns3
{
namespace cbergr
{

/// argmax over sigmoid(w . x) * progress weight, shared by FixedWeight (FW) and FittedLogistic (A1).
Decision SelectByWeights(const FeatureBuilder& features,
                         const std::array<double, FeatureBuilder::kDim>& weights,
                         double kappaP,
                         bool requestAck,
                         const PacketCtx& pkt,
                         const std::vector<Candidate>& eligible,
                         Time now);

class SelectorFixedWeight : public ForwarderSelector
{
  public:
    static TypeId GetTypeId();
    void SetServices(const SelectorServices& services) override;

    // --- Selection ---
    Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) override;

  private:
    SelectorServices m_services;
    std::unique_ptr<FeatureBuilder> m_features;
    double m_kappaP;
    bool m_requestAck;
    bool m_marginFromRssi;
};

/**
 * "A1" (spec §11 A1, kickoff §2.4): the same rule as FW, but with weights FITTED OFFLINE by scripts/fit_a1.py
 * (L2 logistic regression on decision logs of round-robin-with-feedback runs on the training seeds only), then
 * fixed. The weights come in as the attribute Weights = "w1,...,w9".
 */
class SelectorFittedLogistic : public ForwarderSelector
{
  public:
    static TypeId GetTypeId();
    void SetServices(const SelectorServices& services) override;

    // --- Selection ---
    Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) override;

    const std::array<double, FeatureBuilder::kDim>& Weights() const { return m_w; }

  private:
    std::string m_weightsText;
    std::array<double, FeatureBuilder::kDim> m_w{};
    SelectorServices m_services;
    std::unique_ptr<FeatureBuilder> m_features;
    double m_kappaP;
    bool m_requestAck;
    bool m_marginFromRssi;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SELECTOR_FIXED_WEIGHT_H */

