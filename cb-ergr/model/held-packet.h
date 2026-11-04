/*
 * held-packet.h: what a node needs in order to (re)route one data packet.
 * The payload itself is regenerated for every transmission (it is just zeros), so only the
 * routing state is kept: identity, hop budget, who sent it to us, and which forwarders to avoid.
 */

#ifndef CB_ERGR_HELD_PACKET_H
#define CB_ERGR_HELD_PACKET_H

#include "forwarder-selector.h"

#include "ns3/event-id.h"

#include <cstdint>
#include <set>

namespace ns3
{
namespace cbergr
{

struct HeldPacket
{
    E2EKey key;
    uint32_t genTimeMs = 0;
    uint8_t hpkt = 0;
    int32_t prevHop = -1;          ///< node that sent it to us, -1 if we are the source
    std::set<uint16_t> excluded;   ///< forwarders that answered with a void NACK
    uint8_t voidRetries = 0;
    uint8_t attempt = 0;           ///< transmissions of this packet made by this node so far
    uint16_t lastForwarder = 0;
    uint16_t lastHopSeq = 0;
    EventId expiry;                ///< NACK-window timer
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_HELD_PACKET_H */
