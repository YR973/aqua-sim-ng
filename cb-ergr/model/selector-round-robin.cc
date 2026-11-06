/*
 * selector-round-robin.cc: round robin over the eligible heads (spec §5.1, §11 B1).
 */

#include "selector-round-robin.h"

#include "cb-ergr-params.h"
#include "selector-fixed-weight.h"

#include "ns3/boolean.h"

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(SelectorRoundRobin);

TypeId
SelectorRoundRobin::GetTypeId()
{
    static TypeId tid = TypeId("ns3::cbergr::SelectorRoundRobin")
                            .SetParent<ForwarderSelector>()
                            .AddConstructor<SelectorRoundRobin>()
                            .AddAttribute("WithFeedback",
                                          "Send round-robin choices with ACK requests and retries (not the published protocol).",
                                          BooleanValue(params::selector::kRoundRobinWithFeedback),
                                          MakeBooleanAccessor(&SelectorRoundRobin::m_withFeedback),
                                          MakeBooleanChecker())
                            .AddAttribute("LowestReach", "RR-P: send at the lowest power level whose predicted margin to the chosen head is >= 0.",
                                          BooleanValue(false), MakeBooleanAccessor(&SelectorRoundRobin::m_lowestReach), MakeBooleanChecker())
                            .AddAttribute("AckRequest",
                                          "With feedback: request an ACK on first attempts (false: labels come from receipt bitmaps).",
                                          BooleanValue(params::selector::kRequestAckOnFirstAttempt),
                                          MakeBooleanAccessor(&SelectorRoundRobin::m_requestAck),
                                          MakeBooleanChecker());
    return tid;
}

void
SelectorRoundRobin::SetServices(const SelectorServices& services)
{
    if (services.link)
    {
        m_features = std::make_unique<FeatureBuilder>(services.link, params::learner::kMarginFromRssi);
    }
}

Decision
SelectorRoundRobin::Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now)
{
    Decision d;
    if (eligible.empty())
    {
        return d; // invalid decision: caller handles the void
    }
    // Candidates arrive sorted by id. Take the first id larger than the last one used; wrap to the start.
    const Candidate* chosen = &eligible.front();
    for (const Candidate& c : eligible)
    {
        if (static_cast<int32_t>(c.id) > m_lastId)
        {
            chosen = &c;
            break;
        }
    }
    m_lastId = chosen->id;

    d.fwdId = chosen->id;
    if (m_features)
    {
        // For the decision log (and for fitting A1 offline); the choice above did not use these numbers.
        FeatureBuilder::FullFeatures x = m_features->BuildFull(pkt, *chosen->nb, params::phy::kNumPowerLevels - 1, now); // x10 too (8 Nov 2026)
        for (int i = 0; i < FeatureBuilder::kFullDim; ++i)
        {
            d.x[i] = x[i];
        }
    }
    d.powerLevel = params::phy::kNumPowerLevels - 1; // full power
    if (m_lowestReach && m_features)
    {
        d.powerLevel = static_cast<uint8_t>(LowestFeasibleLevel(*m_features, pkt, *chosen->nb, now));
    }
    d.ackRequest = m_withFeedback && m_requestAck;   // no hop ACKs in the published protocol
    d.valid = true;
    return d;
}

} // namespace cbergr
} // namespace ns3
