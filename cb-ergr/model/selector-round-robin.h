/*
 * selector-round-robin.h: B1 / "RR", ERGR-EMHC as published (spec §5.1, §11 B1).
 * Cycles through the eligible heads in id order, always at full power, with no hop ACKs and no learning.
 */

#ifndef CB_ERGR_SELECTOR_ROUND_ROBIN_H
#define CB_ERGR_SELECTOR_ROUND_ROBIN_H

#include "forwarder-selector.h"

namespace ns3
{
namespace cbergr
{

class SelectorRoundRobin : public ForwarderSelector
{
  public:
    static TypeId GetTypeId();

    // --- Selection ---
    Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) override;

    // --- Learning ---
    bool UsesFeedback() const override { return false; }

  private:
    int32_t m_lastId = -1; ///< id chosen last time; the next choice is the next larger id (wrapping)
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SELECTOR_ROUND_ROBIN_H */
