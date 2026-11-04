/*
 * selector-lfa-q.h: "LinQ" baseline (spec §11 B4): linear Q(x) = w . x on the same 9 features, power fixed at
 * full. Semi-gradient TD(0): target = r + gamma * V_j where V_j is the best linear value that forwarder j
 * advertised on its beacon; epsilon-greedy. Tests the value of bootstrapping on the same features.
 */

#ifndef CB_ERGR_SELECTOR_LFA_Q_H
#define CB_ERGR_SELECTOR_LFA_Q_H

#include "cb-features.h"
#include "forwarder-selector.h"

#include "ns3/random-variable-stream.h"

namespace ns3
{
namespace cbergr
{

class SelectorLfaQ : public ForwarderSelector
{
  public:
    static TypeId GetTypeId();
    SelectorLfaQ();
    void SetServices(const SelectorServices& services) override;
    int64_t AssignStreams(int64_t stream) override;

    // --- Selection ---
    Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) override;

    // --- Learning ---
    void OnLabel(const Decision& d, int y, LabelSource src, Time now) override;

    // --- Sharing ---
    void OnBeaconTx(ErgrBeaconHeader& beacon, Time now) override;

    const Vec<9>& Weights() const { return m_w; }

  private:
    double m_step, m_gamma, m_epsilon, m_stepCost;
    bool m_marginFromRssi;
    Vec<9> m_w;
    double m_valueSelf = 0.0;
    SelectorServices m_services;
    std::unique_ptr<FeatureBuilder> m_features;
    Ptr<UniformRandomVariable> m_uniform;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SELECTOR_LFA_Q_H */
