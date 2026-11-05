/*
 * selector-q-learning.cc: QELAR-style Q-learning forwarder choice (spec §11 B2).
 */

#include "selector-q-learning.h"

#include "cb-ergr-params.h"

#include "ns3/boolean.h"
#include "ns3/double.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(SelectorQLearning);

TypeId
SelectorQLearning::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::SelectorQLearning")
            .SetParent<ForwarderSelector>()
            .AddConstructor<SelectorQLearning>()
            .AddAttribute("Alpha", "Learning rate.", DoubleValue(params::selector::kQlAlpha),
                          MakeDoubleAccessor(&SelectorQLearning::m_alpha), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("Gamma", "Discount of the next-hop value.", DoubleValue(params::selector::kQlGamma),
                          MakeDoubleAccessor(&SelectorQLearning::m_gamma), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("Epsilon", "Probability of a random forwarder.", DoubleValue(params::selector::kQlEpsilon),
                          MakeDoubleAccessor(&SelectorQLearning::m_epsilon), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("StepCost", "Cost subtracted from the 0/1 reward.", DoubleValue(params::selector::kQlStepCost),
                          MakeDoubleAccessor(&SelectorQLearning::m_stepCost), MakeDoubleChecker<double>(0.0))
            .AddAttribute("Legacy", "Old target (y - StepCost) + Gamma * V_j with initial Q 1 + HminBonus*dHmin (QlLegacy).",
                          BooleanValue(false), MakeBooleanAccessor(&SelectorQLearning::m_legacy), MakeBooleanChecker())
            .AddAttribute("HminBonus", "Initial Q bonus per hop of Hmin advantage.",
                          DoubleValue(params::selector::kQlHminBonus),
                          MakeDoubleAccessor(&SelectorQLearning::m_hminBonus), MakeDoubleChecker<double>(0.0));
    return tid;
}

SelectorQLearning::SelectorQLearning()
    : m_alpha(params::selector::kQlAlpha),
      m_gamma(params::selector::kQlGamma),
      m_epsilon(params::selector::kQlEpsilon),
      m_stepCost(params::selector::kQlStepCost),
      m_hminBonus(params::selector::kQlHminBonus)
{
    m_uniform = CreateObject<UniformRandomVariable>();
}

int64_t
SelectorQLearning::AssignStreams(int64_t stream)
{
    m_uniform->SetStream(stream);
    return 1;
}

double
SelectorQLearning::Q(uint16_t id) const
{
    auto it = m_q.find(id);
    return it == m_q.end() ? (m_legacy ? 1.0 : m_gamma) : it->second;
}

Decision
SelectorQLearning::Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now)
{
    Decision d;
    if (eligible.empty())
    {
        return d;
    }
    // Value of each option: the learned Q, or the initial guess (1 + bonus for a lower hop count).
    auto value = [&](const Candidate& c) {
        auto it = m_q.find(c.id);
        if (it != m_q.end())
        {
            return it->second;
        }
        if (m_legacy)
        {
            return 1.0 + m_hminBonus * (static_cast<double>(pkt.myHmin) - static_cast<double>(c.nb->hmin));
        }
        return std::pow(m_gamma, static_cast<double>(c.nb->hmin)); // optimistic start: discounted hops to the sink
    };
    const Candidate* chosen = &eligible.front();
    double chosenValue = value(*chosen);
    double bestValue = chosenValue;
    for (const Candidate& c : eligible)
    {
        const double v = value(c);
        bestValue = std::max(bestValue, v);
        if (v > chosenValue + 1e-12)
        {
            chosen = &c;
            chosenValue = v;
        }
    }
    if (m_uniform->GetValue() < m_epsilon)
    {
        chosen = &eligible[m_uniform->GetInteger(0, eligible.size() - 1)];
        chosenValue = value(*chosen);
    }
    m_valueSelf = bestValue; // what this node tells its neighbours its position is worth

    d.fwdId = chosen->id;
    d.powerLevel = static_cast<uint8_t>(params::phy::kNumPowerLevels - 1);
    d.ackRequest = true; // AckMode = Every
    d.pMean = std::max(0.0, std::min(1.0, chosenValue));
    d.score = chosenValue;
    if (m_legacy)
    {
        // Next-state value: a sink is terminal (0); otherwise what the forwarder advertised (0 if it never did).
        d.aux = (chosen->nb->isSink || !chosen->nb->hasQ) ? 0.0 : chosen->nb->advertisedQ;
    }
    else
    {
        // Value of a successful hop: 1 at a sink, else gamma * V_j (what j advertised, or the optimistic gamma^hmin_j).
        const double vj = chosen->nb->hasQ ? chosen->nb->advertisedQ : std::pow(m_gamma, static_cast<double>(chosen->nb->hmin));
        d.aux = chosen->nb->isSink ? 1.0 : m_gamma * vj;
    }
    d.numCandidates = static_cast<uint8_t>(eligible.size());
    d.chosenRank = 1;
    d.valid = true;
    return d;
}

void
SelectorQLearning::OnLabel(const Decision& d, int y, LabelSource src, Time now)
{
    double target;
    if (m_legacy)
    {
        target = (static_cast<double>(y) - m_stepCost) + m_gamma * d.aux;
    }
    else
    {
        // success: the value of arriving at j (d.aux); failure: the packet is still at i, so bootstrap from V_i.
        target = y ? d.aux : m_gamma * m_valueSelf;
    }
    const double previous = m_q.count(d.fwdId) ? m_q[d.fwdId] : d.score; // start from the value used at decision time
    m_q[d.fwdId] = previous + m_alpha * (target - previous);
}

void
SelectorQLearning::OnBeaconTx(ErgrBeaconHeader& beacon, Time now)
{
    beacon.hasQ = true;
    double q = std::max(-30.0, std::min(30.0, m_valueSelf));
    beacon.qValue = static_cast<int16_t>(std::round(q / params::selector::kQlValueScale));
}

} // namespace cbergr
} // namespace ns3
