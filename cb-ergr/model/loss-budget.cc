/*
 * loss-budget.cc: see loss-budget.h.
 */

#include "loss-budget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ns3
{
namespace cbergr
{

namespace
{
constexpr double kSoundSpeedMps = 1500.0;
constexpr double kKeepS = 40.0; // arrivals older than this are forgotten
} // namespace

void
LossBudget::Configure(double bitRateBps, double preambleS, double warmupS, double endS)
{
    m_bitRateBps = bitRateBps;
    m_preambleS = preambleS;
    m_warmupS = warmupS;
    m_endS = endS;
}

void
LossBudget::OnFrameTx(uint32_t node, FrameType type, uint32_t bytes, int32_t receiver, double nowS)
{
    if (!Enabled() || nowS < m_warmupS)
    {
        return;
    }
    m_airtimeS[static_cast<int>(type)] += Airtime(bytes);
    if (type != FrameType::Data || receiver < 0)
    {
        return;
    }
    Attempt a;
    a.key = m_lastKey[node];
    a.sender = node;
    a.fwd = static_cast<uint32_t>(receiver);
    a.txS = nowS;
    m_attempts.push_back(a);
    const size_t idx = m_attempts.size() - 1;
    m_pending.push_back(idx);
    m_byKey[std::make_tuple(a.key.source, a.key.seq, node, a.fwd)].push_back(idx);
    m_lastAttemptOfPacket[std::make_tuple(a.key.source, a.key.seq)] = idx;
}

void
LossBudget::OnLinkEval(uint32_t txNode, uint32_t rxNode, FrameType type, uint32_t bytes, double distM, bool decodable, double nowS,
                       uint64_t dataId)
{
    if (!Enabled())
    {
        return;
    }
    const double air = Airtime(bytes);
    // New transmission by txNode? (all receivers of one frame are evaluated at the same instant)
    auto last = m_lastAirStart.find(txNode);
    const bool fresh = (last == m_lastAirStart.end() || last->second != nowS);
    if (fresh)
    {
        ClassifyReady(nowS);
        m_lastAirStart[txNode] = nowS;
        auto& air_q = m_air[txNode];
        air_q.emplace_back(nowS, nowS + air);
        while (!air_q.empty() && air_q.front().second < nowS - kKeepS)
        {
            air_q.pop_front();
        }
        if (type == FrameType::Data)
        {
            const uint16_t source = static_cast<uint16_t>(dataId >> 32);
            const uint16_t seq = static_cast<uint16_t>(dataId >> 16);
            const uint32_t fwd = static_cast<uint32_t>(dataId & 0xffff);
            auto list = m_byKey.find(std::make_tuple(source, seq, txNode, fwd));
            if (list != m_byKey.end())
            {
                for (size_t idx : list->second) // the oldest attempt with this identity that is not yet on the air
                {
                    if (!m_attempts[idx].bound)
                    {
                        m_bound[txNode] = {nowS, idx};
                        m_attempts[idx].bound = true;
                        break;
                    }
                }
            }
        }
    }
    const double arrS = nowS + distM / kSoundSpeedMps;
    auto& q = m_arrivals[rxNode];
    int64_t attemptIdx = -1;
    if (type == FrameType::Data)
    {
        auto b = m_bound.find(txNode);
        if (b != m_bound.end() && b->second.first == nowS)
        {
            attemptIdx = static_cast<int64_t>(b->second.second);
        }
    }
    q.push_back({arrS, arrS + air, type, txNode, attemptIdx, decodable});
    while (!q.empty() && q.front().e < nowS - kKeepS)
    {
        q.pop_front();
    }
    if (type == FrameType::Data)
    {
        auto b = m_bound.find(txNode);
        if (b != m_bound.end() && b->second.first == nowS)
        {
            Attempt& a = m_attempts[b->second.second];
            if (a.fwd == rxNode)
            {
                a.evaluated = true;
                a.decodable = decodable;
                a.arrS = arrS;
                a.arrE = arrS + air;
            }
        }
    }
}

void
LossBudget::OnDataDelivered(E2EKey key, uint32_t sender, uint32_t fwd, double nowS)
{
    auto it = m_byKey.find(std::make_tuple(key.source, key.seq, sender, fwd));
    if (it == m_byKey.end())
    {
        return;
    }
    // The latest attempt that had started to arrive by now and is not yet marked.
    for (auto r = it->second.rbegin(); r != it->second.rend(); ++r)
    {
        Attempt& a = m_attempts[*r];
        if (!a.received && a.arrS >= 0.0 && a.arrS <= nowS + 1e-9)
        {
            a.received = true;
            return;
        }
    }
}

void
LossBudget::Classify(Attempt& a)
{
    if (a.classified)
    {
        return;
    }
    a.classified = true;
    if (a.txS < m_warmupS || a.txS > m_endS - 20.0)
    {
        return; // outside the window of the measurement; the last seconds may still be in flight
    }
    m_hopsTotal++;
    if (a.received)
    {
        m_hopsDelivered++;
        return;
    }
    std::string cause;
    if (!a.bound)
    {
        cause = "never_on_air";
    }
    else if (!a.evaluated)
    {
        cause = "out_of_range";
    }
    else if (!a.decodable)
    {
        cause = "per_loss";
    }
    else
    {
        const double death = m_deathTimeS ? m_deathTimeS(a.fwd) : -1.0;
        if (death >= 0.0 && death <= a.arrE)
        {
            cause = "receiver_dead";
        }
        else
        {
            bool halfDuplex = false;
            for (const auto& iv : m_air[a.fwd])
            {
                if (iv.first <= a.arrS + 1e-9 && iv.second > a.arrS + 1e-9)
                {
                    halfDuplex = true; // the PHY checks "sending" at the instant the frame starts to arrive
                }
            }
            bool data = false, ctl = false, ack = false, samePacket = false, sameSender = false;
            bool dataDecodable = false, dataStartsInside = false;
            for (const Arrival& o : m_arrivals[a.fwd])
            {
                if (o.tx == a.sender && std::fabs(o.s - a.arrS) < 1e-7)
                {
                    continue; // the frame itself
                }
                if (o.s < a.arrE && o.e > a.arrS)
                {
                    data = data || o.type == FrameType::Data;
                    if (o.type == FrameType::Data)
                    {
                        dataDecodable = dataDecodable || o.ok;
                        dataStartsInside = dataStartsInside || (o.s >= a.arrS);
                    }
                    if (o.type == FrameType::Data && o.attempt >= 0)
                    {
                        samePacket = samePacket || (m_attempts[o.attempt].key == a.key);
                        sameSender = sameSender || (o.tx == a.sender);
                    }
                    ack = ack || o.type == FrameType::Ack || o.type == FrameType::Nack;
                    ctl = ctl || o.type == FrameType::Beacon || o.type == FrameType::Election || o.type == FrameType::Handover;
                }
            }
            if (halfDuplex)
            {
                cause = "half_duplex";
            }
            else if (data)
            {
                cause = "collision_with_data";
                // Why it matters for transmit-power control (CHANGELOG 7 Nov): in this PHY a DECODABLE overheard frame locks the receiver in
                // RECV (a later frame is lost) while an UNDECODABLE one only sets the collision flag, which the next decodable arrival resets.
                m_hopCauses[dataDecodable ? "  of which: some overlapping DATA frame was decodable at the victim receiver"
                                          : "  of which: every overlapping DATA frame was undecodable at the victim receiver"]++;
                if (dataStartsInside)
                {
                    m_hopCauses["  of which: an overlapping DATA frame started inside the victim's window"]++;
                }
                if (samePacket)
                {
                    m_hopCauses["  of which: the other DATA frame is the same packet"]++;
                }
                if (sameSender)
                {
                    m_hopCauses["  of which: the other DATA frame has the same sender"]++;
                }
            }
            else if (ctl)
            {
                cause = "collision_with_beacon_hello";
            }
            else if (ack)
            {
                cause = "collision_with_ack_nack";
            }
            else
            {
                cause = "unexplained_phy_busy";
            }
        }
    }
    a.cause = cause;
    m_hopCauses[cause]++;
}

// Classification needs the arrivals around the attempt, which are only kept for a while: do it as soon as the outcome is
// certain (the forwarder's receive delay is two airtimes, so 5 s after the frame ended is safe).
void
LossBudget::ClassifyReady(double nowS)
{
    for (auto it = m_pending.begin(); it != m_pending.end();)
    {
        Attempt& a = m_attempts[*it];
        const bool ready = (a.bound && a.arrE >= 0.0 && nowS > a.arrE + 5.0) || (a.bound && a.arrE < 0.0 && nowS > a.txS + 60.0);
        if (ready)
        {
            Classify(a);
            it = m_pending.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void
LossBudget::Finalize(const std::function<double(uint32_t)>& deathTimeS)
{
    m_deathTimeS = deathTimeS;
    for (size_t idx : m_pending)
    {
        Classify(m_attempts[idx]);
    }
    m_pending.clear();
}

std::string
LossBudget::PacketHopCause(E2EKey key) const
{
    auto it = m_lastAttemptOfPacket.find(std::make_tuple(key.source, key.seq));
    if (it == m_lastAttemptOfPacket.end())
    {
        return "";
    }
    const Attempt& a = m_attempts[it->second];
    return a.received ? "received_but_not_forwarded" : a.cause;
}

} // namespace cbergr
} // namespace ns3
