/*
 * current-drift-mobility.cc: ocean-current drift with OU perturbation and wall reflection (spec §4.6).
 */

#include "current-drift-mobility.h"

#include "cb-ergr-params.h"

#include "ns3/double.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(CurrentDriftMobility);

TypeId
CurrentDriftMobility::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::CurrentDriftMobility")
            .SetParent<MobilityModel>()
            .AddConstructor<CurrentDriftMobility>()
            .AddAttribute("CurrentSpeed",
                          "Speed scale U of the current (m/s).",
                          DoubleValue(params::mobility::kCurrentSpeedMps),
                          MakeDoubleAccessor(&CurrentDriftMobility::m_currentSpeedMps),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("BoxSizeM",
                          "Side of the cubic box (m); nodes reflect at its walls.",
                          DoubleValue(params::grid::kBoxSizeM),
                          MakeDoubleAccessor(&CurrentDriftMobility::m_boxSizeM),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("UpdateIntervalS",
                          "Integration step (s).",
                          DoubleValue(params::mobility::kUpdateIntervalS),
                          MakeDoubleAccessor(&CurrentDriftMobility::m_updateIntervalS),
                          MakeDoubleChecker<double>(0.01))
            .AddAttribute("OuTauS",
                          "Correlation time of the per-node velocity perturbation (s).",
                          DoubleValue(params::mobility::kOuTauS),
                          MakeDoubleAccessor(&CurrentDriftMobility::m_ouTauS),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("OuSigmaFraction",
                          "Std of the perturbation per axis, as a fraction of CurrentSpeed.",
                          DoubleValue(params::mobility::kOuSigmaFraction),
                          MakeDoubleAccessor(&CurrentDriftMobility::m_ouSigmaFraction),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("MeanderPeriodS",
                          "Period of the meander and of the vertical drift (s).",
                          DoubleValue(params::mobility::kMeanderPeriodS),
                          MakeDoubleAccessor(&CurrentDriftMobility::m_meanderPeriodS),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("VerticalDriftFactor",
                          "Vertical drift amplitude as a fraction of CurrentSpeed.",
                          DoubleValue(params::mobility::kVerticalDriftFactor),
                          MakeDoubleAccessor(&CurrentDriftMobility::m_verticalDriftFactor),
                          MakeDoubleChecker<double>(0.0));
    return tid;
}

CurrentDriftMobility::CurrentDriftMobility()
    : m_currentSpeedMps(params::mobility::kCurrentSpeedMps),
      m_boxSizeM(params::grid::kBoxSizeM),
      m_updateIntervalS(params::mobility::kUpdateIntervalS),
      m_ouTauS(params::mobility::kOuTauS),
      m_ouSigmaFraction(params::mobility::kOuSigmaFraction),
      m_meanderPeriodS(params::mobility::kMeanderPeriodS),
      m_verticalDriftFactor(params::mobility::kVerticalDriftFactor),
      m_verticalPhase(0.0)
{
    m_normal = CreateObject<NormalRandomVariable>();
    m_uniform = CreateObject<UniformRandomVariable>();
}

int64_t
CurrentDriftMobility::DoAssignStreams(int64_t stream)
{
    m_normal->SetStream(stream);
    m_uniform->SetStream(stream + 1);
    return 2;
}

Vector
CurrentDriftMobility::CurrentField(const Vector& p, double tS) const
{
    const double twoPi = 2.0 * M_PI;
    double phase = 0.5 * std::sin(twoPi * tS / m_meanderPeriodS);
    double vx = m_currentSpeedMps * std::cos(twoPi * p.y / m_boxSizeM + phase);
    double vy = 0.5 * m_currentSpeedMps * std::sin(twoPi * p.x / m_boxSizeM + phase);
    double vz = m_verticalDriftFactor * m_currentSpeedMps *
                std::sin(twoPi * tS / m_meanderPeriodS + m_verticalPhase);
    return Vector(vx, vy, vz);
}

void
CurrentDriftMobility::DoSetPosition(const Vector& position)
{
    m_pos = position;
    m_lastStep = Simulator::Now();
    if (!m_running)
    {
        m_running = true;
        m_verticalPhase = 2.0 * M_PI * m_uniform->GetValue();
        m_vel = CurrentField(m_pos, Simulator::Now().GetSeconds());
        if (m_currentSpeedMps > 0.0)
        {
            Simulator::Schedule(Seconds(m_updateIntervalS), &CurrentDriftMobility::Step, this);
        }
    }
    NotifyCourseChange();
}

Vector
CurrentDriftMobility::DoGetPosition() const
{
    double dt = (Simulator::Now() - m_lastStep).GetSeconds();
    return Vector(m_pos.x + m_vel.x * dt, m_pos.y + m_vel.y * dt, m_pos.z + m_vel.z * dt);
}

Vector
CurrentDriftMobility::DoGetVelocity() const
{
    return m_vel;
}

void
CurrentDriftMobility::ReflectIntoBox()
{
    double* coords[3] = {&m_pos.x, &m_pos.y, &m_pos.z};
    double* speeds[3] = {&m_vel.x, &m_vel.y, &m_vel.z};
    for (int axis = 0; axis < 3; ++axis)
    {
        double& c = *coords[axis];
        while (c < 0.0 || c > m_boxSizeM)
        {
            c = (c < 0.0) ? -c : 2.0 * m_boxSizeM - c;
            *speeds[axis] = -*speeds[axis];
            // the OU part flips too, so the node keeps moving away from the wall
            if (axis == 0) m_ou.x = -m_ou.x;
            if (axis == 1) m_ou.y = -m_ou.y;
            if (axis == 2) m_ou.z = -m_ou.z;
        }
    }
}

void
CurrentDriftMobility::Step()
{
    double dt = m_updateIntervalS;
    double now = Simulator::Now().GetSeconds();

    // 1. move with the velocity of the step that just ended
    m_pos = DoGetPosition();
    ReflectIntoBox();

    // 2. Ornstein-Uhlenbeck update of the perturbation
    double decay = std::exp(-dt / m_ouTauS);
    double innovation = m_ouSigmaFraction * m_currentSpeedMps * std::sqrt(1.0 - decay * decay);
    m_ou.x = m_ou.x * decay + innovation * m_normal->GetValue();
    m_ou.y = m_ou.y * decay + innovation * m_normal->GetValue();
    m_ou.z = m_ou.z * decay + innovation * m_normal->GetValue();

    // 3. new velocity for the next step: current + perturbation
    Vector field = CurrentField(m_pos, now);
    m_vel = Vector(field.x + m_ou.x, field.y + m_ou.y, field.z + m_ou.z);
    m_lastStep = Simulator::Now();

    NotifyCourseChange();
    Simulator::Schedule(Seconds(dt), &CurrentDriftMobility::Step, this);
}

} // namespace cbergr
} // namespace ns3
