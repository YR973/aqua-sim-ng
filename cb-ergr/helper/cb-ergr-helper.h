/*
 * cb-ergr-helper.h: one place that configures Aqua-Sim NG the way CB-ERGR needs it.
 * Used by every example and test, so they all share the same PHY, energy and channel settings
 * (all values come from cb-ergr-params.h and can be overridden through the attributes).
 */

#ifndef CB_ERGR_HELPER_H
#define CB_ERGR_HELPER_H

#include "ns3/aqua-sim-helper.h"
#include "ns3/aqua-sim-channel.h"
#include "ns3/aqua-sim-net-device.h"
#include "ns3/cb-ergr-propagation.h"
#include "ns3/metrics-collector.h"

namespace ns3
{
namespace cbergr
{

class CbErgrHelper
{
  public:
    /// Channel using the SNR-based propagation (spec §4.4). 'propagation' receives the model.
    static Ptr<AquaSimChannel> CreateChannel(Ptr<CbErgrPropagation>& propagation,
                                             double shadowSigmaDb,
                                             double shadowTauS);

    /// PHY + energy attributes (patches P1-P3 on, explicit energy values: spec §4.1, §4.3).
    /// Call before AquaSimHelper::Create().
    static void ConfigureHelper(AquaSimHelper& helper,
                                double preambleS,
                                double initialEnergyJ,
                                double txPowerFullW,
                                double rxPowerW,
                                double idlePowerW);

    /// Per-device settings that have no helper attribute (modulation bit rate).
    static void ConfigureDevice(Ptr<AquaSimNetDevice> device, double bitRateBps);

    /**
     * N-A header-first receive (D§4.2): a node that hears unicast DATA addressed to someone else pays receive
     * energy only for the preamble and the routing header. Beacons, ACKs and frames for this node are charged in full.
     */
    static void EnableHeaderFirstRx(Ptr<AquaSimNetDevice> device, uint16_t nodeId, double preambleS, double bitRateBps);

    /**
     * Per-frame transmit power (spec §4.2): the PHY charges P_tx[level] x airtime, with P_tx[level] =
     * txPowerFullW x TxPowerLevelScale[level], for frames carrying a TxPowerLevelTag; untagged frames use the full level.
     */
    static void EnablePowerLevels(Ptr<AquaSimNetDevice> device, double txPowerFullW);

    /// Receive-energy duration of one frame under N-A, or -1 for "the whole frame" (exposed for tests).
    static double HeaderFirstChargeS(Ptr<const Packet> frame, uint16_t nodeId, double preambleS, double bitRateBps);

    /// Send every link decision of the channel (PER losses) to the metrics collector.
    static void ConnectLinkTrace(Ptr<CbErgrPropagation> propagation, Ptr<MetricsCollector> metrics);

    /// Count PHY error events of one node (collisions, half-duplex) in the metrics collector.
    static void ConnectPhyTrace(Ptr<AquaSimNetDevice> device, Ptr<MetricsCollector> metrics, uint32_t nodeId);
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_HELPER_H */
