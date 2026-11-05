/*
 * selector-lfa-q.cc: linear function-approximation Q-learning on the CB features (spec §11 B4).
 */

#include "selector-lfa-q.h"

#include "cb-ergr-params.h"

#include "ns3/boolean.h"
#include "ns3/double.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(SelectorLfaQ);

TypeId
SelectorLfaQ::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::SelectorLfaQ")
            .SetParent<ForwarderSelector>()
            .AddConstructor<SelectorLfaQ>()
            .AddAttribute("Step", "TD step size.", DoubleValue(params::selector::kLfaStep),
                          MakeDoubleAccessor(&SelectorLfaQ::m_step), MakeDoubleChecker<double>(0.0))
            .AddAttribute("Gamma", "Discount of the next-hop value.", DoubleValue(params::selector::kLfaGamma),
                          MakeDoubleAccessor(&SelectorLfaQ::m_gamma), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("Epsilon", "Probability of a random forwarder.", DoubleValue(params::selector::kLfaEpsilon),
                          MakeDoubleAccessor(&SelectorLfaQ::m_epsilon), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("StepCost", "Cost subtracted from the 0/1 reward.", DoubleValue(params::selector::kQlStepCost),
                          MakeDoubleAccessor(&SelectorLfaQ::m_stepCost), MakeDoubleChecker<double>(0.0))
            .AddAttribute("Legacy", "Old target (y - StepCost) + Gamma * V_j (LinQ legacy).",
                          BooleanValue(false), MakeBooleanAccessor(&SelectorLfaQ::m_legacy), MakeBooleanChecker())
            .AddAttribute("MarginFromRssi", "Use the measured beacon SNR for the margin feature.",
                          BooleanValue(params::learner::kMarginFromRssi),
                          MakeBooleanAccessor(&SelectorLfaQ::m_marginFromRssi), MakeBooleanChecker());
    return tid;
}

SelectorLfaQ::SelectorLfaQ()
    : m_step(params::selector::kLfaStep),
      m_gamma(params::selector::kLfaGamma),
      m_epsilon(params::selector::kLfaEpsilon),
      m_stepCost(params::selector::kQlStepCost),
      m_marginFromRssi(params::learner::kMarginFromRssi)
{
    m_uniform = CreateObject<UniformRandomVariable>();
}

int64_t
SelectorLfaQ::AssignStreams(int64_t stream)
{
    m_uniform->SetStream(stream);
    return 1;
}

void
SelectorLfaQ::SetServices(const SelectorServices& services)
{
    m_services = services;
    m_features = std::make_unique<FeatureBuilder>(services.link, m_marginFromRssi);
}

Decision
SelectorLfaQ::Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now)
{
    Decision d;
    if (eligible.empty())
    {
        return d;
    }
    const int fullLevel = params::phy::kNumPowerLevels - 1;
    std::vector<FeatureBuilder::Features> xs;
    std::vector<double> qs;
    size_t bestIndex = 0;
    for (size_t i = 0; i < eligible.size(); ++i)
    {
        xs.push_back(m_features->Build(pkt, *eligible[i].nb, fullLevel, now));
        qs.push_back(m_w.Dot(xs.back()));
        if (qs.back() > qs[bestIndex] + 1e-12)
        {
            bestIndex = i;
        }
    }
    m_valueSelf = qs[bestIndex]; // advertised on the next beacon
    size_t chosenIndex = bestIndex;
    if (m_uniform->GetValue() < m_epsilon)
    {
        chosenIndex = m_uniform->GetInteger(0, eligible.size() - 1);
    }
    const Candidate& c = eligible[chosenIndex];
    d.fwdId = c.id;
    d.powerLevel = static_cast<uint8_t>(fullLevel);
    d.ackRequest = true;
    for (int i = 0; i < FeatureBuilder::kDim; ++i)
    {
        d.x[i] = xs[chosenIndex][i];
    }
    d.score = qs[chosenIndex];
    d.pMean = std::max(0.0, std::min(1.0, qs[chosenIndex]));
    if (m_legacy)
    {
        d.aux = (c.nb->isSink || !c.nb->hasQ) ? 0.0 : c.nb->advertisedQ;
    }
    else
    {
        d.aux = c.nb->isSink ? 1.0 : (c.nb->hasQ ? m_gamma * c.nb->advertisedQ : 0.0); // value of a successful hop
    }
    d.numCandidates = static_cast<uint8_t>(eligible.size());
    d.chosenRank = 1;
    d.valid = true;
    return d;
}

// Semi-gradient TD(0): w += step * (target - w.x) * x.
void
SelectorLfaQ::OnLabel(const Decision& d, int y, LabelSource src, Time now)
{
    Vec<9> x;
    for (int i = 0; i < 9; ++i)
    {
        x[i] = d.x[i];
    }
    const double target = m_legacy ? (static_cast<double>(y) - m_stepCost) + m_gamma * d.aux
                                   : (y ? d.aux : m_gamma * m_valueSelf); // failure: stays at i, bootstrap from V_i
    const double error = target - m_w.Dot(x);
    m_w.AddScaled(x, m_step * error);
}

void
SelectorLfaQ::OnBeaconTx(ErgrBeaconHeader& beacon, Time now)
{
    beacon.hasQ = true;
    double q = std::max(-30.0, std::min(30.0, m_valueSelf));
    beacon.qValue = static_cast<int16_t>(std::round(q / params::selector::kQlValueScale));
}

} // namespace cbergr
} // namespace ns3
