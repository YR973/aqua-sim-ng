/*
 * cb-ergr-propagation.h: SNR-based reception for Aqua-Sim NG (spec §4.4).
 *
 * Upstream propagation gives every node in range a copy of every frame, so success
 * does not depend on distance or transmit power. This subclass decides, per receiver,
 * whether the frame survives the link:
 *     SNR = SL(Pt) - TL(d) - NL + shadow(i, j, t)   ->  PER (BFSK)  ->  draw U(0,1)
 * and reports the result to the PHY through the received power field:
 *     pR = 1 (frame decodable)  or  pR = 0 (lost to noise).
 * The PHY must be configured with RXThresh = 0.5 so that pR = 0 is treated as
 * "below the receive threshold": the frame still occupies the channel (it can
 * collide with other frames and costs receive energy) but is not delivered.
 * Collisions and half-duplex remain Aqua-Sim's own logic.
 */

#ifndef CB_ERGR_PROPAGATION_H
#define CB_ERGR_PROPAGATION_H

#include "acoustic-link-model.h"
#include "gm-shadowing.h"

#include "ns3/aqua-sim-propagation.h"
#include "ns3/random-variable-stream.h"
#include "ns3/traced-callback.h"

#include <functional>
#include <memory>

namespace ns3
{
namespace cbergr
{

class CbErgrPropagation : public AquaSimPropagation
{
  public:
    static TypeId GetTypeId();
    CbErgrPropagation();

    std::vector<PktRecvUnit>* ReceivedCopies(Ptr<AquaSimNetDevice> s,
                                             Ptr<Packet> p,
                                             std::vector<Ptr<AquaSimNetDevice>> dList) override;

    void SetTraceValues(double, double, double) override {}
    void SetTraceValues(double, double, double, double, double) override {}

    /// Link budget used for all decisions (built from the attributes on first use).
    const AcousticLinkModel& GetLinkModel();

    /// Shadowing process, exposed so oracles can read the true value (spec §4.5).
    Ptr<GmShadowing> GetShadowing() { return m_shadowing; }

    /// Uses 2 ns-3 random streams; returns how many.
    int64_t AssignStreams(int64_t stream);

    /// Test hook: return true to lose a frame (sender node, receiver node, frame type).
    void SetFrameFilter(std::function<bool(uint32_t, uint32_t, uint8_t)> filter) { m_filter = filter; }

  private:
    void EnsureLinkModel();

    // Attributes
    double m_rangeM;
    double m_carrierKHz;
    double m_bandwidthHz;
    double m_bitRateBps;
    double m_spreadingK;
    double m_shippingActivity;
    double m_windSpeedMps;
    double m_referencePtW;
    double m_calibFrameBytes;
    double m_extraLossProbability;
    std::function<bool(uint32_t, uint32_t, uint8_t)> m_filter;
    Ptr<UniformRandomVariable> m_extraLossDraw;

    std::unique_ptr<AcousticLinkModel> m_link;
    Ptr<GmShadowing> m_shadowing;
    Ptr<UniformRandomVariable> m_lossDraw;

    /// (sender node, receiver node, distance m, SNR dB, PER, frame decodable?, frame bytes, frame type)
    TracedCallback<uint32_t, uint32_t, double, double, double, bool, uint32_t, uint8_t> m_linkEval;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_PROPAGATION_H */
