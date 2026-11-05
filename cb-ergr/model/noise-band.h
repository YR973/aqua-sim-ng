/*
 * noise-band.h: extra ambient noise inside a slab of the box (a "shipping lane"), the heterogeneous scenario of 5 Nov 2026.
 *
 * Receivers whose x lies in [xMinM, xMaxM) hear offsetDb more noise, i.e. every frame they receive has offsetDb lower SNR.
 * The transmitter's own noise does not change. A forwarder can therefore NOT see the band from the beacon SNR it measures
 * (that SNR is measured at ITS position, in the other direction): the band is invisible to the features of FW and CB and
 * is known only to the channel and the oracle. offsetDb = 0 switches it off.
 */

#ifndef CB_ERGR_NOISE_BAND_H
#define CB_ERGR_NOISE_BAND_H

#include "ns3/vector.h"

namespace ns3
{
namespace cbergr
{

struct NoiseBand
{
    double xMinM = 0.0;
    double xMaxM = 0.0;
    double offsetDb = 0.0;
    double startS = 0.0;    ///< the band is on from this time ...
    double endS = -1.0;     ///< ... until this time (negative = until the end): a noise BURST (7 Nov 2026, change scenario)

    /// Change of the link SNR (dB, <= 0) for a frame received at 'rxPos' at time 'nowS'.
    double SnrChangeDb(const Vector& rxPos, double nowS = 0.0) const
    {
        const bool on = nowS >= startS && (endS < 0.0 || nowS < endS);
        return (offsetDb != 0.0 && on && rxPos.x >= xMinM && rxPos.x < xMaxM) ? -offsetDb : 0.0;
    }
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_NOISE_BAND_H */
