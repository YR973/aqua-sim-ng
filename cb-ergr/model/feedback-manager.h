/*
 * feedback-manager.h: turns a sent packet into a 0/1 label for the learner (spec §7, §9; kickoff §2.3).
 *
 * Every DATA attempt made through a feedback-using selector is recorded as a PendingAttempt, keyed by
 * (forwarder, hopSeq). It is resolved by exactly one of:
 *   - an ACK from the forwarder                    -> label 1  (source Ack)
 *   - the ACK timer firing (AR attempts only)      -> label 0  at once (K§2.3, D§3 P8) (source AckTimeout)
 *   - a receipt bitmap on the forwarder's beacon   -> label 1 or an explicit 0 (source Bitmap)
 *   - the expiry timer                             -> label 0  (source Expiry)
 *   - a void NACK from the forwarder               -> label 0  (source Void)
 * The first label for an attempt wins; later ones are counted as duplicate labels and ignored.
 * Ground truth (did the forwarder really get it?) is recorded by the metrics collector for the CSVs
 * only; it is never passed to the learner (K§0 rule 7).
 *
 * Receiver side: one ReceiptWindow per upstream sender, written into outgoing beacons.
 */

#ifndef CB_ERGR_FEEDBACK_MANAGER_H
#define CB_ERGR_FEEDBACK_MANAGER_H

#include "ergr-headers.h"
#include "forwarder-selector.h"
#include "held-packet.h"
#include "metrics-collector.h"
#include "receipt-window.h"

#include "ns3/event-id.h"
#include "ns3/nstime.h"

#include <functional>
#include <map>
#include <vector>

namespace ns3
{
namespace cbergr
{

/// (forwarder, hopSeq): identifies one transmission attempt from this node.
struct HopKey
{
    uint16_t fwdId = 0;
    uint16_t hopSeq = 0;
    bool operator<(const HopKey& o) const { return fwdId != o.fwdId ? fwdId < o.fwdId : hopSeq < o.hopSeq; }
};

/// One DATA attempt waiting for its label.
struct PendingAttempt
{
    Decision decision;       ///< what the selector decided (features x, power, AR)
    HopKey hop;
    E2EKey pkt;
    Time sentAt;
    bool ackRequested = false;
    uint8_t attempt = 1;     ///< 1 for the first transmission of the packet at this node
    bool learn = true;       ///< false for member hand-offs: the label is logged but not given to the selector
    double predictedDistM = 0.0;
    EventId ackTimer;
    EventId expiryTimer;
};

class FeedbackManager
{
  public:
    struct Config
    {
        uint16_t myId = 0;
        double beaconPeriodS = 0.0;
        int maxTx = 0;
        double expiryS = 0.0;            ///< MExpiry
        double receiptKeepS = 0.0;       ///< how long a sender stays in outgoing receipt lists
    };

    /// Callbacks into the routing layer (kept as functions so this class does not depend on it).
    struct Hooks
    {
        std::function<void(const HeldPacket&)> retry;                         ///< route the packet again (fresh decision)
        std::function<void(const HeldPacket&, DropReason)> drop;              ///< give up on the packet
        std::function<void(uint16_t)> forwarderTimedOut;                      ///< an ACK timer fired for this forwarder (suspect tier)
        std::function<void(uint16_t)> forwarderAcked;                         ///< an ACK arrived from this forwarder
    };

    FeedbackManager(const Config& cfg, Ptr<ForwarderSelector> selector, Ptr<MetricsCollector> metrics, Hooks hooks);

    // --- Sender side ---

    /**
     * Record a transmitted attempt, keep a copy of the packet for retries and start the timers.
     * 'ackTimeoutS' is only used when the attempt requested an ACK.
     */
    void Track(const PendingAttempt& attempt, const HeldPacket& packet, double ackTimeoutS);

    /// Spec §9.3: an ACK frame arrived from 'ackerId' for hopSeq.
    void OnAck(uint16_t ackerId, uint16_t hopSeq);

    /// Spec §9.5: a beacon arrived; resolve pending attempts to that node from its receipt list.
    void OnBeaconReceipts(const ErgrBeaconHeader& beacon);

    /// Void NACK from the forwarder of an attempt that is still unresolved (label 0, source Void).
    void OnVoid(uint16_t fwdId, uint16_t hopSeq);

    /// True while a copy of the packet is kept for retries.
    bool IsBuffered(const E2EKey& key) const { return m_buffer.count(key) > 0; }

    /// Number of attempts waiting for a label.
    size_t PendingCount() const { return m_pending.size(); }

    /// Attempts still unresolved (for the end-of-run consistency check).
    size_t Unresolved() const { return m_pending.size(); }

    // --- Receiver side ---

    /// Spec §9.2 step 1: remember that hopSeq from 'senderId' arrived.
    void MarkReceived(uint16_t senderId, uint16_t hopSeq, Time now);

    /// Receipt entries for the next beacon: every sender heard within 'receiptKeepS'.
    std::vector<ReceiptEntry> BuildReceipts(Time now) const;

    // --- Counters for the metrics ---
    uint64_t DuplicateLabels() const { return m_duplicateLabels; }

  private:
    struct ReceiptState
    {
        ReceiptWindow window;
        Time lastHeard;
    };

    /// Spec §9.7: remove the attempt, cancel its timers, tell the selector, log, free the buffer on success.
    void Resolve(const HopKey& key, int y, LabelSource src);
    void OnAckTimeout(HopKey key);
    void OnExpiry(HopKey key);
    /// Spec §9.8: another attempt if the packet is still buffered and MaxTx is not reached.
    void RetryOrDrop(const E2EKey& pkt);

    Config m_cfg;
    Ptr<ForwarderSelector> m_selector;
    Ptr<MetricsCollector> m_metrics;
    Hooks m_hooks;
    std::map<HopKey, PendingAttempt> m_pending;
    std::map<E2EKey, HeldPacket> m_buffer;
    std::map<uint16_t, ReceiptState> m_receipts;
    uint64_t m_duplicateLabels = 0;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_FEEDBACK_MANAGER_H */
