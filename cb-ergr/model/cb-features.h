/*
 * cb-features.h: the 9 features x1..x9 of one forwarding option (spec §8.1, §8.2; kickoff §2.3).
 * Shared by CB-ERGR and by every baseline that works on the same features (fixed weights, linear Q, fitted
 * logistic), so all of them see exactly the same information. Reads only what a node knows: its neighbour
 * table row, its own noisy position, and the measured SNR of the candidate's last beacon.
 */

#ifndef CB_ERGR_CB_FEATURES_H
#define CB_ERGR_CB_FEATURES_H

#include "forwarder-selector.h"
#include "small-matrix.h"

#include <memory>

namespace ns3
{
namespace cbergr
{

class FeatureBuilder
{
  public:
    static constexpr int kDim = params::feature::kBaseDim;
    using Features = Vec<kDim>;

    FeatureBuilder(std::shared_ptr<const AcousticLinkModel> link, bool marginFromRssi)
        : m_link(link),
          m_marginFromRssi(marginFromRssi)
    {
    }

    /// Spec §8.1: x1..x9, all clipped to [0, 1]. 'level' is the power level (only x8 depends on it).
    Features Build(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const;

    /// Spec §8.2: predicted SNR margin in dB.
    double PredictedMarginDb(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const;

    void SetMarginFromRssi(bool on) { m_marginFromRssi = on; }

  private:
    std::shared_ptr<const AcousticLinkModel> m_link;
    bool m_marginFromRssi;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_CB_FEATURES_H */
