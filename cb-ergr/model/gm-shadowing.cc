/*
 * gm-shadowing.cc: Gauss-Markov shadowing, s <- rho*s + sigma*sqrt(1-rho^2)*N(0,1),
 * rho = exp(-dt/tau), evaluated on a fixed time grid (spec §4.5).
 */

#include "gm-shadowing.h"

#include "cb-ergr-params.h"

#include "ns3/double.h"
#include "ns3/log.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace cbergr
{

NS_LOG_COMPONENT_DEFINE("CbErgrGmShadowing");
NS_OBJECT_ENSURE_REGISTERED(GmShadowing);

namespace
{
// splitmix64: a well-mixed 64-bit hash step, used as a counter-based generator.
uint64_t
SplitMix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Uniform in (0, 1] from a hash.
double
UnitFromHash(uint64_t h)
{
    return (static_cast<double>(h >> 11) + 1.0) / 9007199254740992.0; // 2^53
}
} // namespace

TypeId
GmShadowing::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::GmShadowing")
            .SetParent<Object>()
            .AddConstructor<GmShadowing>()
            .AddAttribute("ShadowSigmaDb",
                          "Standard deviation of the shadowing (dB).",
                          DoubleValue(params::channel::kShadowSigmaDb),
                          MakeDoubleAccessor(&GmShadowing::m_sigmaDb),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("ShadowTauS",
                          "Correlation time of the shadowing (s).",
                          DoubleValue(params::channel::kShadowTauS),
                          MakeDoubleAccessor(&GmShadowing::m_tauS),
                          MakeDoubleChecker<double>(1e-6))
            .AddAttribute("ShadowTickS",
                          "Time grid of the process (s). Must be well below ShadowTauS.",
                          DoubleValue(params::channel::kShadowTickS),
                          MakeDoubleAccessor(&GmShadowing::m_tickS),
                          MakeDoubleChecker<double>(1e-6));
    return tid;
}

GmShadowing::GmShadowing()
    : m_sigmaDb(params::channel::kShadowSigmaDb),
      m_tauS(params::channel::kShadowTauS),
      m_tickS(params::channel::kShadowTickS)
{
    m_seedStream = CreateObject<UniformRandomVariable>();
}

void
GmShadowing::SetBaseSeed(uint64_t seed)
{
    m_baseSeed = seed;
    m_seeded = true;
}

int64_t
GmShadowing::AssignStreams(int64_t stream)
{
    m_seedStream->SetStream(stream);
    return 1;
}

uint64_t
GmShadowing::LinkKey(uint32_t i, uint32_t j) const
{
    uint32_t lo = std::min(i, j);
    uint32_t hi = std::max(i, j);
    return (static_cast<uint64_t>(lo) << 32) | hi;
}

// One N(0,1) draw for (link, tick) via Box-Muller on two hashed uniforms.
double
GmShadowing::NoiseAt(uint64_t linkKey, int64_t tick) const
{
    uint64_t h = SplitMix64(m_baseSeed ^ SplitMix64(linkKey));
    h = SplitMix64(h ^ static_cast<uint64_t>(tick));
    double u1 = UnitFromHash(SplitMix64(h));
    double u2 = UnitFromHash(SplitMix64(h ^ 0x5555555555555555ULL));
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
}

double
GmShadowing::GetShadowDb(uint32_t i, uint32_t j, Time now)
{
    if (m_sigmaDb <= 0.0)
    {
        return 0.0;
    }
    if (!m_seeded)
    {
        m_baseSeed = (static_cast<uint64_t>(m_seedStream->GetInteger(0, 0x7fffffff)) << 32) |
                     m_seedStream->GetInteger(0, 0x7fffffff);
        m_seeded = true;
    }
    uint64_t key = LinkKey(i, j);
    int64_t tick = static_cast<int64_t>(std::floor(now.GetSeconds() / m_tickS));

    auto it = m_links.find(key);
    if (it == m_links.end())
    {
        // Every chain starts at tick 0 from the stationary distribution, whatever time it is
        // first asked about, so the path never depends on when anyone first looked.
        LinkState fresh{0, m_sigmaDb * NoiseAt(key, 0)};
        it = m_links.emplace(key, fresh).first;
    }
    LinkState& st = it->second;
    if (tick < st.tick)
    {
        // Asked about an earlier time than we have advanced to (not expected in a
        // forward-running simulation): return the latest value.
        return st.shadowDb;
    }
    double rho = std::exp(-m_tickS / m_tauS);
    double innovationStd = m_sigmaDb * std::sqrt(1.0 - rho * rho);
    for (int64_t k = st.tick + 1; k <= tick; ++k)
    {
        st.shadowDb = rho * st.shadowDb + innovationStd * NoiseAt(key, k);
    }
    st.tick = tick;
    return st.shadowDb;
}

} // namespace cbergr
} // namespace ns3
