/*
 * selector-qelar.cc: see selector-qelar.h.
 */

#include "selector-qelar.h"

#include "cb-ergr-params.h"

#include "ns3/double.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(SelectorQelar);

TypeId
SelectorQelar::GetTypeId()
{
    using namespace params::selector;
    static TypeId tid =
        TypeId("ns3::cbergr::SelectorQelar")
            .SetParent<ForwarderSelector>()
            .AddConstructor<SelectorQelar>()
            .AddAttribute("G", "Constant cost of a transmission.", DoubleValue(kQelarG), MakeDoubleAccessor(&SelectorQelar::m_g), MakeDoubleChecker<double>())
            .AddAttribute("Alpha1", "Weight of the residual-energy cost, success.", DoubleValue(kQelarAlpha1), MakeDoubleAccessor(&SelectorQelar::m_a1), MakeDoubleChecker<double>())
            .AddAttribute("Alpha2", "Weight of the energy-distribution term, success.", DoubleValue(kQelarAlpha2), MakeDoubleAccessor(&SelectorQelar::m_a2), MakeDoubleChecker<double>())
            .AddAttribute("Beta1", "Weight of the residual-energy cost, failure.", DoubleValue(kQelarBeta1), MakeDoubleAccessor(&SelectorQelar::m_b1), MakeDoubleChecker<double>())
            .AddAttribute("Beta2", "Weight of the energy-distribution term, failure.", DoubleValue(kQelarBeta2), MakeDoubleAccessor(&SelectorQelar::m_b2), MakeDoubleChecker<double>())
            .AddAttribute("Gamma", "Discount.", DoubleValue(kQelarGamma), MakeDoubleAccessor(&SelectorQelar::m_gamma), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("PriorSuccess", "Beta prior of P (successes).", DoubleValue(kQelarPriorSuccess), MakeDoubleAccessor(&SelectorQelar::m_priorS), MakeDoubleChecker<double>(0.0))
            .AddAttribute("PriorFailure", "Beta prior of P (failures).", DoubleValue(kQelarPriorFailure), MakeDoubleAccessor(&SelectorQelar::m_priorF), MakeDoubleChecker<double>(0.0));
    return tid;
}

SelectorQelar::SelectorQelar()
    : m_g(params::selector::kQelarG),
      m_a1(params::selector::kQelarAlpha1),
      m_a2(params::selector::kQelarAlpha2),
      m_b1(params::selector::kQelarBeta1),
      m_b2(params::selector::kQelarBeta2),
      m_gamma(params::selector::kQelarGamma),
      m_priorS(params::selector::kQelarPriorSuccess),
      m_priorF(params::selector::kQelarPriorFailure)
{
}

double
SelectorQelar::SuccessEstimate(uint16_t id) const
{
    auto it = m_counts.find(id);
    const double s = it == m_counts.end() ? 0.0 : it->second.s;
    const double f = it == m_counts.end() ? 0.0 : it->second.f;
    return (s + m_priorS) / (s + f + m_priorS + m_priorF);
}

double
SelectorQelar::ValueOf(const NeighbourEntry& nb) const
{
    if (nb.isSink)
    {
        return 0.0; // terminal
    }
    auto it = m_ackV.find(nb.id);
    const bool ack = it != m_ackV.end();
    if (nb.hasQ && (!ack || nb.beaconTime >= it->second.at))
    {
        return nb.advertisedQ;
    }
    return ack ? it->second.v : 0.0; // never heard: 0, as the paper initialises V
}

double
SelectorQelar::QValue(const PacketCtx& pkt, const NeighbourEntry& nb, double groupMeanJ, Time now) const
{
    const double e0 = std::max(1e-9, m_services.initialEnergyJ);
    const double ei = pkt.myEnergyJ;
    const double ej = nb.battery * e0;
    const double ci = 1.0 - ei / e0;
    const double cj = 1.0 - ej / e0;
    const double di = 2.0 / M_PI * std::atan(ei - groupMeanJ);
    const double dj = 2.0 / M_PI * std::atan(ej - groupMeanJ);
    const double rs = -m_g - m_a1 * (ci + cj) + m_a2 * (di + dj);
    const double rf = -m_g - m_b1 * ci + m_b2 * di;
    const double p = SuccessEstimate(nb.id);
    return p * (rs + m_gamma * ValueOf(nb)) + (1.0 - p) * (rf + m_gamma * m_vSelf);
}

Decision
SelectorQelar::Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now)
{
    Decision d;
    if (eligible.empty())
    {
        return d;
    }
    // The node's group: itself and its eligible neighbours (residual energies from their beacons).
    const double e0 = m_services.initialEnergyJ;
    double sum = pkt.myEnergyJ;
    for (const Candidate& c : eligible)
    {
        sum += c.nb->battery * e0;
    }
    const double groupMean = sum / (eligible.size() + 1.0);
    const Candidate* best = nullptr;
    double bestQ = -1e300;
    for (const Candidate& c : eligible)
    {
        const double q = QValue(pkt, *c.nb, groupMean, now);
        if (!best || q > bestQ + 1e-12)
        {
            best = &c;
            bestQ = q;
        }
    }
    m_vSelf = bestQ;
    d.fwdId = best->id;
    d.powerLevel = static_cast<uint8_t>(params::phy::kNumPowerLevels - 1);
    d.ackRequest = true;
    d.pMean = SuccessEstimate(best->id);
    d.score = bestQ;
    d.aux = ValueOf(*best->nb);
    d.numCandidates = static_cast<uint8_t>(eligible.size());
    d.chosenRank = 1;
    d.valid = true;
    return d;
}

void
SelectorQelar::OnLabel(const Decision& d, int y, LabelSource src, Time now)
{
    Counts& c = m_counts[d.fwdId];
    (y ? c.s : c.f) += 1.0;
    m_labels++;
}

void
SelectorQelar::OnBeaconTx(ErgrBeaconHeader& beacon, Time now)
{
    if (m_services.isHead && !m_services.isHead())
    {
        return; // only heads are candidates, so only head beacons carry V
    }
    beacon.hasQ = true;
    beacon.qValue = static_cast<int16_t>(std::round(std::max(-30.0, std::min(30.0, m_vSelf)) / params::selector::kQlValueScale));
}

bool
SelectorQelar::AckValue(double& v) const
{
    v = m_vSelf;
    return true;
}

void
SelectorQelar::OnAckValue(uint16_t ackerId, double v, Time now)
{
    m_ackV[ackerId] = AckV{v, now};
}

void
SelectorQelar::Diagnostics(std::map<std::string, double>& out) const
{
    if (m_labels == 0)
    {
        return;
    }
    out["theta_sum_0"] += m_vSelf; // theta_trace.csv column theta1 = V of the head
    double pSum = 0.0;
    for (const auto& e : m_counts)
    {
        pSum += SuccessEstimate(e.first);
    }
    out["theta_sum_1"] += m_counts.empty() ? 0.0 : pSum / m_counts.size(); // theta2 = mean estimated P
    out["theta_heads"] += 1.0;
}

} // namespace cbergr
} // namespace ns3
