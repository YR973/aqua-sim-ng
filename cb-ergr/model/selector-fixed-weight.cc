/*
 * selector-fixed-weight.cc: hand-set logistic weights on the CB features (kickoff §2.4 FW).
 * The weights (params::selector::kFwWeights) were chosen from the physics, not fitted: a short distance,
 * a high SNR margin, hop progress and depth advantage help; a long queue hurts; the intercept sets the
 * overall level. They are the same for every run and were never tuned on any seed.
 */

#include "selector-fixed-weight.h"

#include "cb-ergr-params.h"

#include "ns3/boolean.h"
#include "ns3/double.h"

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(SelectorFixedWeight);

TypeId
SelectorFixedWeight::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::SelectorFixedWeight")
            .SetParent<ForwarderSelector>()
            .AddConstructor<SelectorFixedWeight>()
            .AddAttribute("KappaP", "Weight of hop progress in the score.", DoubleValue(params::learner::kKappaP),
                          MakeDoubleAccessor(&SelectorFixedWeight::m_kappaP), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("AckRequest", "Request an ACK on every attempt (same feedback as CB with AckMode=Every).",
                          BooleanValue(true), MakeBooleanAccessor(&SelectorFixedWeight::m_requestAck),
                          MakeBooleanChecker())
            .AddAttribute("MarginFromRssi", "Use the measured beacon SNR for the margin feature.",
                          BooleanValue(params::learner::kMarginFromRssi),
                          MakeBooleanAccessor(&SelectorFixedWeight::m_marginFromRssi), MakeBooleanChecker());
    return tid;
}

void
SelectorFixedWeight::SetServices(const SelectorServices& services)
{
    m_services = services;
    m_features = std::make_unique<FeatureBuilder>(services.link, m_marginFromRssi);
}

Decision
SelectorFixedWeight::Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now)
{
    Decision best;
    if (eligible.empty())
    {
        return best;
    }
    const int fullLevel = params::phy::kNumPowerLevels - 1;
    double bestScore = -1e300;
    for (const Candidate& c : eligible)
    {
        FeatureBuilder::Features x = m_features->Build(pkt, *c.nb, fullLevel, now);
        double logit = 0.0;
        for (int i = 0; i < FeatureBuilder::kDim; ++i)
        {
            logit += params::selector::kFwWeights[i] * x[i];
        }
        const double pSuccess = Sigmoid(logit);
        const double progressWeight = 1.0 - m_kappaP + m_kappaP * x[params::feature::kHopProgress];
        const double score = pSuccess * progressWeight;
        if (score > bestScore + 1e-12) // eligible is sorted by id, so ties keep the lower id
        {
            bestScore = score;
            best.fwdId = c.id;
            best.pMean = pSuccess;
            best.score = score;
            for (int i = 0; i < FeatureBuilder::kDim; ++i)
            {
                best.x[i] = x[i];
            }
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
