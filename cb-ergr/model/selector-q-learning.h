/*
 * selector-q-learning.h: "QL" baseline, QELAR-style tabular Q-learning (spec §11 B2, review R10).
 *   Q(j) <- Q(j) + alpha * (r + gamma * V_j - Q(j)),  r = success - cost,
 * V_j is the best value that forwarder j advertised in its beacon (+2 B on every beacon, counted as overhead),
 * epsilon-greedy exploration, ACK on every attempt (AckMode = Every), full power. Initial Q is 1 plus a bonus
 * for forwarders with a lower hop count than the sender. [CONFIRM C1: definition not yet checked against the
 * group's QL-ERGR-EMHC proposal]
 */

#ifndef CB_ERGR_SELECTOR_Q_LEARNING_H
#define CB_ERGR_SELECTOR_Q_LEARNING_H

#include "forwarder-selector.h"

#include "ns3/random-variable-stream.h"

#include <map>

namespace ns3
{
namespace cbergr
{

class SelectorQLearning : public ForwarderSelector
{
  public:
    static TypeId GetTypeId();
    SelectorQLearning();
    int64_t AssignStreams(int64_t stream) override;

    // --- Selection ---
    Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) override;

    // --- Learning ---
    void OnLabel(const Decision& d, int y, LabelSource src, Time now) override;

    // --- Sharing (the advertised value rides on the beacon) ---
    void OnBeaconTx(ErgrBeaconHeader& beacon, Time now) override;

    double Q(uint16_t id) const;

  private:
    double m_alpha, m_gamma, m_epsilon, m_stepCost, m_hminBonus;
    std::map<uint16_t, double> m_q;
    double m_valueSelf = 0.0;       ///< best Q seen at the last decision: what this node advertises
    Ptr<UniformRandomVariable> m_uniform;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SELECTOR_Q_LEARNING_H */
