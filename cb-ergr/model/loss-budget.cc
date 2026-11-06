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
LossBudget::SetCandidates(uint32_t node, const std::vector<uint16_t>& cands, int shadowChoice)
{
    if (m_cf)
    {
        m_pendingCand[node] = {cands, shadowChoice};
    }
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
    if (m_cf)
    {
        a.nth = ++m_nthAttempt[std::make_pair(a.key.source, a.key.seq)];
        auto pc = m_pendingCand.find(node);
        if (pc != m_pendingCand.end())
        {
            a.cand = pc->second.first;
            a.shadow = pc->second.second;
            m_pendingCand.erase(pc);
        }
        if (a.cand.empty())
        {
            a.cand.push_back(static_cast<uint16_t>(receiver));
        }
    }
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
            if (m_cf && std::find(a.cand.begin(), a.cand.end(), static_cast<uint16_t>(rxNode)) != a.cand.end())
            {
                a.eval[rxNode] = {decodable, arrS, arrS + air};
            }
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

LossBudget::Why
LossBudget::WhyLostAt(const Attempt& a, uint32_t rx, bool evaluated, bool decodable, double arrS, double arrE) const
{
    Why w;
    if (!evaluated)
    {
        w.cause = "out_of_range";
        return w;
    }
    if (!decodable)
    {
        w.cause = "per_loss";
        return w;
    }
    const double death = m_deathTimeS ? m_deathTimeS(rx) : -1.0;
    if (death >= 0.0 && death <= arrE)
    {
        w.cause = "receiver_dead";
        return w;
    }
    bool halfDuplex = false;
    auto air = m_air.find(rx);
    if (air != m_air.end())
    {
        for (const auto& iv : air->second)
        {
            if (iv.first <= arrS + 1e-9 && iv.second > arrS + 1e-9)
            {
                halfDuplex = true; // the PHY checks "sending" at the instant the frame starts to arrive
            }
        }
    }
    bool data = false, ctl = false, ack = false;
    auto arr = m_arrivals.find(rx);
    if (arr != m_arrivals.end())
    {
        for (const Arrival& o : arr->second)
        {
            if (o.tx == a.sender && std::fabs(o.s - arrS) < 1e-7)
            {
                continue; // the frame itself
            }
            if (o.s < arrE && o.e > arrS)
            {
                data = data || o.type == FrameType::Data;
                if (o.type == FrameType::Data)
                {
                    w.dataDecodable = w.dataDecodable || o.ok;
                    w.dataStartsInside = w.dataStartsInside || (o.s >= arrS);
                    w.lockedBefore = w.lockedBefore || (o.ok && o.s < arrS); // a decodable frame that started first holds the receiver in RECV
                    if (o.attempt >= 0)
                    {
                        w.samePacket = w.samePacket || (m_attempts[o.attempt].key == a.key);
                        w.sameSender = w.sameSender || (o.tx == a.sender);
                    }
                }
                ack = ack || o.type == FrameType::Ack || o.type == FrameType::Nack;
                ctl = ctl || o.type == FrameType::Beacon || o.type == FrameType::Election || o.type == FrameType::Handover;
            }
        }
    }
    if (halfDuplex)
    {
        w.cause = "half_duplex";
    }
    else if (data)
    {
        w.cause = "collision_with_data";
    }
    else if (ctl)
    {
        w.cause = "collision_with_beacon_hello";
    }
    else if (ack)
    {
        w.cause = "collision_with_ack_nack";
    }
    return w;
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
    // Counterfactual: would the same frame have reached each eligible candidate?
    auto code = [](const std::string& cause) {
        if (cause.empty()) return 'R';
        if (cause == "out_of_range") return 'O';
        if (cause == "per_loss") return 'P';
        if (cause == "half_duplex") return 'H';
        if (cause == "receiver_dead") return 'D';
        return 'C';
    };
    std::string chosenCause;
    if (a.received)
    {
        m_hopsDelivered++;
    }
    else
    {
        std::string cause;
        if (!a.bound)
        {
            cause = "never_on_air";
        }
        else
        {
            Why w = WhyLostAt(a, a.fwd, a.evaluated, a.decodable, a.arrS, a.arrE);
            cause = w.cause.empty() ? "unexplained_phy_busy" : w.cause;
            if (cause == "collision_with_data")
            {
                if (w.samePacket)
                {
                    m_hopCauses["  of which: the other DATA frame is the same packet"]++;
                }
                if (w.sameSender)
                {
                    m_hopCauses["  of which: the other DATA frame has the same sender"]++;
                }
                // Why it matters for transmit-power control (CHANGELOG 7 Nov): in this PHY a DECODABLE overheard frame locks the receiver in
                // RECV (a later frame is lost) while an UNDECODABLE one only sets the collision flag, which the next decodable arrival resets.
                m_hopCauses[w.dataDecodable ? "  of which: some overlapping DATA frame was decodable at the victim receiver"
                                            : "  of which: every overlapping DATA frame was undecodable at the victim receiver"]++;
                if (w.dataStartsInside)
                {
                    m_hopCauses["  of which: an overlapping DATA frame started inside the victim's window"]++;
                }
                if (w.lockedBefore)
                {
                    m_hopCauses["  of which: receiver locked by an earlier decodable DATA frame"]++; // receiver-lock events (9 Nov 2026)
                }
            }
        }
        a.cause = cause;
        m_hopCauses[cause]++;
        chosenCause = cause;
    }
    if (m_cf && a.bound && !a.cand.empty())
    {
        std::string cands;
        for (uint16_t c : a.cand)
        {
            char status;
            if (c == a.fwd)
            {
                status = a.received ? 'R' : code(chosenCause == "unexplained_phy_busy" ? "collision" : chosenCause);
            }
            else
            {
                auto ev = a.eval.find(c);
                const bool in = ev != a.eval.end();
                Why w = WhyLostAt(a, c, in, in && ev->second.decodable, in ? ev->second.arrS : 0.0, in ? ev->second.arrE : 0.0);
                status = code(w.cause);
            }
            cands += (cands.empty() ? "" : "|") + std::to_string(c) + ":" + status;
        }
        char line[512];
        std::snprintf(line, sizeof(line), "%.3f,%u,%u,%u,%u,%u,%d,%s,%d,%zu,", a.txS, a.sender, a.key.source, a.key.seq, a.nth, a.fwd,
                      a.received ? 1 : 0, a.received ? "ok" : chosenCause.c_str(), a.shadow, a.cand.size());
        m_cfRows.push_back(std::string(line) + cands);
    }
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
