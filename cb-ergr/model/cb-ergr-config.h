/*
 * CB-ERGR v5 for ns-3 / Aqua-Sim NG.
 * Author: Yousef Roudani (MSc thesis, SQU Dept. of Computer Science).
 *
 * Module-wide constants shared by all CB-ERGR components.
 */

#ifndef CB_ERGR_CONFIG_H
#define CB_ERGR_CONFIG_H

#include <string>

namespace ns3
{
namespace cbergr
{

/// Spec version implemented by this module.
std::string GetSpecVersion();

/// Feature dimension of the contextual-bandit model (spec section 8.1).
static constexpr int kFeatureDim = 9;

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_CONFIG_H */
