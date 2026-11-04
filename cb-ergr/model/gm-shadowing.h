/*
 * gm-shadowing.h: time-correlated log-normal shadowing per link (spec §4.5).
 *
 * Each unordered node pair has a Gauss-Markov process in dB (std sigma, correlation
 * time tau). To keep runs reproducible even when an oracle peeks at the true
 * shadowing (spec §4.5 GetShadow for the oracle), the process is a PURE function of
 * (link, time): noise comes from a counter-based generator keyed by (run seed, link,
 * tick), not from a shared stream. Reading the shadowing at any time, in any order,
 * therefore never changes what the real packets experience.
 * The run seed is drawn once from an ns-3 RandomVariableStream, so AssignStreams()
 * and the usual --RngRun still control everything.
 */

#ifndef CB_ERGR_GM_SHADOWING_H
#define CB_ERGR_GM_SHADOWING_H

#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/random-variable-stream.h"

#include <cstdint>
#include <map>

namespace ns3
{
namespace cbergr
{

class GmShadowing : public Object
{
  public:
    static TypeId GetTypeId();
    GmShadowing();

    /// Shadowing in dB on the link (i, j) at time 'now'. Symmetric in (i, j).
    double GetShadowDb(uint32_t i, uint32_t j, Time now);

    /// Fix the base seed explicitly (tests). Normally drawn from the RNG stream on first use.
    void SetBaseSeed(uint64_t seed);

    /// Use ns-3 random stream numbers [stream, stream + 1); returns the number used.
    int64_t AssignStreams(int64_t stream);

  private:
    struct LinkState
    {
        int64_t tick;
        double shadowDb;
    };

    uint64_t LinkKey(uint32_t i, uint32_t j) const;
    double NoiseAt(uint64_t linkKey, int64_t tick) const;

    double m_sigmaDb;
    double m_tauS;
    double m_tickS;
    bool m_seeded = false;
    uint64_t m_baseSeed = 0;
    Ptr<UniformRandomVariable> m_seedStream;
    std::map<uint64_t, LinkState> m_links;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_GM_SHADOWING_H */
