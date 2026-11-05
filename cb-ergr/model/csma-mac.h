/*
 * csma-mac.h: a minimal listen-before-talk MAC for the cb-ergr scenarios (Mac=Csma).
 *
 * The ERGR-EMHC paper used "a CSMA-based MAC" (section 5). Aqua-Sim NG has no plain CSMA MAC: its AquaSimBroadcastMac senses
 * the channel but backs off at most 0.1 s, four times, and then silently drops the frame. This class is the smallest thing that
 * deserves the name:
 *   - frames wait in a FIFO (limit params::mac::kQueueLimit);
 *   - before sending, the head frame waits a random number of slots in [0, CW); CW = kCwMin for ordinary frames and
 *     kCwControl for ACK/NACK frames (they have a timer running);
 *   - when the backoff ends, the frame is sent if the modem is idle (not sending, not receiving); otherwise CW doubles (up to
 *     kCwMax) and a new backoff starts; after kMaxAttempts deferrals the frame is dropped and counted;
 *   - the next frame starts its backoff when the modem is free again.
 * "Channel busy" = the modem is receiving or sending (the only carrier sense Aqua-Sim exposes), so propagation delay (up to 1 s)
 * still lets hidden transmissions collide, as in any underwater CSMA.
 */

#ifndef CB_ERGR_CSMA_MAC_H
#define CB_ERGR_CSMA_MAC_H

#include "ns3/aqua-sim-mac.h"
#include "ns3/random-variable-stream.h"

#include <deque>

namespace ns3
{
namespace cbergr
{

class CsmaMac : public AquaSimMac
{
  public:
    static TypeId GetTypeId();
    CsmaMac();

    bool RecvProcess(Ptr<Packet> pkt) override;
    bool TxProcess(Ptr<Packet> pkt) override;
    int64_t AssignStreams(int64_t stream) override;

    uint64_t Drops() const { return m_drops; }
    uint64_t Deferrals() const { return m_deferrals; }

  protected:
    void DoDispose() override;

  private:
    struct Waiting
    {
        Ptr<Packet> packet;   // carries the AquaSimHeader (SetTxTime done) and the MAC header
        bool control = false;
        int attempts = 0;
    };

    void StartBackoff();
    void BackoffDone();

    std::deque<Waiting> m_queue;
    bool m_timerRunning = false;
    Ptr<UniformRandomVariable> m_rand;
    double m_slotS;
    int m_cwMin, m_cwMax, m_cwControl, m_maxAttempts, m_queueLimit;
    uint64_t m_drops = 0, m_deferrals = 0;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_CSMA_MAC_H */
