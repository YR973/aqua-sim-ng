/*
 * loss-budget.h: where do data hops and packets get lost? (6 Nov 2026, user request 2)
 *
 * Reporting only, never read by a protocol. For every DATA frame a node hands to its MAC (a "hop attempt" to a named
 * forwarder) the collector learns, from the channel evaluation, when the frame is on the air and when it reaches the forwarder,
 * and from the forwarder whether it arrived. Every attempt that did not arrive gets ONE cause:
 *
 *   never_on_air               the MAC queue dropped it (or it was still queued at the end)
 *   out_of_range               the forwarder was farther than R when the frame was sent (position estimate error / mobility)
 *   per_loss                   the channel model made the frame undecodable (SNR too low)
 *   receiver_dead              the forwarder's battery was empty
 *   half_duplex                the forwarder was transmitting when the frame started to arrive
 *   collision_with_data        another DATA frame overlapped it at the forwarder
 *   collision_with_beacon_hello  a beacon / election (Hello, Retire, Solicit) / handover frame overlapped it
 *   collision_with_ack_nack    an ACK or NACK overlapped it
 *   unexplained_phy_busy       decodable, nothing overlapped it, forwarder not transmitting, yet not delivered: the PHY was
 *                              still "busy" after an earlier collision (see CHANGELOG, PHY review) or the routing layer dropped it
 *
 * Overlap = the two frames are at the forwarder at the same time (Aqua-Sim has no capture: any overlap is a collision).
 * When several frame types overlap, the first in the order DATA, beacon/Hello, ACK/NACK is named. A packet that was not delivered
 * gets the cause of its last failed hop, or its drop reason, or "lost_after_receipt" (the last forwarder received it but never
 * sent it on) or "never_sent".
 * Airtime: the sum of frame durations by type (a frame heard by many nodes still counts once), as an Erlang load and a share.
 */

#ifndef CB_ERGR_LOSS_BUDGET_H
#define CB_ERGR_LOSS_BUDGET_H

#include "forwarder-selector.h"
#include "ergr-headers.h"

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace ns3
{
namespace cbergr
{

class LossBudget
{
  public:
    void Configure(double bitRateBps, double preambleS, double warmupS, double endS);
    void SetDeathProbe(std::function<double(uint32_t)> probe) { m_deathTimeS = probe; }
    bool Enabled() const { return m_bitRateBps > 0.0; }

    void OnDataKey(uint32_t sender, E2EKey key) { m_lastKey[sender] = key; }
    void OnFrameTx(uint32_t node, FrameType type, uint32_t bytes, int32_t receiver, double nowS);
    void OnLinkEval(uint32_t txNode, uint32_t rxNode, FrameType type, uint32_t bytes, double distM, bool decodable, double nowS);
    void OnDataDelivered(E2EKey key, uint32_t sender, uint32_t fwd, double nowS);

    /// Classify every attempt; 'deathTimeS(node)' returns the battery death time or -1.
    void Finalize(const std::function<double(uint32_t)>& deathTimeS);

    /// Cause of the last failed hop of this packet ("" if none recorded).
    std::string PacketHopCause(E2EKey key) const;

    const std::map<std::string, uint64_t>& HopCauses() const { return m_hopCauses; }
    uint64_t HopsTotal() const { return m_hopsTotal; }
    uint64_t HopsDelivered() const { return m_hopsDelivered; }
    double AirtimeS(FrameType t) const { return m_airtimeS[static_cast<int>(t)]; }
    double WindowS() const { return m_endS - m_warmupS; }

  private:
    struct Attempt
    {
        E2EKey key;
        uint32_t sender = 0, fwd = 0;
        double txS = 0.0;
        bool bound = false, evaluated = false, decodable = true, received = false;
        double arrS = -1.0, arrE = -1.0;
        std::string cause;
        bool classified = false;
    };
    struct Arrival
    {
        double s, e;
        FrameType type;
        uint32_t tx;
        int64_t attempt; // index of the DATA attempt this frame belongs to, -1 for other frame types
    };

    void Classify(Attempt& a);
    void ClassifyReady(double nowS);
    double Airtime(uint32_t bytes) const { return m_preambleS + bytes * 8.0 / m_bitRateBps; }

    double m_bitRateBps = 0.0, m_preambleS = 0.0, m_warmupS = 0.0, m_endS = 0.0;
    std::map<uint32_t, E2EKey> m_lastKey;
    std::vector<Attempt> m_attempts;
    std::vector<size_t> m_pending;                                     // attempts not yet classified
    std::function<double(uint32_t)> m_deathTimeS;
    std::map<uint32_t, std::deque<size_t>> m_unbound;                  // per sender: attempts handed to the MAC, not yet on the air
    std::map<uint32_t, std::pair<double, size_t>> m_bound;             // per sender: air start time -> attempt of that transmission
    std::map<std::tuple<uint16_t, uint16_t, uint32_t, uint32_t>, std::vector<size_t>> m_byKey; // (source, seq, sender, fwd) -> attempts
    std::map<uint32_t, std::deque<Arrival>> m_arrivals;                // per receiver, recent
    std::map<uint32_t, std::deque<std::pair<double, double>>> m_air;   // per sender: air intervals (for half duplex)
    std::map<uint32_t, double> m_lastAirStart;
    std::array<double, 8> m_airtimeS{};
    std::map<std::tuple<uint16_t, uint16_t>, size_t> m_lastAttemptOfPacket;
    std::map<std::string, uint64_t> m_hopCauses;
    uint64_t m_hopsTotal = 0, m_hopsDelivered = 0;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_LOSS_BUDGET_H */
