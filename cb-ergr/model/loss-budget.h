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

    /// Counterfactual log (simulator side only): for every DATA attempt, whether the same frame would have been received by each
    /// eligible candidate, given what really happened at that candidate (see CHANGELOG 7 Nov, Step 2).
    void EnableCounterfactual(bool on) { m_cf = on; }
    void SetCandidates(uint32_t node, const std::vector<uint16_t>& cands, int shadowChoice);
    /// One CSV line per classified attempt (header from CounterfactualHeader()).
    const std::vector<std::string>& CounterfactualRows() const { return m_cfRows; }
    static const char* CounterfactualHeader() { return "t,sender,source,seq,attempt_of_packet,chosen,chosen_received,chosen_cause,shadow,n_cand,candidates"; }

    /// Per-attempt log (k = 1 experiment, 6 Nov 2026): role / true position of the sender at the attempt, from a simulator-side probe.
    struct AttemptInfo
    {
        bool senderHead = false, fwdSink = false;
        float depthM = -1.0f, dsinkM = -1.0f;
    };
    void EnableAttemptLog(bool on) { m_logAttempts = on; }
    void SetAttemptProbe(std::function<AttemptInfo(uint32_t sender, uint32_t fwd)> probe) { m_attemptProbe = probe; }
    const std::vector<std::string>& AttemptRows() const { return m_attemptRows; }
    static const char* AttemptHeader() { return "t,source,seq,sender,fwd,nth_here,sender_head,fwd_sink,depth_m,dsink_m,received,cause"; }
    /// Frames handed to the MAC after warm-up, by type.
    uint64_t Frames(FrameType t) const { return m_frames[static_cast<int>(t)]; }

    void OnDataKey(uint32_t sender, E2EKey key) { m_lastKey[sender] = key; }
    void OnFrameTx(uint32_t node, FrameType type, uint32_t bytes, int32_t receiver, double nowS);
    /// 'dataId' (DATA frames only) = sourceId << 32 | sourceSeq << 16 | forwarderId, read from the frame header by the channel:
    /// the transmission is bound to its attempt by identity (7 Nov 2026; the old per-sender FIFO lost its alignment whenever a
    /// frame handed to the MAC never went on the air, which happened under Aloha).
    void OnLinkEval(uint32_t txNode, uint32_t rxNode, FrameType type, uint32_t bytes, double distM, bool decodable, double nowS,
                    uint64_t dataId = 0);
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
        // counterfactual (only when enabled)
        std::vector<uint16_t> cand;
        int shadow = -1;
        struct Eval
        {
            bool decodable = false;
            double arrS = 0.0, arrE = 0.0;
        };
        std::map<uint32_t, Eval> eval;     // per candidate that was within range of the transmission
        uint8_t nth = 1;                   // how many DATA attempts this sender had made for this packet so far
        uint8_t nthHere = 1;               // attempt number of this sender for this packet (1 = first attempt, >1 = retry)
        AttemptInfo info;
    };
    struct Arrival
    {
        double s, e;
        FrameType type;
        uint32_t tx;
        int64_t attempt; // index of the DATA attempt this frame belongs to, -1 for other frame types
        bool ok = true;  // decodable at this receiver (a decodable frame also locks the receiver in RECV; an undecodable one only sets the collision flag)
    };

    /// Why a copy of attempt 'a' would be lost at receiver 'rx' ("" = nothing explains a loss). Also fills the detail flags.
    struct Why
    {
        std::string cause;
        bool samePacket = false, sameSender = false, dataDecodable = false, dataStartsInside = false, lockedBefore = false;
    };
    Why WhyLostAt(const Attempt& a, uint32_t rx, bool evaluated, bool decodable, double arrS, double arrE) const;
    void Classify(Attempt& a);
    void ClassifyReady(double nowS);
    double Airtime(uint32_t bytes) const { return m_preambleS + bytes * 8.0 / m_bitRateBps; }

    double m_bitRateBps = 0.0, m_preambleS = 0.0, m_warmupS = 0.0, m_endS = 0.0;
    std::map<uint32_t, E2EKey> m_lastKey;
    std::vector<Attempt> m_attempts;
    std::vector<size_t> m_pending;                                     // attempts not yet classified
    std::function<double(uint32_t)> m_deathTimeS;
    std::map<uint32_t, std::pair<double, size_t>> m_bound;             // per sender: air start time -> attempt of that transmission
    std::map<std::tuple<uint16_t, uint16_t, uint32_t, uint32_t>, std::vector<size_t>> m_byKey; // (source, seq, sender, fwd) -> attempts
    std::map<uint32_t, std::deque<Arrival>> m_arrivals;                // per receiver, recent
    std::map<uint32_t, std::deque<std::pair<double, double>>> m_air;   // per sender: air intervals (for half duplex)
    std::map<uint32_t, double> m_lastAirStart;
    std::array<double, 8> m_airtimeS{};
    std::map<std::tuple<uint16_t, uint16_t>, size_t> m_lastAttemptOfPacket;
    std::map<std::string, uint64_t> m_hopCauses;
    uint64_t m_hopsTotal = 0, m_hopsDelivered = 0;
    bool m_cf = false;
    std::map<uint32_t, std::pair<std::vector<uint16_t>, int>> m_pendingCand;   // per sender: candidates of the decision just made
    std::map<std::pair<uint16_t, uint16_t>, uint8_t> m_nthAttempt;              // (source, seq) -> attempts so far (any node)
    std::vector<std::string> m_cfRows;
    bool m_logAttempts = false;
    std::function<AttemptInfo(uint32_t, uint32_t)> m_attemptProbe;
    std::map<std::tuple<uint16_t, uint16_t, uint32_t>, uint8_t> m_nthHere; // (source, seq, sender) -> attempts so far
    std::vector<std::string> m_attemptRows;
    std::array<uint64_t, 8> m_frames{};
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_LOSS_BUDGET_H */
