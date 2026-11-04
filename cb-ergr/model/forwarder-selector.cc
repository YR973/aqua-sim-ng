/*
 * forwarder-selector.cc: the abstract base of all forwarding methods and its shared helper.
 * Implements the interface of spec §5.3 and the distance prediction of §8.1.
 */

#include "forwarder-selector.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(ForwarderSelector);

TypeId
ForwarderSelector::GetTypeId()
{
    static TypeId tid = TypeId("ns3::cbergr::ForwarderSelector").SetParent<Object>();
    return tid;
}

double
ForwarderSelector::PredictedDistance(const PacketCtx& pkt, const NeighbourEntry& nb, Time now)
{
    Vector predicted = nb.PredictedPos(now);
    double dx = predicted.x - pkt.myPos.x;
    double dy = predicted.y - pkt.myPos.y;
    double dz = predicted.z - pkt.myPos.z;
    return std::max(1.0, std::sqrt(dx * dx + dy * dy + dz * dz));
}

} // namespace cbergr
} // namespace ns3
