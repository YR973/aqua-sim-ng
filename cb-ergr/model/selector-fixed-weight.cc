/*
 * selector-fixed-weight.cc: hand-set logistic weights on the CB features (kickoff §2.4 FW).
 * The weights (params::selector::kFwWeights) were chosen from the physics, not fitted: a short distance,
 * a high SNR margin, hop progress and depth advantage help; a long queue hurts; the intercept sets the
 * overall level. They are the same for every run and were never tuned on any seed.
 */

#include "selector-fixed-weight.h"

#include "cb-ergr-params.h"

#include "ns3/boolean.h"
#include "ns3/abort.h"
#include "ns3/double.h"
#include "ns3/string.h"

#include <sstream>

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(SelectorFixedWeight);
NS_OBJECT_ENSURE_REGISTERED(SelectorFittedLogistic);

TypeId
SelectorFixedWeight::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::SelectorFixedWeight")
            .SetParent<ForwarderSelector>()
            .AddConstructor<SelectorFixedWeight>()
            .AddAttribute("Weights",
                          "Nine comma-separated logistic weights; empty = the hand-set physics weights (params::selector::kFwWeights). "
                          "scripts/tune.py writes tuned weights into configs/fw-tuned.txt.",
                          StringValue(""), MakeStringAccessor(&SelectorFixedWeight::m_weightsText), MakeStringChecker())
            .AddAttribute("KappaP", "Weight of hop progress in the score.", DoubleValue(params::learner::kKappaP),
                          MakeDoubleAccessor(&SelectorFixedWeight::m_kappaP), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("AckRequest", "Request an ACK on every attempt (same feedback as CB with AckMode=Every).",
                          BooleanValue(params::selector::kRequestAckOnFirstAttempt), MakeBooleanAccessor(&SelectorFixedWeight::m_requestAck),
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
    m_w = params::selector::kFwWeights;
    if (!m_weightsText.empty())
    {
        std::stringstream ss(m_weightsText);
        int i = 0;
        for (std::string item; std::getline(ss, item, ',') && i < FeatureBuilder::kDim; ++i)
        {
            m_w[i] = std::stod(item);
        }
        NS_ABORT_MSG_IF(i != FeatureBuilder::kDim, "SelectorFixedWeight needs Weights with 9 numbers or none");
    }
}

Decision
SelectByWeights(const FeatureBuilder& features,
                const std::array<double, FeatureBuilder::kDim>& weights,
                double kappaP,
                bool requestAck,
                const PacketCtx& pkt,
                const std::vector<Candidate>& eligible,
                Time now)
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
        FeatureBuilder::Features x = features.Build(pkt, *c.nb, fullLevel, now);
        double logit = 0.0;
        for (int i = 0; i < FeatureBuilder::kDim; ++i)
        {
            logit += weights[i] * x[i];
        }
        const double pSuccess = Sigmoid(logit);
        const double progressWeight = 1.0 - kappaP + kappaP * x[params::feature::kHopProgress];
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
    best.ackRequest = requestAck;
    best.numCandidates = static_cast<uint8_t>(eligible.size());
    best.chosenRank = 1;
    best.valid = true;
    return best;
}

Decision
SelectorFixedWeight::Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now)
{
    return SelectByWeights(*m_features, m_w, m_kappaP, m_requestAck, pkt, eligible, now);
}

// ---------------------------------------------------------------- A1 ------

TypeId
SelectorFittedLogistic::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::SelectorFittedLogistic")
            .SetParent<ForwarderSelector>()
            .AddConstructor<SelectorFittedLogistic>()
            .AddAttribute("Weights", "Nine comma-separated logistic weights from scripts/fit_a1.py.", StringValue(""),
                          MakeStringAccessor(&SelectorFittedLogistic::m_weightsText), MakeStringChecker())
            .AddAttribute("KappaP", "Weight of hop progress in the score.", DoubleValue(params::learner::kKappaP),
                          MakeDoubleAccessor(&SelectorFittedLogistic::m_kappaP), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("AckRequest", "Request an ACK on every attempt.", BooleanValue(params::selector::kRequestAckOnFirstAttempt),
                          MakeBooleanAccessor(&SelectorFittedLogistic::m_requestAck), MakeBooleanChecker())
            .AddAttribute("MarginFromRssi", "Use the measured beacon SNR for the margin feature.",
                          BooleanValue(params::learner::kMarginFromRssi),
                          MakeBooleanAccessor(&SelectorFittedLogistic::m_marginFromRssi), MakeBooleanChecker());
    return tid;
}

void
SelectorFittedLogistic::SetServices(const SelectorServices& services)
{
    m_services = services;
    m_features = std::make_unique<FeatureBuilder>(services.link, m_marginFromRssi);
    // Parse "w1,...,w9" (the attribute is set before this call).
    std::stringstream ss(m_weightsText);
    int i = 0;
    for (std::string item; std::getline(ss, item, ',') && i < FeatureBuilder::kDim; ++i)
    {
        m_w[i] = std::stod(item);
    }
    NS_ABORT_MSG_IF(i != FeatureBuilder::kDim, "SelectorFittedLogistic needs Weights with 9 numbers (run scripts/fit_a1.py)");
}

Decision
SelectorFittedLogistic::Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now)
{
    return SelectByWeights(*m_features, m_w, m_kappaP, m_requestAck, pkt, eligible, now);
}

} // namespace cbergr
} // namespace ns3
