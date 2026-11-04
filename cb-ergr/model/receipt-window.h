/*
 * receipt-window.h: per-sender receipt bitmap (spec §7.2, §9.5).
 *
 * The receiver keeps, for every upstream sender, the highest hopSeq heard and
 * a 32-bit mask: bit k set means hopSeq (highest - k) was received.
 * The mask travels on beacons (6.3 ReceiptEntry). The sender reads it back
 * with LookupReceipt() to turn a silent attempt into an explicit 0 or 1 label.
 * Plain C++, no ns-3 dependency.
 */

#ifndef CB_ERGR_RECEIPT_WINDOW_H
#define CB_ERGR_RECEIPT_WINDOW_H

#include "serial-number.h"

#include <cstdint>

namespace ns3
{
namespace cbergr
{

constexpr int kReceiptWindowBits = 32;

/// What a receipt entry says about one specific hopSeq.
enum class ReceiptStatus
{
    Received,    ///< bit is set: the forwarder got it
    Missing,     ///< inside the window, bit is clear: an explicit loss
    OutOfWindow  ///< older than the window or newer than 'highest': no information
};

/// Receiver side: record each hopSeq heard from one sender.
class ReceiptWindow
{
  public:
    /// Spec §7.2: update the window when hopSeq 's' arrives.
    void Mark(uint16_t s)
    {
        if (!m_valid)
        {
            m_valid = true;
            m_highest = s;
            m_mask = 1;
            return;
        }
        if (SerialGreater(s, m_highest))
        {
            uint16_t k = SerialForward(s, m_highest);
            m_mask = (k >= kReceiptWindowBits) ? 1u : ((m_mask << k) | 1u);
            m_highest = s;
        }
        else
        {
            uint16_t k = SerialForward(m_highest, s);
            if (k < kReceiptWindowBits)
            {
                m_mask |= (1u << k);
            }
        }
    }

    bool IsValid() const { return m_valid; }
    uint16_t Highest() const { return m_highest; }
    uint32_t Mask() const { return m_mask; }

  private:
    bool m_valid = false;
    uint16_t m_highest = 0;
    uint32_t m_mask = 0;
};

/**
 * Sender side (spec §9.5): read hopSeq out of a (highest, mask) receipt entry.
 * k = highest - hopSeq; only 0 <= k < 32 carries information.
 */
inline ReceiptStatus
LookupReceipt(uint16_t highest, uint32_t mask, uint16_t hopSeq)
{
    int k = SerialDiff(highest, hopSeq);
    if (k < 0 || k >= kReceiptWindowBits)
    {
        return ReceiptStatus::OutOfWindow;
    }
    return ((mask >> k) & 1u) ? ReceiptStatus::Received : ReceiptStatus::Missing;
}

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_RECEIPT_WINDOW_H */
