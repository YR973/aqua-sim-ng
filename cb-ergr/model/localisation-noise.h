/*
 * localisation-noise.h: imperfect self-localisation (spec §4.7).
 * The position and velocity a node puts in its beacon, and uses for its own distance predictions,
 * are the true values plus Gaussian noise (20 m per axis, 0.1 m/s). The oracles use true positions.
 */

#ifndef CB_ERGR_LOCALISATION_NOISE_H
#define CB_ERGR_LOCALISATION_NOISE_H

#include "ns3/object.h"
#include "ns3/random-variable-stream.h"
#include "ns3/vector.h"

namespace ns3
{
namespace cbergr
{

class LocalisationNoise : public Object
{
  public:
    static TypeId GetTypeId();
    LocalisationNoise();

    /// True position plus N(0, LocErrM^2) per axis.
    Vector NoisyPosition(const Vector& truePos);

    /// True velocity plus N(0, VelErrMps^2) per axis.
    Vector NoisyVelocity(const Vector& trueVel);

    /// Uses 1 ns-3 random stream; returns how many.
    int64_t AssignStreams(int64_t stream);

  private:
    double m_locErrM;
    double m_velErrMps;
    Ptr<NormalRandomVariable> m_normal;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_LOCALISATION_NOISE_H */
