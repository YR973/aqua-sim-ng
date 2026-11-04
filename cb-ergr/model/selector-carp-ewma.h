/*
 * selector-carp-ewma.h: "CARP" baseline (spec §11 B3; review R10). Each forwarder has an exponentially
 * weighted moving average of its hop success; the score is that average times the hop-progress weight, and
 * residual energy breaks ties. Feedback follows CARP: no per-packet ACKs (retries only), labels come from
 * the receipt bitmaps on beacons, silence ends in an expiry label 0.
 */

#ifndef CB_ERGR_SELECTOR_CARP_EWMA_H
#define CB_ERGR_SELECTOR_CARP_EWMA_H

#include "forwarder-selector.h"

#include <map>

namespace ns3
{
namespace cbergr
{

class SelectorCarpEwma : public ForwarderSelector
{
  public:
    static TypeId GetTypeId();

    // --- Selection ---
    Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) override;

    // --- Learning ---
    void OnLabel(const Decision& d, int y, LabelSource src, Time now) override;

    double Ewma(uint16_t id) const;

  private:
    double m_beta;
    double m_initial;
    double m_kappaP;
    bool m_requestAck;
    std::map<uint16_t, double> m_ewma;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SELECTOR_CARP_EWMA_H */
