/*
 * selector-oracle.h: "O" and "GO" (spec §11; kickoff §2.4). With true positions (O: and true shadowing) it picks the
 * eligible forwarder with the best expected utility
 *     U(j) = successTrue(j) * (1 - KappaP + KappaP * x2),
 * the benchmark for regret. They are controllers with perfect knowledge of the channel, not protocols a real
 * node could run. AckRequest is on (same feedback as the other methods) so retries behave identically.
 */

#ifndef CB_ERGR_SELECTOR_ORACLE_H
#define CB_ERGR_SELECTOR_ORACLE_H

#include "forwarder-selector.h"

namespace ns3
{
namespace cbergr
{

class SelectorOracle : public ForwarderSelector
{
  public:
    static TypeId GetTypeId();
    void SetServices(const SelectorServices& services) override { m_services = services; }
    void SetOracle(Ptr<OracleEvaluator> oracle) override { m_oracle = oracle; }

    // --- Selection ---
    Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) override;

  private:
    SelectorServices m_services;
    Ptr<OracleEvaluator> m_oracle;
    bool m_geoOnly;     ///< GO: ignore shadowing
    double m_kappaP;
    bool m_requestAck;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SELECTOR_ORACLE_H */
