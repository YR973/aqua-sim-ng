/*
 * tx-power-level-tag.cc: registration of the per-frame power level tag (spec §4.2).
 */

#include "tx-power-level-tag.h"

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(TxPowerLevelTag);

TypeId
TxPowerLevelTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::cbergr::TxPowerLevelTag").SetParent<Tag>().AddConstructor<TxPowerLevelTag>();
    return tid;
}

} // namespace cbergr
} // namespace ns3
