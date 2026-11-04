/*
 * tx-power-level-tag.h: per-frame transmit power level (spec §4.2).
 * The routing layer attaches it to a DATA frame; the channel reads it to set the source level of that frame
 * (SL[level] = SL_full + offset[level]) and the helper's PHY hook reads it to charge P_tx[level] for the energy.
 * Frames without the tag (beacons, ACKs, control) use the full level.
 */

#ifndef CB_ERGR_TX_POWER_LEVEL_TAG_H
#define CB_ERGR_TX_POWER_LEVEL_TAG_H

#include "ns3/tag.h"

#include <cstdint>

namespace ns3
{
namespace cbergr
{

class TxPowerLevelTag : public Tag
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override { return GetTypeId(); }
    TxPowerLevelTag() = default;
    explicit TxPowerLevelTag(uint8_t level)
        : m_level(level)
    {
    }

    uint32_t GetSerializedSize() const override { return 1; }
    void Serialize(TagBuffer i) const override { i.WriteU8(m_level); }
    void Deserialize(TagBuffer i) override { m_level = i.ReadU8(); }
    void Print(std::ostream& os) const override { os << "level=" << int(m_level); }

    uint8_t GetLevel() const { return m_level; }

  private:
    uint8_t m_level = 0;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_TX_POWER_LEVEL_TAG_H */
