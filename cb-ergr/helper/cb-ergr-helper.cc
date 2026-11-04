/*
 * cb-ergr-helper.cc: channel/PHY/energy configuration shared by all CB-ERGR scenarios.
 * Implements the set-up parts of spec §4.1 (preamble), §4.3 (explicit energy) and §4.4 (SNR channel).
 */

#include "cb-ergr-helper.h"

#include "ns3/aqua-sim-phy-cmn.h"
#include "ns3/boolean.h"
#include "ns3/cb-ergr-params.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/uinteger.h"

namespace ns3
{
namespace cbergr
{

Ptr<AquaSimChannel>
CbErgrHelper::CreateChannel(Ptr<CbErgrPropagation>& propagation,
                            double shadowSigmaDb,
                            double shadowTauS)
{
    AquaSimChannelHelper channelHelper = AquaSimChannelHelper::Default();
    channelHelper.SetPropagation("ns3::cbergr::CbErgrPropagation");
    Ptr<AquaSimChannel> channel = channelHelper.Create();

    PointerValue pv;
    channel->GetAttribute("SetProp", pv);
    propagation = pv.Get<CbErgrPropagation>();
    propagation->GetShadowing()->SetAttribute("ShadowSigmaDb", DoubleValue(shadowSigmaDb));
    propagation->GetShadowing()->SetAttribute("ShadowTauS", DoubleValue(shadowTauS));
    return channel;
}

void
CbErgrHelper::ConfigureHelper(AquaSimHelper& helper,
                              double preambleS,
                              double initialEnergyJ,
                              double txPowerFullW,
                              double rxPowerW,
                              double idlePowerW)
{
    helper.SetPhy("ns3::AquaSimPhyCmn",
                  "Preamble", DoubleValue(preambleS),
                  "ExactPreamble", BooleanValue(true),            // patch P1
                  "RxEnergyUsesAirtime", BooleanValue(true),      // patch P2
                  "KeepEnergyTxPower", BooleanValue(true),        // patch P3
                  "PT", DoubleValue(params::phy::kReferenceAcousticPtW),
                  "RXThresh", DoubleValue(params::phy::kRxThreshold));
    helper.SetEnergyModel("ns3::AquaSimEnergyModel",
                          "InitialEnergy", DoubleValue(initialEnergyJ),
                          "TxPower", DoubleValue(txPowerFullW),
                          "RxPower", DoubleValue(rxPowerW),
                          "IdlePower", DoubleValue(idlePowerW));
}

void
CbErgrHelper::ConfigureDevice(Ptr<AquaSimNetDevice> device, double bitRateBps)
{
    Ptr<AquaSimPhyCmn> phy = DynamicCast<AquaSimPhyCmn>(device->GetPhy());
    NS_ASSERT_MSG(phy, "CB-ERGR needs AquaSimPhyCmn");
    // Bps() = SPS / CodingEff, with CodingEff = 1 bit/symbol.
    phy->Modulation(nullptr)->SetAttribute("SPS", UintegerValue(static_cast<uint32_t>(bitRateBps)));
}

} // namespace cbergr
} // namespace ns3
