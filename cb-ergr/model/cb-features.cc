/*
 * cb-features.cc: feature construction x1..x9 (spec §8.1) and the predicted SNR margin (§8.2, K§2.3).
 */

#include "cb-features.h"

#include "cb-ergr-params.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace cbergr
{

namespace
{
using namespace params::feature;

double
Clip01(double v)
{
    return std::max(0.0, std::min(1.0, v));
}

double
Norm3(const Vector& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
} // namespace

double
FeatureBuilder::PredictedMarginDb(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const
{
    const double levelOffsetDb = params::phy::kSourceLevelOffsetDb[level]; // dB below the full level
    const double snrReqDb = m_link->SnrReqDb(pkt.dataBytes);
    if (m_marginFromRssi && nb.hasSnr)
    {
        // The beacon was sent at full power, so its measured SNR already contains the real path loss
        // (and recent shadowing). Assuming a symmetric link, data at 'level' sees that SNR plus the offset.
        return nb.lastSnrDb + levelOffsetDb - snrReqDb;
    }
    // Fallback: the model-based margin from the predicted distance, without shadowing (spec §8.2).
    const double distanceM = ForwarderSelector::PredictedDistance(pkt, nb, now);
    const double sourceLevelDb = m_link->FullSourceLevelDb() + levelOffsetDb;
    return m_link->MarginDb(sourceLevelDb, distanceM, pkt.dataBytes);
}

FeatureBuilder::Features
FeatureBuilder::Build(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const
{
    const double rangeM = m_link->GetConfig().rangeM;
    Features x;

    // Where the candidate is now, from its last beacon (spec §8.1).
    const Vector candidatePos = nb.PredictedPos(now);
    const double distanceM = ForwarderSelector::PredictedDistance(pkt, nb, now);
    x[kDistance] = Clip01(distanceM / rangeM);                                           // x1
    x[kHopProgress] = (nb.hmin < pkt.hpkt) ? 1.0 : 0.0;                                  // x2
    x[kDepthAdvantage] = Clip01(((pkt.myPos.z - candidatePos.z) / rangeM + 1.0) / 2.0);  // x3: shallower is better
    x[kBattery] = Clip01(nb.battery);                                                    // x4
    x[kEntryAge] = Clip01((now - nb.firstHeard).GetSeconds() / params::learner::kEntryAgeScaleS); // x5
    x[kQueue] = Clip01(nb.queueLen / params::learner::kQueueMax);                        // x6

    // x7: closing speed = -d|r|/dt, from relative position r and relative velocity v.
    const Vector r(candidatePos.x - pkt.myPos.x, candidatePos.y - pkt.myPos.y, candidatePos.z - pkt.myPos.z);
    const Vector v(nb.vel.x - pkt.myVel.x, nb.vel.y - pkt.myVel.y, nb.vel.z - pkt.myVel.z);
    const double rNorm = std::max(1.0, Norm3(r));
    const double rangeRate = (r.x * v.x + r.y * v.y + r.z * v.z) / rNorm; // d|r|/dt
    x[kClosingSpeed] = Clip01((-rangeRate / params::learner::kVMaxMps + 1.0) / 2.0);

    // x8: predicted SNR margin (the only feature that depends on the power level).
    const double marginDb = PredictedMarginDb(pkt, nb, level, now);
    x[kSnrMargin] = Clip01((marginDb + params::learner::kMarginOffsetDb) / params::learner::kMarginScaleDb);

    x[kIntercept] = 1.0;                                                                 // x9
    return x;
}

double
FeatureBuilder::NoiseCorrectionDb(const PacketCtx& pkt, const NeighbourEntry& nb) const
{
    if (!nb.hasChannel)
    {
        return 0.0;
    }
    // The beacon SNR was measured at THIS node, so it contains this node's noise; data to the candidate meets the candidate's noise.
    const bool measuredHere = m_marginFromRssi && nb.hasSnr;
    const double assumedDb = (measuredHere && pkt.hasMyNoise) ? pkt.myNoiseDb : m_link->NoiseLevelDb();
    return nb.noiseDb - assumedDb;
}

double
FeatureBuilder::NoiseCorrectedMarginFeature(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const
{
    const double marginDb = PredictedMarginDb(pkt, nb, level, now) - NoiseCorrectionDb(pkt, nb);
    return Clip01((marginDb + params::learner::kMarginOffsetDb) / params::learner::kMarginScaleDb);
}

FeatureBuilder::FullFeatures
FeatureBuilder::BuildFull(const PacketCtx& pkt, const NeighbourEntry& nb, int level, Time now) const
{
    const Features base = Build(pkt, nb, level, now);
    FullFeatures x;
    for (int i = 0; i < kDim; ++i)
    {
        x[i] = base[i];
    }
    x[kNoiseMargin] = NoiseCorrectedMarginFeature(pkt, nb, level, now);
    return x;
}

} // namespace cbergr
} // namespace ns3
