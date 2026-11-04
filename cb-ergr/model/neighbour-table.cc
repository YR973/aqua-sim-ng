/*
 * neighbour-table.cc: G1 table maintenance (spec §7.1).
 */

#include "neighbour-table.h"

namespace ns3
{
namespace cbergr
{

TableUpdateResult
NeighbourTable::Update(const NeighbourEntry& entry)
{
    TableUpdateResult result;
    auto it = m_rows.find(entry.id);
    if (it == m_rows.end())
    {
        NeighbourEntry fresh = entry;
        fresh.firstHeard = entry.beaconTime;
        m_rows.emplace(entry.id, fresh);
        result.isNew = true;
        return result;
    }
    NeighbourEntry& row = it->second;
    result.headStatusChanged = (row.isHead != entry.isHead) || (row.headRank != entry.headRank);
    result.cellChanged = row.cellId != entry.cellId;

    Time firstHeard = row.firstHeard;
    row = entry;
    row.firstHeard = firstHeard;
    // K§2.3: entry age restarts when the node becomes a head.
    if (result.headStatusChanged && entry.isHead)
    {
        row.firstHeard = entry.beaconTime;
    }
    return result;
}

std::vector<uint16_t>
NeighbourTable::Expire(Time now, Time timeout)
{
    std::vector<uint16_t> removed;
    for (auto it = m_rows.begin(); it != m_rows.end();)
    {
        if (now - it->second.beaconTime > timeout)
        {
            removed.push_back(it->first);
            it = m_rows.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return removed;
}

const NeighbourEntry*
NeighbourTable::Find(uint16_t id) const
{
    auto it = m_rows.find(id);
    return it == m_rows.end() ? nullptr : &it->second;
}

NeighbourEntry*
NeighbourTable::FindMutable(uint16_t id)
{
    auto it = m_rows.find(id);
    return it == m_rows.end() ? nullptr : &it->second;
}

} // namespace cbergr
} // namespace ns3
