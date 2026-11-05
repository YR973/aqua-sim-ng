/*
 * csma-mac.cc: listen-before-talk MAC with random backoff, see csma-mac.h.
 */

#include "csma-mac.h"

#include "cb-ergr-params.h"
#include "ergr-headers.h"

#include "ns3/aqua-sim-address.h"
#include "ns3/aqua-sim-header-mac.h"
#include "ns3/aqua-sim-header.h"
#include "ns3/aqua-sim-net-device.h"
#include "ns3/double.h"
#include "ns3/integer.h"
#include "ns3/simulator.h"

#include <algorithm>

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(CsmaMac);

TypeId
CsmaMac::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::CsmaMac")
            .SetParent<AquaSimMac>()
            .AddConstructor<CsmaMac>()
            .AddAttribute("SlotS", "Backoff slot (s).", DoubleValue(params::mac::kSlotS),
                          MakeDoubleAccessor(&CsmaMac::m_slotS), MakeDoubleChecker<double>(0.001))
            .AddAttribute("CwMin", "Initial contention window (slots).", IntegerValue(params::mac::kCwMin),
                          MakeIntegerAccessor(&CsmaMac::m_cwMin), MakeIntegerChecker<int>(1))
            .AddAttribute("CwMax", "Largest contention window (slots).", IntegerValue(params::mac::kCwMax),
                          MakeIntegerAccessor(&CsmaMac::m_cwMax), MakeIntegerChecker<int>(1))
            .AddAttribute("CwControl", "Contention window for ACK/NACK frames (slots).", IntegerValue(params::mac::kCwControl),
                          MakeIntegerAccessor(&CsmaMac::m_cwControl), MakeIntegerChecker<int>(1))
            .AddAttribute("MaxAttempts", "Busy-channel deferrals before a frame is dropped.",
                          IntegerValue(params::mac::kMaxAttempts), MakeIntegerAccessor(&CsmaMac::m_maxAttempts),
                          MakeIntegerChecker<int>(1))
            .AddAttribute("QueueLimit", "Frames that may wait in the MAC.", IntegerValue(params::mac::kQueueLimit),
                          MakeIntegerAccessor(&CsmaMac::m_queueLimit), MakeIntegerChecker<int>(1));
    return tid;
}

CsmaMac::CsmaMac()
    : m_slotS(params::mac::kSlotS),
      m_cwMin(params::mac::kCwMin),
      m_cwMax(params::mac::kCwMax),
      m_cwControl(params::mac::kCwControl),
      m_maxAttempts(params::mac::kMaxAttempts),
      m_queueLimit(params::mac::kQueueLimit)
{
    m_rand = CreateObject<UniformRandomVariable>();
}

int64_t
CsmaMac::AssignStreams(int64_t stream)
{
    m_rand->SetStream(stream);
    return 1;
}

void
CsmaMac::DoDispose()
{
    m_queue.clear();
    AquaSimMac::DoDispose();
}

bool
CsmaMac::RecvProcess(Ptr<Packet> pkt)
{
    AquaSimHeader ash;
    MacHeader mach;
    pkt->RemoveHeader(ash);
    pkt->RemoveHeader(mach);
    if (ash.GetErrorFlag())
    {
        return false;
    }
    AquaSimAddress dst = mach.GetDA();
    if (dst == AquaSimAddress::GetBroadcast() || dst == AquaSimAddress::ConvertFrom(m_device->GetAddress()))
    {
        pkt->AddHeader(ash); // the MAC header stays off: the routing layer addresses frames itself
        return SendUp(pkt);
    }
    return false;
}

bool
CsmaMac::TxProcess(Ptr<Packet> pkt)
{
    AquaSimHeader ash;
    MacHeader mach;
    pkt->RemoveHeader(ash);
    mach.SetDA(AquaSimAddress::GetBroadcast());
    mach.SetSA(AquaSimAddress::ConvertFrom(m_device->GetAddress()));
    ash.SetTxTime(GetTxTime(pkt));
    ash.SetDirection(AquaSimHeader::DOWN);
    // The routing layer's frame type is the first byte after the AquaSim header.
    const uint8_t type = PeekFrameType(pkt);
    pkt->AddHeader(mach);
    pkt->AddHeader(ash);
    if (static_cast<int>(m_queue.size()) >= m_queueLimit)
    {
        m_drops++;
        return false;
    }
    Waiting w;
    w.packet = pkt;
    w.control = (type == static_cast<uint8_t>(FrameType::Ack) || type == static_cast<uint8_t>(FrameType::Nack));
    m_queue.push_back(w);
    if (!m_timerRunning)
    {
        StartBackoff();
    }
    return true;
}

void
CsmaMac::StartBackoff()
{
    if (m_queue.empty())
    {
        m_timerRunning = false;
        return;
    }
    const Waiting& head = m_queue.front();
    int cw = head.control ? m_cwControl : m_cwMin;
    for (int i = 0; i < head.attempts && !head.control; ++i)
    {
        cw = std::min(cw * 2, m_cwMax);
    }
    const int slots = static_cast<int>(m_rand->GetInteger(0, cw - 1));
    m_timerRunning = true;
    Simulator::Schedule(Seconds(slots * m_slotS + 1e-4), &CsmaMac::BackoffDone, this);
}

void
CsmaMac::BackoffDone()
{
    if (m_queue.empty())
    {
        m_timerRunning = false;
        return;
    }
    switch (m_device->GetTransmissionStatus())
    {
    case SLEEP:
        PowerOn();
        break;
    case NIDLE: {
        Waiting w = m_queue.front();
        m_queue.pop_front();
        AquaSimHeader ash;
        w.packet->PeekHeader(ash);
        SendDown(w.packet);
        // The next frame starts its backoff once this one has left the modem.
        Simulator::Schedule(ash.GetTxTime() + Seconds(1e-4), &CsmaMac::StartBackoff, this);
        return;
    }
    default:
        break; // SEND or RECV: the channel is busy
    }
    m_deferrals++;
    Waiting& head = m_queue.front();
    head.attempts++;
    if (head.attempts >= m_maxAttempts)
    {
        m_queue.pop_front();
        m_drops++;
    }
    StartBackoff();
}

} // namespace cbergr
} // namespace ns3
