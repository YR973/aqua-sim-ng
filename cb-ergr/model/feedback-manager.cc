/*
 * feedback-manager.cc: pending-attempt table, timers and label resolution (spec §9.1-§9.8, kickoff §2.3).
 * Keeps the rule "silence is never success": an attempt becomes label 1 only on positive evidence
 * (ACK or a set bit in a receipt bitmap); every other end is an explicit 0 with its source.
 */

#include "feedback-manager.h"

#include "ns3/simulator.h"

#include <algorithm>

namespace ns3
{
namespace cbergr
{

FeedbackManager::FeedbackManager(const Config& cfg,
                                 Ptr<ForwarderSelector> selector,
                                 Ptr<MetricsCollector> metrics,
                                 Hooks hooks)
    : m_cfg(cfg),
      m_selector(selector),
      m_metrics(metrics),
      m_hooks(hooks)
{
    // Report unresolved attempts at the end of the run for the consistency check.
    m_metrics->RegisterPendingProbe([this]() { return m_pending.size(); });
}

// ============================================================ sender side ==

void
FeedbackManager::Track(const PendingAttempt& attempt, const HeldPacket& packet, double ackTimeoutS)
{
    PendingAttempt pa = attempt;
    pa.sentAt = Simulator::Now();
    if (pa.ackRequested)
    {
        pa.ackTimer = Simulator::Schedule(Seconds(ackTimeoutS), &FeedbackManager::OnAckTimeout, this, pa.hop);
    }
    pa.expiryTimer = Simulator::Schedule(Seconds(m_cfg.expiryS), &FeedbackManager::OnExpiry, this, pa.hop);
    m_pending[pa.hop] = pa;
    m_peakPending = std::max(m_peakPending, m_pending.size());
    m_metrics->OnPendingSize(m_pending.size());
    m_buffer[packet.key] = packet;
}

void
FeedbackManager::Resolve(const HopKey& key, int y, LabelSource src)
{
    auto it = m_pending.find(key);
    if (it == m_pending.end())
    {
        // Already labelled: a second label (late ACK, bitmap) is ignored but counted (spec §8.8).
        m_duplicateLabels++;
        m_metrics->OnDuplicateLabel(m_cfg.myId);
        return;
    }
    PendingAttempt pa = it->second;
    m_pending.erase(it);
    pa.ackTimer.Cancel();
    pa.expiryTimer.Cancel();

    if (y < 0 && m_cfg.censoredAsZero)
    {
        y = 0; // old behaviour: "no evidence" is counted as a failure
        src = LabelSource::Expiry;
    }
    double delayS = (Simulator::Now() - pa.sentAt).GetSeconds();
    m_metrics->OnLabel(m_cfg.myId, key.fwdId, key.hopSeq, y, src, delayS, pa.attempt, pa.ackRequested, pa.learn,
                       pa.decision);
    if (pa.learn && y >= 0)
    {
        m_selector->OnLabel(pa.decision, y, src, Simulator::Now()); // a censored label is never shown to the learner
    }
    if (y == 1)
    {
        m_buffer.erase(pa.pkt); // the forwarder has it: nothing more to do at this hop
    }
    if (src == LabelSource::Ack && m_hooks.forwarderAcked)
    {
        m_hooks.forwarderAcked(key.fwdId);
    }
    else if (src == LabelSource::AckTimeout && m_hooks.forwarderTimedOut)
    {
        m_hooks.forwarderTimedOut(key.fwdId);
    }
}

void
FeedbackManager::OnAck(uint16_t ackerId, uint16_t hopSeq)
{
    m_metrics->OnAckReceived(m_cfg.myId);
    Resolve(HopKey{ackerId, hopSeq}, 1, LabelSource::Ack);
}

void
FeedbackManager::OnAckTimeout(HopKey key)
{
    auto it = m_pending.find(key);
    if (it == m_pending.end())
    {
        return;
    }
    E2EKey pkt = it->second.pkt;
    if (!m_cfg.keepRecordAfterTimeout)
    {
        // Old rule (K§2.3 / D§3 P8): a timed-out ACK is label 0 at once and the record is erased.
        Resolve(key, 0, LabelSource::AckTimeout);
        RetryOrDrop(pkt);
        return;
    }
    // New rule (7 Nov 2026): the retry starts now, but the record stays so that a late ACK or bitmap can still label the attempt;
    // if nothing arrives by expiry the label is censored.
    it->second.timedOut = true;
    m_metrics->OnAckTimeoutFired(m_cfg.myId, key.fwdId, key.hopSeq);
    if (m_hooks.forwarderTimedOut)
    {
        m_hooks.forwarderTimedOut(key.fwdId);
    }
    RetryOrDrop(pkt);
}

void
FeedbackManager::OnExpiry(HopKey key)
{
    auto it = m_pending.find(key);
    if (it == m_pending.end())
    {
        return;
    }
    E2EKey pkt = it->second.pkt;
    const bool retryStarted = it->second.ackRequested; // an AR attempt started its retry at the ACK timeout
    Resolve(key, -1, LabelSource::Censored);          // nothing proved either outcome: censored (dropped unless CensoredLabels=Zero)
    if (!retryStarted)
    {
        RetryOrDrop(pkt); // without an ACK request nothing else would have started a retry
    }
}

void
FeedbackManager::OnVoid(uint16_t fwdId, uint16_t hopSeq)
{
    // Only if the attempt has no label yet; the routing layer re-routes the packet itself (NACK handling).
    HopKey key{fwdId, hopSeq};
    if (m_pending.count(key))
    {
        Resolve(key, 0, LabelSource::Void);
    }
}

void
FeedbackManager::OnBeaconReceipts(const ErgrBeaconHeader& beacon)
{
    const ReceiptEntry* mine = nullptr;
    for (const ReceiptEntry& r : beacon.receipts)
    {
        if (r.senderId == m_cfg.myId)
        {
            mine = &r;
            break;
        }
    }
    if (!mine)
    {
        return; // silence is not evidence
    }
    std::vector<HopKey> keys;
    for (const auto& entry : m_pending)
    {
        if (entry.first.fwdId == beacon.nodeId)
        {
            keys.push_back(entry.first);
        }
    }
    for (const HopKey& key : keys)
    {
        ReceiptStatus status = LookupReceipt(mine->highest, mine->mask, key.hopSeq);
        if (status == ReceiptStatus::Received)
        {
            Resolve(key, 1, LabelSource::Bitmap);
        }
        else if (status == ReceiptStatus::Missing)
        {
            E2EKey pkt = m_pending[key].pkt;
            const bool retryStarted = m_pending[key].timedOut; // the ACK timer already started the retry
            Resolve(key, 0, LabelSource::Bitmap); // an explicit loss: the forwarder skipped this hopSeq
            if (!retryStarted)
            {
                RetryOrDrop(pkt);
            }
        }
    }
}

void
FeedbackManager::RetryOrDrop(const E2EKey& pkt)
{
    auto it = m_buffer.find(pkt);
    if (it == m_buffer.end())
    {
        return; // another attempt already succeeded, or the packet is gone
    }
    HeldPacket held = it->second;
    if (held.attempt < m_cfg.maxTx)
    {
        m_hooks.retry(held); // a fresh decision with a fresh hopSeq
        return;
    }
    m_buffer.erase(it);
    m_hooks.drop(held, DropReason::MaxTxReached);
}

// ========================================================== receiver side ==

void
FeedbackManager::MarkReceived(uint16_t senderId, uint16_t hopSeq, Time now)
{
    ReceiptState& state = m_receipts[senderId];
    state.window.Mark(hopSeq);
    state.lastHeard = now;
}

std::vector<ReceiptEntry>
FeedbackManager::BuildReceipts(Time now) const
{
    std::vector<ReceiptEntry> entries;
    for (const auto& item : m_receipts)
    {
        if ((now - item.second.lastHeard).GetSeconds() > m_cfg.receiptKeepS)
        {
            continue;
        }
        ReceiptEntry e;
        e.senderId = item.first;
        e.highest = item.second.window.Highest();
        e.mask = item.second.window.Mask();
        entries.push_back(e);
        if (entries.size() >= 255)
        {
            break; // nReceipts is one byte
        }
    }
    return entries;
}

} // namespace cbergr
} // namespace ns3
