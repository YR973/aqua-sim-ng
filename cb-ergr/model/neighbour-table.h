/*
 * neighbour-table.h: the G1 table (spec §5.1, §7.1): what a node knows about nodes it heard beacons from.
 * The routing layer filters it to heads of neighbouring cells (and sinks) when it needs candidates;
 * the election uses the rows of the node's own cell. A row is removed when its beacon is older
 * than the neighbour timeout (3 beacon periods).
 */

#ifndef CB_ERGR_NEIGHBOUR_TABLE_H
#define CB_ERGR_NEIGHBOUR_TABLE_H

#include "ns3/nstime.h"
#include "ns3/vector.h"

#include <cstdint>
#include <map>
#include <vector>

namespace ns3
{
namespace cbergr
{

/// One row of the table. Positions and velocities are the (noisy) values the node put in its beacon.
struct NeighbourEntry
{
    uint16_t id = 0;
    uint16_t cellId = 0;
    bool isHead = false;
    uint8_t headRank = 0;
    bool isSink = false;
    uint8_t hmin = 255;
    Vector pos;               ///< position at beaconTime (m)
    Vector vel;               ///< velocity (m/s)
    Time beaconTime;          ///< when the latest beacon was received
    Time firstHeard;          ///< when the node entered the table; reset when it becomes head (K§2.3)
    double battery = 1.0;     ///< residual energy fraction 0..1
    uint8_t queueLen = 0;
    Time nextBeaconIn;        ///< interval it advertised (N-C)
    bool hasQ = false;        ///< the node advertised a state value (Q-learning methods)
    double advertisedQ = 0.0; ///< that value
    double lastSnrDb = 0.0;   ///< measured SNR of its last beacon (MarginFromRssi, K§2.3)
    bool hasSnr = false;

    /// Position extrapolated to 'now' with the advertised velocity (spec §8.1).
    Vector PredictedPos(Time now) const
    {
        double dt = (now - beaconTime).GetSeconds();
        return Vector(pos.x + vel.x * dt, pos.y + vel.y * dt, pos.z + vel.z * dt);
    }
};

/// What changed when a beacon was applied (drives election and known-change events).
struct TableUpdateResult
{
    bool isNew = false;           ///< node was not in the table
    bool headStatusChanged = false; ///< isHead or headRank differs from the previous row
    bool cellChanged = false;
};

class NeighbourTable
{
  public:
    /// Insert or refresh a row from a received beacon. 'entry.beaconTime' must be set by the caller.
    TableUpdateResult Update(const NeighbourEntry& entry);

    /// Remove rows whose last beacon is older than max(timeout, multiplier x the interval the node advertised).
    /// Returns the removed ids.
    std::vector<uint16_t> Expire(Time now, Time timeout, double multiplier = 0.0);

    /// Row for a node, or nullptr.
    const NeighbourEntry* Find(uint16_t id) const;
    NeighbourEntry* FindMutable(uint16_t id);

    void Remove(uint16_t id) { m_rows.erase(id); }

    const std::map<uint16_t, NeighbourEntry>& Rows() const { return m_rows; }
    size_t Size() const { return m_rows.size(); }

  private:
    std::map<uint16_t, NeighbourEntry> m_rows; // ordered by id, so iteration is deterministic
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_NEIGHBOUR_TABLE_H */
