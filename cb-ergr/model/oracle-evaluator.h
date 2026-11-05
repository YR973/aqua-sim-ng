/*
 * oracle-evaluator.h: the ONLY place that reads true positions and true shadowing (kickoff §0 rule 7).
 * It answers "how likely is this hop to succeed, given the real world?" for the oracle selectors (O and GO) and
 * for the regret measurement. Learners never receive it: only SelectorOracle overrides ForwarderSelector::SetOracle.
 *
 *   success(tx, rx, level) = 1 - PER(SNR_true),   SNR_true = SL(level) - TL(true distance) - NL + shadow(tx, rx, t)
 * Collisions and half-duplex are not part of it (spec §4.4 link model only). GO ("geo oracle") uses the true
 * distance but no shadowing.
 */

#ifndef CB_ERGR_ORACLE_EVALUATOR_H
#define CB_ERGR_ORACLE_EVALUATOR_H

#include "acoustic-link-model.h"
#include "noise-band.h"
#include "gm-shadowing.h"

#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/vector.h"

#include <functional>
#include <memory>

namespace ns3
{
namespace cbergr
{

class OracleEvaluator : public Object
{
  public:
    static TypeId GetTypeId();

    /// Wire the evaluator to the simulated world.
    void Configure(std::function<Vector(uint32_t)> truePosition,
                   Ptr<GmShadowing> shadowing,
                   std::shared_ptr<const AcousticLinkModel> link,
                   double rangeM);

    /**
     * Probability that a frame of 'bytes' bytes sent by 'tx' at power level 'level' is decodable at 'rx' at
     * time 'now'. withShadowing = false gives the geometry-only (GO) value. Zero beyond the range.
     */
    double HopSuccess(uint32_t tx, uint32_t rx, int level, uint32_t bytes, Time now, bool withShadowing);

    /// True distance between two nodes (m).
    double Distance(uint32_t a, uint32_t b);
    /// Ground truth: the channel's noise band (heterogeneous scenario), so the oracle sees what the learners cannot.
    void SetNoiseBand(const NoiseBand& band) { m_noiseBand = band; }

  private:
    std::function<Vector(uint32_t)> m_position;
    Ptr<GmShadowing> m_shadowing;
    std::shared_ptr<const AcousticLinkModel> m_link;
    double m_rangeM = 0.0;
    NoiseBand m_noiseBand;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_ORACLE_EVALUATOR_H */
