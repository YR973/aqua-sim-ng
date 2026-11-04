/*
 * selector-carp-ewma.cc: EWMA-of-success forwarder choice with bitmap feedback (spec §11 B3).
 */

#include "selector-carp-ewma.h"

#include "cb-ergr-params.h"

#include "ns3/boolean.h"
#include "ns3/double.h"

#include <cmath>

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(SelectorCarpEwma);

TypeId
SelectorCarpEwma::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::SelectorCarpEwma")
            .SetParent<ForwarderSelector>()
            .AddConstructor<SelectorCarpEwma>()
            .AddAttribute("Beta", "EWMA weight of the newest label.", DoubleValue(params::selector::kCarpBeta),
                          MakeDoubleAccessor(&SelectorCarpEwma::m_beta), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("Initial", "EWMA value of a forwarder never tried.", DoubleValue(params::selector::kCarpInitial),
                          MakeDoubleAccessor(&SelectorCarpEwma::m_initial), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("KappaP", "Weight of hop progress in the score.", DoubleValue(params::learner::kKappaP),
                          MakeDoubleAccessor(&SelectorCarpEwma::m_kappaP), MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("AckRequest", "Ask for an ACK on first attempts (CARP style: no, labels from bitmaps).",
                          BooleanValue(false), MakeBooleanAccessor(&SelectorCarpEwma::m_requestAck),
                          MakeBooleanChecker());
    return tid;
}

double
SelectorCarpEwma::Ewma(uint16_t id) const
{
    auto it = m_ewma.find(id);
    return it == m_ewma.end() ? m_initial : it->second;
}

Decision
SelectorCarpEwma::Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now)
{
    Decision best;
    if (eligible.empty())
    {
        return best;
    }
    double bestScore = -1e300;
    double bestBattery = -1.0;
    for (const Candidate& c : eligible)
    {
        const double progressWeight =
            1.0 - m_kappaP + m_kappaP * ((c.nb->hmin < pkt.hpkt) ? 1.0 : 0.0);
        const double score = Ewma(c.id) * progressWeight;
        const bool better = score > bestScore + 1e-12;
        const bool tieWithMoreEnergy = std::fabs(score - bestScore) <= 1e-12 && c.nb->battery > bestBattery;
        if (better || tieWithMoreEnergy)
        {
            bestScore = score;
            bestBattery = c.nb->battery;
            best.fwdId = c.id;
            best.pMean = Ewma(c.id);
            best.score = score;
        }
    }
    best.powerLevel = static_cast<uint8_t>(params::phy::kNumPowerLevels - 1);
    best.ackRequest = m_requestAck;
    best.numCandidates = static_cast<uint8_t>(eligible.size());
    best.chosenRank = 1;
    best.valid = true;
    return best;
}

void
SelectorCarpEwma::OnLabel(const Decision& d, int y, LabelSource src, Time now)
{
    double previous = Ewma(d.fwdId);
    m_ewma[d.fwdId] = (1.0 - m_beta) * previous + m_beta * y;
}

} // namespace cbergr
} // namespace ns3
