/*
 * localisation-noise.cc: Gaussian position/velocity error (spec §4.7).
 */

#include "localisation-noise.h"

#include "cb-ergr-params.h"

#include "ns3/double.h"

namespace ns3
{
namespace cbergr
{

NS_OBJECT_ENSURE_REGISTERED(LocalisationNoise);

TypeId
LocalisationNoise::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::LocalisationNoise")
            .SetParent<Object>()
            .AddConstructor<LocalisationNoise>()
            .AddAttribute("LocErrM",
                          "Position error std per axis (m).",
                          DoubleValue(params::mobility::kLocErrM),
                          MakeDoubleAccessor(&LocalisationNoise::m_locErrM),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("VelErrMps",
                          "Velocity error std per axis (m/s).",
                          DoubleValue(params::mobility::kVelErrMps),
                          MakeDoubleAccessor(&LocalisationNoise::m_velErrMps),
                          MakeDoubleChecker<double>(0.0));
    return tid;
}

LocalisationNoise::LocalisationNoise()
    : m_locErrM(params::mobility::kLocErrM),
      m_velErrMps(params::mobility::kVelErrMps)
{
    m_normal = CreateObject<NormalRandomVariable>(); // mean 0, variance 1
}

Vector
LocalisationNoise::NoisyPosition(const Vector& p)
{
    return Vector(p.x + m_locErrM * m_normal->GetValue(),
                  p.y + m_locErrM * m_normal->GetValue(),
                  p.z + m_locErrM * m_normal->GetValue());
}

Vector
LocalisationNoise::NoisyVelocity(const Vector& v)
{
    return Vector(v.x + m_velErrMps * m_normal->GetValue(),
                  v.y + m_velErrMps * m_normal->GetValue(),
                  v.z + m_velErrMps * m_normal->GetValue());
}

int64_t
LocalisationNoise::AssignStreams(int64_t stream)
{
    m_normal->SetStream(stream);
    return 1;
}

} // namespace cbergr
} // namespace ns3
