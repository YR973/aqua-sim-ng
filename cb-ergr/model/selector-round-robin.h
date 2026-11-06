/*
 * selector-round-robin.h: B1 / "RR", ERGR-EMHC as published (spec §5.1, §11 B1).
 * Cycles through the eligible heads in id order, always at full power, with no hop ACKs and no learning.
 */

#ifndef CB_ERGR_SELECTOR_ROUND_ROBIN_H
#define CB_ERGR_SELECTOR_ROUND_ROBIN_H

#include "cb-features.h"
#include "forwarder-selector.h"

namespace ns3
{
namespace cbergr
{

class SelectorRoundRobin : public ForwarderSelector
{
  public:
    static TypeId GetTypeId();

    void SetServices(const SelectorServices& services) override;

    // --- Selection ---
    Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) override;

    // --- Learning ---
    /// False for the published protocol (no hop ACKs). With WithFeedback the same round-robin choice is
    /// sent with an ACK request on every attempt and retried (isolates the value of retransmission from learning).
    bool UsesFeedback() const override { return m_withFeedback; }

  private:
    /// Features of the chosen option are computed for the decision log only; they do not influence the choice.
    std::unique_ptr<FeatureBuilder> m_features;
    bool m_withFeedback = false;
    bool m_lowestReach = false; ///< RR-P (9 Nov 2026): send at the lowest level whose predicted margin to the chosen head is >= 0
    bool m_requestAck = true;   ///< with feedback: ask for an ACK on first attempts (false = bitmap labels only)
    int32_t m_lastId = -1; ///< id chosen last time; the next choice is the next larger id (wrapping)
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SELECTOR_ROUND_ROBIN_H */
