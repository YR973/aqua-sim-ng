/*
 * serial-number.h: 16-bit sequence-number arithmetic (RFC 1982), spec §6.1/§7.2.
 *
 * hopSeq is a uint16 that wraps from 65535 to 0, so "a is newer than b"
 * cannot be a plain a > b. RFC 1982 says: a is newer than b when the
 * forward distance from b to a (mod 2^16) is between 1 and 2^15 - 1.
 * Plain C++, no ns-3 dependency.
 */

#ifndef CB_ERGR_SERIAL_NUMBER_H
#define CB_ERGR_SERIAL_NUMBER_H

#include <cstdint>

namespace ns3
{
namespace cbergr
{

/// Forward distance from b to a, modulo 2^16 (0 .. 65535).
inline uint16_t
SerialForward(uint16_t a, uint16_t b)
{
    return static_cast<uint16_t>(a - b);
}

/// RFC 1982: true if a is strictly newer than b. Distance exactly 2^15 is undefined and gives false.
inline bool
SerialGreater(uint16_t a, uint16_t b)
{
    uint16_t forward = SerialForward(a, b);
    return forward != 0 && forward < 0x8000;
}

/// Signed distance a - b in the range -32768 .. 32767 (positive when a is newer).
inline int
SerialDiff(uint16_t a, uint16_t b)
{
    return static_cast<int16_t>(SerialForward(a, b));
}

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SERIAL_NUMBER_H */
