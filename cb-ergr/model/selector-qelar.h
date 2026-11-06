/*
 * selector-qelar.h: "QELAR-G", QELAR (Hu & Fei, IEEE Trans. Mobile Computing 9(6), 796-809, 2010) on the ERGR-EMHC-g candidate set.
 *
 *   Q_i(j) = P_j [ r_s(i, j) + gamma V_j ] + (1 - P_j) [ r_f(i) + gamma V_i ]           (model-based, as in QELAR)
 *   r_s = -g - a1 (c_i + c_j) + a2 (d_i + d_j),   r_f = -g - b1 c_i + b2 d_i
 *   c(n) = 1 - E_res(n) / E_init,   d(n) = (2/pi) atan(E_res(n) - mean E_res of n's group)
 *   V_i = max_j Q_i(j); the forwarder is argmax_j Q_i(j); a sink is terminal (V = 0).
 * P_j is estimated per neighbour from the node's own labels (Beta prior). V_j comes from j's beacon (head beacons carry it, +2 B) or from
 * the ACKs j sends to this node (+2 B); the fresher one is used.
 * Deviations from the paper (stated in CONFIRM E50): no overhearing (the paper learns V from overheard packet headers; the design rules
 * forbid that), the candidates and the ACK/MaxTx/eligibility rules are those of ERGR-EMHC-g, energies of neighbours come from their beacon
 * battery field (1/255 resolution), and the numeric defaults are my choices because the paper could not be read.
 */

#ifndef CB_ERGR_SELECTOR_QELAR_H
#define CB_ERGR_SELECTOR_QELAR_H

#include "forwarder-selector.h"

#include <map>

namespace ns3
{
namespace cbergr
{

class SelectorQelar : public ForwarderSelector
{
  public:
    static TypeId GetTypeId();
    SelectorQelar();

    void SetServices(const SelectorServices& services) override { m_services = services; }
    Decision Select(const PacketCtx& pkt, const std::vector<Candidate>& eligible, Time now) override;
    void OnLabel(const Decision& d, int y, LabelSource src, Time now) override;
    void OnBeaconTx(ErgrBeaconHeader& beacon, Time now) override;
    bool AckValue(double& v) const override;
    void OnAckValue(uint16_t ackerId, double v, Time now) override;
    void Diagnostics(std::map<std::string, double>& out) const override;

    /// Q of one candidate given the current estimates (exposed for the tests).
    double QValue(const PacketCtx& pkt, const NeighbourEntry& nb, double groupMeanJ, Time now) const;
    double SuccessEstimate(uint16_t id) const;
    double ValueOf(const NeighbourEntry& nb) const;
    double VSelf() const { return m_vSelf; }

  private:
    double m_g, m_a1, m_a2, m_b1, m_b2, m_gamma, m_priorS, m_priorF;
    SelectorServices m_services;
    struct Counts
    {
        double s = 0.0, f = 0.0;
    };
    std::map<uint16_t, Counts> m_counts;
    struct AckV
    {
        double v = 0.0;
        Time at;
    };
    std::map<uint16_t, AckV> m_ackV;
    double m_vSelf = 0.0;
    uint64_t m_labels = 0;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SELECTOR_QELAR_H */
