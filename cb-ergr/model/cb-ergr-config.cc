/*
 * CB-ERGR v5 for ns-3 / Aqua-Sim NG.
 */

#include "cb-ergr-config.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CbErgrConfig");

namespace cbergr
{

std::string
GetSpecVersion()
{
    return "CB-ERGR v5 spec 1.0";
}

} // namespace cbergr
} // namespace ns3
