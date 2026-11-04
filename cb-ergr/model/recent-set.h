/*
 * recent-set.h: bounded "have I seen this packet?" filter (spec §7 recentlySeen: capacity 1024, TTL 600 s).
 */

#ifndef CB_ERGR_RECENT_SET_H
#define CB_ERGR_RECENT_SET_H

#include "forwarder-selector.h"

#include "ns3/nstime.h"

#include <deque>
#include <set>
#include <utility>

namespace ns3
{
namespace cbergr
{

class RecentSet
{
  public:
    RecentSet(size_t capacity, Time ttl)
        : m_capacity(capacity),
          m_ttl(ttl)
    {
    }

    /// Insert 'key'. Returns true if it was new (not seen within the TTL), false for a duplicate.
    bool Insert(const E2EKey& key, Time now)
    {
        while (!m_order.empty() && now - m_order.front().second > m_ttl)
        {
            m_keys.erase(m_order.front().first);
            m_order.pop_front();
        }
        if (m_keys.count(key) > 0)
        {
            return false;
        }
        m_keys.insert(key);
        m_order.emplace_back(key, now);
        if (m_order.size() > m_capacity)
        {
            m_keys.erase(m_order.front().first);
            m_order.pop_front();
        }
        return true;
    }

    size_t Size() const { return m_order.size(); }

  private:
    size_t m_capacity;
    Time m_ttl;
    std::deque<std::pair<E2EKey, Time>> m_order;
    std::set<E2EKey> m_keys;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_RECENT_SET_H */
