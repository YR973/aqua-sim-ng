/*
 * current-drift-mobility.h: nodes carried by an ocean current (spec §4.6).
 *
 * Velocity = meandering horizontal current field (speed scale U = CurrentSpeed)
 *          + Ornstein-Uhlenbeck perturbation per node
 *          + vertical drift of 0.1 U.
 * Nodes reflect at the walls of the box. U = 0 gives static nodes.
 *
 * ASSUMPTION (CONFIRM E10): the pilot's current() field was not available, so the field here is a
 * simple meander, vx = U cos(2 pi y / L + phi(t)), vy = 0.5 U sin(2 pi x / L + phi(t)), with
 * phi(t) = 0.5 sin(2 pi t / T). Same speed scale and statistics as the spec describes, different shape.
 *
 * Determinism: the state advances on a fixed timer, never on access, and position is extrapolated
 * linearly between steps, so peeking at a position (oracles, metrics) cannot change the run.
 */

#ifndef CB_ERGR_CURRENT_DRIFT_MOBILITY_H
#define CB_ERGR_CURRENT_DRIFT_MOBILITY_H

#include "ns3/mobility-model.h"
#include "ns3/nstime.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{
namespace cbergr
{

class CurrentDriftMobility : public MobilityModel
{
  public:
    static TypeId GetTypeId();
    CurrentDriftMobility();

    /// Uses 2 ns-3 random streams; returns how many.
    int64_t DoAssignStreams(int64_t stream) override;

  private:
    Vector DoGetPosition() const override;
    void DoSetPosition(const Vector& position) override;
    Vector DoGetVelocity() const override;

    void Step();                                  ///< advance the state by one update interval
    Vector CurrentField(const Vector& p, double tS) const;
    void ReflectIntoBox();

    double m_currentSpeedMps;
    double m_boxSizeM;
    double m_updateIntervalS;
    double m_ouTauS;
    double m_ouSigmaFraction;
    double m_meanderPeriodS;
    double m_verticalDriftFactor;

    Vector m_pos;            ///< position at m_lastStep
    Vector m_vel;            ///< velocity during the current step
    Vector m_ou;             ///< Ornstein-Uhlenbeck part of the velocity
    double m_verticalPhase;  ///< per-node phase of the vertical drift
    Time m_lastStep;
    bool m_running = false;
    Ptr<NormalRandomVariable> m_normal;
    Ptr<UniformRandomVariable> m_uniform;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_CURRENT_DRIFT_MOBILITY_H */
