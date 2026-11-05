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
    static constexpr int kFullDim = params::feature::kFullDim;
    using FullFeatures = Vec<kFullDim>;

    FeatureBuilder(std::shared_ptr<const AcousticLinkModel> link, bool marginFromRssi)
        : m_link(link),
          m_marginFromRssi(marginFromRssi)
    {
    }

    /// Spec §8.1: x1..x9, all clipped to [0, 1]. 'level' is the power level (only x8 depends on it).
    Features Build(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const;

    /// x1..x11 (7 Nov 2026): the nine base features, then
    ///   x10 = noise-corrected SNR margin: x8's margin minus (the candidate's advertised noise minus the noise embedded in x8), scaled like x8;
    ///   x11 = the candidate's advertised loss rate of addressed DATA frames.
    /// Without a channel-aware advertisement from the candidate x10 = x8 and x11 = 0.
    FullFeatures BuildFull(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const;

    /// x10 alone, the scaled noise-corrected margin (FW-NA replaces x8 by it).
    double NoiseCorrectedMarginFeature(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const;

    /// The noise correction in dB: the candidate's advertised noise minus the noise assumed in the margin (own noise estimate when
    /// the margin comes from a beacon SNR measured here, else the link model's noise level). 0 without an advertisement.
    double NoiseCorrectionDb(const PacketCtx& pkt, const NeighbourEntry& nb) const;

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
