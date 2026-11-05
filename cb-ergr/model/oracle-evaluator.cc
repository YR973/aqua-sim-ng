/*
 * oracle-evaluator.cc: true-world hop success for the oracles and the regret measurement.
 */

#include "oracle-evaluator.h"

#include "cb-ergr-params.h"

#include <cmath>

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(OracleEvaluator);

TypeId
OracleEvaluator::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::OracleEvaluator").SetParent<Object>().AddConstructor<OracleEvaluator>();
    return tid;
}

void
OracleEvaluator::Configure(std::function<Vector(uint32_t)> truePosition,
                           Ptr<GmShadowing> shadowing,
                           std::shared_ptr<const AcousticLinkModel> link,
                           double rangeM)
{
    m_position = truePosition;
    m_shadowing = shadowing;
    m_link = link;
    m_rangeM = rangeM;
}

double
OracleEvaluator::Distance(uint32_t a, uint32_t b)
{
    Vector pa = m_position(a);
    Vector pb = m_position(b);
    double dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double
OracleEvaluator::HopSuccess(uint32_t tx, uint32_t rx, int level, uint32_t bytes, Time now, bool withShadowing)
{
    const double distanceM = Distance(tx, rx);
    if (distanceM > m_rangeM)
    {
        return 0.0;
    }
    const double shadowDb = (withShadowing ? m_shadowing->GetShadowDb(tx, rx, now) : 0.0) + m_noiseBand.SnrChangeDb(m_position(rx));
    const double sourceLevelDb = m_link->FullSourceLevelDb() + params::phy::kSourceLevelOffsetDb[level];
    const double snrDb = m_link->SnrDb(sourceLevelDb, distanceM, shadowDb);
    return 1.0 - m_link->PerFromSnr(snrDb, bytes);
}

} // namespace cbergr
} // namespace ns3
