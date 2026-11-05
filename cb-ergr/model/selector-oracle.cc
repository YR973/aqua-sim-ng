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
            .AddAttribute("EnablePower", "Also choose the transmit power level: maximise success x progress - LambdaE x Etx(level).",
                          BooleanValue(params::learner::kEnablePower), MakeBooleanAccessor(&SelectorOracle::m_enablePower),
                          MakeBooleanChecker())
            .AddAttribute("LambdaE", "Fixed energy price used when EnablePower is on (J^-1, the spec's initial price).",
                          DoubleValue(params::price::kLambdaInit), MakeDoubleAccessor(&SelectorOracle::m_lambdaE),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("TxPowerFullW", "Electrical transmit power at the full level (W).",
                          DoubleValue(params::energy::kTxPowerFullW), MakeDoubleAccessor(&SelectorOracle::m_txPowerFullW),
                          MakeDoubleChecker<double>(0.0))
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
    const int firstLevel = m_enablePower ? 0 : fullLevel;
    double bestUtility = -1e300;
    int bestLevel = fullLevel;
    for (const Candidate& c : eligible)
    {
        for (int level = firstLevel; level <= fullLevel; ++level)
        {
            const double success = m_oracle->HopSuccess(m_services.nodeId, c.id, level, pkt.dataBytes, now, !m_geoOnly);
            const double progressWeight = 1.0 - m_kappaP + m_kappaP * ((c.nb->hmin < pkt.hpkt) ? 1.0 : 0.0);
            double utility = success * progressWeight;
            if (m_enablePower)
            {
                const double txJ = m_txPowerFullW * params::phy::kTxPowerLevelScale[level] * m_services.airtimeS(pkt.dataBytes);
                utility -= m_lambdaE * txJ;
            }
            if (utility > bestUtility + 1e-12)
            {
                bestUtility = utility;
                bestLevel = level;
                best.fwdId = c.id;
                best.pMean = success;
                best.score = utility;
            }
        }
    }
    best.powerLevel = static_cast<uint8_t>(bestLevel);
    best.ackRequest = m_requestAck;
    best.numCandidates = static_cast<uint8_t>(eligible.size());
    best.chosenRank = 1;
    best.valid = true;
    return best;
}

} // namespace cbergr
} // namespace ns3
