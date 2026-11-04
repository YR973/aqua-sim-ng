/*
 * selector-oracle.cc: perfect-knowledge forwarder choice (spec §11 O and GO).
 */

#include "selector-oracle.h"

#include "cb-ergr-params.h"

#include "ns3/boolean.h"
#include "ns3/double.h"

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(SelectorOracle);

TypeId
SelectorOracle::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::SelectorOracle")
            .SetParent<ForwarderSelector>()
            .AddConstructor<SelectorOracle>()
            .AddAttribute("GeoOnly", "GO: use the true distance but not the true shadowing.", BooleanValue(false),
                          MakeBooleanAccessor(&SelectorOracle::m_geoOnly), MakeBooleanChecker())
            .AddAttribute("KappaP", "Weight of hop progress in the utility.", DoubleValue(params::learner::kKappaP),
                          MakeDoubleAccessor(&SelectorOracle::m_kappaP), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("AckRequest", "Ask for an ACK on every attempt.", BooleanValue(params::selector::kRequestAckOnFirstAttempt),
                          MakeBooleanAccessor(&SelectorOracle::m_requestAck), MakeBooleanChecker());
    return tid;
}

Decision
SelectorOracle::Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now)
{
    Decision best;
    if (eligible.empty() || !m_oracle)
    {
        return best;
    }
    const int fullLevel = params::phy::kNumPowerLevels - 1;
    double bestUtility = -1e300;
    for (const Candidate& c : eligible)
    {
        const double success = m_oracle->HopSuccess(m_services.nodeId, c.id, fullLevel, pkt.dataBytes, now, !m_geoOnly);
        const double progressWeight = 1.0 - m_kappaP + m_kappaP * ((c.nb->hmin < pkt.hpkt) ? 1.0 : 0.0);
        const double utility = success * progressWeight;
        if (utility > bestUtility + 1e-12)
        {
            bestUtility = utility;
            best.fwdId = c.id;
            best.pMean = success;
            best.score = utility;
        }
    }
    best.powerLevel = static_cast<uint8_t>(fullLevel);
    best.ackRequest = m_requestAck;
    best.numCandidates = static_cast<uint8_t>(eligible.size());
    best.chosenRank = 1;
    best.valid = true;
    return best;
}

} // namespace cbergr
} // namespace ns3
