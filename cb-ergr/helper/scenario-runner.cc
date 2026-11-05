/*
 * scenario-runner.cc: builds nodes, mobility, channel, routing and metrics, runs, and finalises
 * (kickoff §4 D3). Nothing here defines a parameter default: see cb-ergr-params.h.
 */

#include "scenario-runner.h"

#include "ns3/aqua-sim-ng-module.h"
#include "ns3/cb-ergr-helper.h"
#include "ns3/core-module.h"
#include "ns3/current-drift-mobility.h"
#include "ns3/csma-mac.h"
#include "ns3/ergr-emhc-published.h"
#include "ns3/ergr-emhc-routing.h"
#include "ns3/localisation-noise.h"
#include "ns3/mobility-module.h"
#include "ns3/cb-features.h"
#include "ns3/oracle-evaluator.h"
#include "ns3/selector-cb-v5.h"
#include "ns3/selector-fixed-weight.h"
#include "ns3/feedback-manager.h"
#include "ns3/cb-learner-model.h"
#include "ns3/network-module.h"

#include <sys/resource.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace ns3
{
namespace cbergr
{

void
RegisterSettings(ScenarioConfig& c, ScenarioSettings& s)
{
    c.Add("NoiseBandXMinM", &s.noiseBandXMinM, "Heterogeneous scenario: noise band start x (m)");
    c.Add("NoiseBandXMaxM", &s.noiseBandXMaxM, "Noise band end x (m)");
    c.Add("NoiseBandYMinM", &s.noiseBandYMinM, "Noise region: y from (m)");
    c.Add("NoiseBandYMaxM", &s.noiseBandYMaxM, "Noise region: y to (m)");
    c.Add("NoiseBandZMinM", &s.noiseBandZMinM, "Noise region: depth from (m)");
    c.Add("NoiseBandZMaxM", &s.noiseBandZMaxM, "Noise region: depth to (m)");
    c.Add("NoiseBandStartS", &s.noiseBandStartS, "Noise burst: the band switches on at this time (s)");
    c.Add("NoiseBandEndS", &s.noiseBandEndS, "Noise burst: the band switches off at this time (s), negative = stays on");
    c.Add("NoiseBandOffsetDb", &s.noiseBandOffsetDb, "Extra noise heard by receivers in the band (dB), 0 = off");
    c.Add("TargetLifetimeFactor", &s.targetLifetimeFactor, "CB energy price: target lifetime = factor x SimTimeS");
    c.Add("Mac", &s.mac, "Csma (listen-before-talk with backoff, as in the paper) or Aloha (Aqua-Sim broadcast MAC)");
    c.Add("Protocol", &s.protocol, "G = improved ERGR-EMHC-g (default), P0 = ERGR-EMHC as published (round robin only)");
    c.Add("Selector", &s.selector, "Forwarding method: RoundRobin, CbV5, ...");
    c.Add("SelectorAttributes", &s.selectorAttributes, "Attributes of the selector: Name=value;Name=value");
    c.Add("Flags", &s.flags, "Free text describing the variant (goes to summary.csv)");
    c.Add("OutDir", &s.outDir, "Directory for the result files");
    c.Add("Seed", &s.seed, "Random seed");
    c.Add("RngRun", &s.run, "Run number");
    c.Add("SimTimeS", &s.simTimeS, "Simulated time (s)");
    c.Add("WarmupS", &s.warmupS, "Warm-up excluded from the metrics (s)");
    c.Add("N", &s.numNodes, "Number of ordinary nodes");
    c.Add("NumSinks", &s.numSinks, "Number of surface sinks");
    c.Add("PayloadBytes", &s.payloadBytes, "Data payload (B)");
    c.Add("NumSources", &s.numSources, "Number of nodes that generate traffic (0 = all)");
    c.Add("PktRatePerNodeHz", &s.pktRateHz, "Data packets per node per second");
    c.Add("DataStartS", &s.dataStartS, "Traffic start time (s)");
    c.Add("BoxSizeM", &s.boxSizeM, "Box side (m)");
    c.Add("CellSizeM", &s.cellSizeM, "Grid cell side (m)");
    c.Add("HeadsPerCell", &s.headsPerCell, "Heads per cell (k)");
    c.Add("FaceOnly", &s.faceOnly, "Only face-adjacent cells are neighbours");
    c.Add("BeaconPeriodS", &s.beaconPeriodS, "Beacon interval (s)");
    c.Add("QuorumDetection", &s.quorumDetection, "D3 P4: declare a silent head dead after its beacon is overdue and a quorum agrees");
    c.Add("DeadGuardS", &s.deadGuardS, "Guard past the advertised next beacon before a head counts as overdue (s)");
    c.Add("DeadReportRepeatS", &s.deadReportRepeatS, "Minimum time between two reports of the same silent head by one node (s)");
    c.Add("DeadQuorum", &s.deadQuorum, "Observers (including the node itself) needed to declare a head dead");
    c.Add("ChannelAwareBeacons", &s.channelAwareBeacons, "Heads and sinks advertise ambient noise and addressed-frame loss rate on the beacon (+2 B)");
    c.Add("LeanBeacons", &s.leanBeacons, "N-C: members beacon only before elections, head beacons stretch (Trickle style)");
    c.Add("TrickleMaxFactor", &s.trickleMaxFactor, "Largest head beacon interval in base periods (lean mode)");
    c.Add("HeaderFirstRx", &s.headerFirstRx, "N-A: overheard unicast data is charged preamble + header only");
    c.Add("ElectionPeriodS", &s.electionPeriodS, "Periodic election interval (s)");
    c.Add("ElectionBeta", &s.electionBeta, "Energy weight in the head score");
    c.Add("RangeM", &s.rangeM, "Transmission range R (m)");
    c.Add("BitRateBps", &s.bitRateBps, "Bit rate (bit/s)");
    c.Add("TPre", &s.preambleS, "Preamble duration (s)");
    c.Add("IdealLinks", &s.idealLinks, "Every frame inside the range R is decodable (the paper's near-error-free channel)");
    c.Add("ShadowSigmaDb", &s.shadowSigmaDb, "Shadowing std (dB)");
    c.Add("ShadowTauS", &s.shadowTauS, "Shadowing correlation time (s)");
    c.Add("InitialEnergyJ", &s.initialEnergyJ, "Initial battery (J)");
    c.Add("TxPowerFullW", &s.txPowerFullW, "Transmit power draw at full level (W)");
    c.Add("RxPowerW", &s.rxPowerW, "Receive power draw (W)");
    c.Add("IdlePowerW", &s.idlePowerW, "Idle power draw (W)");
    c.Add("CurrentSpeed", &s.currentSpeed, "Ocean current speed scale U (m/s)");
    c.Add("LocErrM", &s.locErrM, "Position error std per axis (m)");
    c.Add("VelErrMps", &s.velErrMps, "Velocity error std per axis (m/s)");
    c.Add("KillHeadsAtS", &s.killHeadsAtS, "Failure injection time (s); negative = no failures");
    c.Add("KillHeadsCells", &s.killHeadsCells, "Number of cells that lose one head at KillHeadsAtS");
    c.Add("RegretAllPowerLevels", &s.regretAllPowerLevels, "Regret compares against all power levels (use with EnablePower=true)");
    c.Add("ComputeRegret", &s.computeRegret, "Measure regret against the oracle at every decision");
    c.Add("LogNodes", &s.logNodes, "Write nodes.csv");
    c.Add("LogLinks", &s.logLinks, "Write links.csv");
    c.Add("LogPackets", &s.logPackets, "Write packets.csv");
    c.Add("LogEvents", &s.logEvents, "Write events.csv");
    c.Add("LogDecisions", &s.logDecisions, "Write decisions.csv");
    c.Add("LogThetaTrace", &s.logThetaTrace, "Write theta_trace.csv (mean learned weights over time, CB only)");
    c.Add("MeasureCpu", &s.measureCpu, "Time Select, OnSent and OnLabel with std::chrono and write them to cost.csv");
    c.Add("LogCounterfactual", &s.logCounterfactual, "Write counterfactual.csv (ground truth per eligible candidate; no node reads it)");
    c.Add("ShadowSelectorAttributes", &s.shadowSelectorAttributes, "Attributes of the FixedWeight selector that is asked the same question as the real one in counterfactual.csv");
    c.Add("LogFrames", &s.logFrames, "Write frames.csv (large)");
}


namespace
{
// Regret of one decision: best utility over all (candidate, power level) options under the TRUE channel, minus the
// utility of the chosen option, with the same energy price the selector used. Never feeds back into a decision.
std::function<void(const PacketCtx&, const std::vector<Candidate>&, const Decision&, Time)>
MakeRegretObserver(Ptr<OracleEvaluator> oracle,
                   std::shared_ptr<const AcousticLinkModel> link,
                   Ptr<MetricsCollector> metrics,
                   uint32_t nodeId,
                   const ScenarioSettings& s)
{
    auto features = std::make_shared<FeatureBuilder>(link, params::learner::kMarginFromRssi);
    const double bitRate = s.bitRateBps;
    const double preamble = s.preambleS;
    const double txFull = s.txPowerFullW;
    const bool allLevels = s.regretAllPowerLevels;
    return [=](const PacketCtx& ctx, const std::vector<Candidate>& cands, const Decision& d, Time now) {
        const double airtimeS = preamble + ctx.dataBytes * 8.0 / bitRate;
        auto utility = [&](const Candidate& c, int level) {
            double success = oracle->HopSuccess(nodeId, c.id, level, ctx.dataBytes, now, true);
            double progress = 1.0 - params::learner::kKappaP + params::learner::kKappaP * ((c.nb->hmin < ctx.hpkt) ? 1.0 : 0.0);
            double etx = txFull * params::phy::kTxPowerLevelScale[level] * airtimeS;
            return success * progress - d.lambdaE * etx;
        };
        double best = -1e300, chosen = 0.0;
        int viable = 0, trulyViable = 0;
        std::vector<double> candBest;
        // What the static rule (FW, physics weights) would have chosen at this very decision, and its true utility.
        Decision fw = SelectByWeights(*features, params::selector::kFwWeights, params::learner::kKappaP, false, ctx, cands, now);
        double fwUtility = 0.0;
        for (const Candidate& c : cands)
        {
            // A method without power control can only use the full level, so it is only compared with that.
            const int firstLevel = allLevels ? 0 : params::phy::kNumPowerLevels - 1;
            double cb = -1e300;
            for (int level = firstLevel; level < params::phy::kNumPowerLevels; ++level)
            {
                cb = std::max(cb, utility(c, level));
            }
            best = std::max(best, cb);
            candBest.push_back(cb);
            if (c.id == d.fwdId)
            {
                chosen = utility(c, d.powerLevel);
            }
            if (fw.valid && c.id == fw.fwdId)
            {
                fwUtility = utility(c, params::phy::kNumPowerLevels - 1);
            }
            if (features->PredictedMarginDb(ctx, *c.nb, params::phy::kNumPowerLevels - 1, now) > 0.0)
            {
                viable++;
            }
            if (oracle->HopSuccess(nodeId, c.id, params::phy::kNumPowerLevels - 1, ctx.dataBytes, now, true) >= 0.5)
            {
                trulyViable++;
            }
        }
        std::sort(candBest.begin(), candBest.end(), std::greater<double>());
        const double gap = candBest.size() >= 2 ? candBest[0] - candBest[1] : 0.0;
        metrics->OnRegret(std::max(0.0, best - chosen), viable >= 2);
        metrics->OnDecisionDetail(std::max(0.0, best - chosen), fwUtility - chosen, gap, trulyViable >= 2);
    };
}

// Sinks sit on the surface on a regular grid; 4 sinks end up at the quarter points (spec §5.2).
Vector
SinkPosition(int index, int numSinks, double boxM)
{
    int perAxis = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(numSinks))));
    int ix = index % perAxis;
    int iy = index / perAxis;
    return Vector((ix + 0.5) * boxM / perAxis, (iy + 0.5) * boxM / perAxis, 0.0);
}
} // namespace

ScenarioResult
RunScenario(const ScenarioSettings& s, const ScenarioConfig& config)
{
    RngSeedManager::SetSeed(s.seed);
    RngSeedManager::SetRun(s.run);

    // ------------------------------------------------------------ nodes ----
    const int numSinks = s.numSinks;
    const int numTotal = numSinks + s.numNodes;
    NodeContainer nodes;
    nodes.Create(numTotal); // ids 0..numSinks-1 are sinks, the rest are ordinary nodes

    Ptr<MetricsCollector> metrics = CreateObject<MetricsCollector>();
    MetricsCollector::Config mc;
    mc.warmupS = s.warmupS;
    mc.simTimeS = s.simTimeS;
    mc.outDir = s.outDir;
    mc.selectorName = s.selector;
    mc.flags = s.flags;
    mc.scenarioParams = config.OneLine({"OutDir"}); // the output path does not affect results
    mc.seed = s.seed;
    mc.run = s.run;
    mc.logNodes = s.logNodes;
    mc.logLinks = s.logLinks;
    mc.logPackets = s.logPackets;
    mc.logEvents = s.logEvents;
    mc.logFrames = s.logFrames;
    mc.logDecisions = s.logDecisions;
    mc.logCounterfactual = s.logCounterfactual;
    mc.measureCpu = s.measureCpu;
    mc.bitRateBps = s.bitRateBps;
    mc.preambleS = s.preambleS;
    mc.txPowerFullW = s.txPowerFullW;
    mc.rxPowerW = s.rxPowerW;
    metrics->Configure(mc);
    {
        std::ofstream used(s.outDir + "/params_used.txt");
        used << "# resolved parameters of this run (header defaults < config file < command line)\n";
        config.Dump(used);
    }

    // ---------------------------------------------------------- channel ----
    Ptr<CbErgrPropagation> propagation;
    Ptr<AquaSimChannel> channel = CbErgrHelper::CreateChannel(propagation, s.shadowSigmaDb, s.shadowTauS);
    propagation->SetAttribute("RangeM", DoubleValue(s.rangeM));
    propagation->SetAttribute("BitRateBps", DoubleValue(s.bitRateBps));
    propagation->SetAttribute("IdealLinks", BooleanValue(s.idealLinks));
    propagation->SetAttribute("NoiseBandXMinM", DoubleValue(s.noiseBandXMinM));
    propagation->SetAttribute("NoiseBandXMaxM", DoubleValue(s.noiseBandXMaxM));
    propagation->SetAttribute("NoiseBandOffsetDb", DoubleValue(s.noiseBandOffsetDb));
    propagation->SetAttribute("NoiseBandYMinM", DoubleValue(s.noiseBandYMinM));
    propagation->SetAttribute("NoiseBandYMaxM", DoubleValue(s.noiseBandYMaxM));
    propagation->SetAttribute("NoiseBandZMinM", DoubleValue(s.noiseBandZMinM));
    propagation->SetAttribute("NoiseBandZMaxM", DoubleValue(s.noiseBandZMaxM));
    propagation->SetAttribute("NoiseBandStartS", DoubleValue(s.noiseBandStartS));
    propagation->SetAttribute("NoiseBandEndS", DoubleValue(s.noiseBandEndS));
    CbErgrHelper::ConnectLinkTrace(propagation, metrics);

    AquaSimHelper asHelper = AquaSimHelper::Default();
    asHelper.SetChannel(channel);
    if (s.mac != "Csma" && s.mac != "Aloha")
    {
        NS_FATAL_ERROR("Mac must be Csma or Aloha, got " << s.mac);
    }
    asHelper.SetMac(s.mac == "Csma" ? "ns3::cbergr::CsmaMac" : "ns3::AquaSimBroadcastMac");
    const bool publishedProtocol = (s.protocol == "P0");
    asHelper.SetRouting(publishedProtocol ? "ns3::cbergr::ErgrEmhcPublishedRouting" : "ns3::cbergr::ErgrEmhcRouting");
    CbErgrHelper::ConfigureHelper(asHelper, s.preambleS, s.initialEnergyJ, s.txPowerFullW, s.rxPowerW,
                                  s.idlePowerW);

    // ----------------------------------------------- mobility and devices --
    Ptr<UniformRandomVariable> placement = CreateObject<UniformRandomVariable>();
    Ptr<LocalisationNoise> localisation = CreateObject<LocalisationNoise>();
    localisation->SetAttribute("LocErrM", DoubleValue(s.locErrM));
    localisation->SetAttribute("VelErrMps", DoubleValue(s.velErrMps));

    // Ground truth for the oracles and the regret measurement: the only users of true positions/shadowing.
    auto linkModel = std::make_shared<AcousticLinkModel>(propagation->GetLinkModel());
    Ptr<OracleEvaluator> oracle = CreateObject<OracleEvaluator>();
    oracle->Configure([nodes](uint32_t id) { return nodes.Get(id)->GetObject<MobilityModel>()->GetPosition(); },
                      propagation->GetShadowing(), linkModel, s.rangeM);
    oracle->SetNoiseBand(propagation->GetNoiseBand());

    std::vector<Ptr<ErgrEmhcRouting>> routings;
    std::vector<Ptr<ErgrEmhcPublishedRouting>> publishedRoutings;
    std::vector<Ptr<CsmaMac>> csmaMacs;
    std::vector<std::pair<uint16_t, Vector>> sinkTable;
    int64_t stream = 1;
    stream += propagation->AssignStreams(stream);
    placement->SetStream(stream++);
    stream += localisation->AssignStreams(stream);

    // Which ordinary nodes generate traffic: all of them, or NumSources random ones (as in the ERGR-EMHC paper).
    std::set<int> sourceNodes;
    if (s.numSources > 0)
    {
        Ptr<UniformRandomVariable> chooser = CreateObject<UniformRandomVariable>();
        chooser->SetStream(stream++);
        std::vector<int> ids;
        for (int i = numSinks; i < numTotal; ++i)
        {
            ids.push_back(i);
        }
        for (int k = 0; k < s.numSources && !ids.empty(); ++k)
        {
            size_t pick = chooser->GetInteger(0, ids.size() - 1);
            sourceNodes.insert(ids[pick]);
            ids.erase(ids.begin() + pick);
        }
    }

    for (int i = 0; i < numTotal; ++i)
    {
        Ptr<Node> node = nodes.Get(i);
        bool isSink = i < numSinks;

        Ptr<MobilityModel> mobility;
        Vector pos;
        if (isSink)
        {
            mobility = CreateObject<ConstantPositionMobilityModel>();
            pos = SinkPosition(i, numSinks, s.boxSizeM);
        }
        else
        {
            Ptr<CurrentDriftMobility> drift = CreateObject<CurrentDriftMobility>();
            drift->SetAttribute("CurrentSpeed", DoubleValue(s.currentSpeed));
            drift->SetAttribute("BoxSizeM", DoubleValue(s.boxSizeM));
            stream += drift->AssignStreams(stream);
            mobility = drift;
            pos = Vector(placement->GetValue(0, s.boxSizeM), placement->GetValue(0, s.boxSizeM),
                         placement->GetValue(0, s.boxSizeM));
        }
        node->AggregateObject(mobility);
        mobility->SetPosition(pos);

        Ptr<AquaSimNetDevice> device = CreateObject<AquaSimNetDevice>();
        asHelper.Create(node, device);
        CbErgrHelper::ConfigureDevice(device, s.bitRateBps);
        CbErgrHelper::EnablePowerLevels(device, s.txPowerFullW);
        CbErgrHelper::ConnectPhyTrace(device, metrics, node->GetId());
        if (s.headerFirstRx)
        {
            CbErgrHelper::EnableHeaderFirstRx(device, node->GetId(), s.preambleS, s.bitRateBps);
        }
        if (isSink)
        {
            device->GetPhy()->EM()->SetInitialEnergy(1e12); // sinks never run out
            device->GetPhy()->EM()->SetEnergy(1e12);
        }
        device->GetMac()->AssignStreams(stream++);
        if (Ptr<CsmaMac> csma = DynamicCast<CsmaMac>(device->GetMac()))
        {
            csmaMacs.push_back(csma);
        }

        if (publishedProtocol)
        {
            Ptr<ErgrEmhcPublishedRouting> p0 = DynamicCast<ErgrEmhcPublishedRouting>(device->GetRouting());
            p0->SetAttribute("CellSizeM", DoubleValue(s.cellSizeM));
            p0->SetAttribute("BoxSizeM", DoubleValue(s.boxSizeM));
            p0->SetAttribute("TxRangeM", DoubleValue(s.rangeM));
            const bool generates = (s.numSources == 0) || sourceNodes.count(i) > 0;
            p0->SetAttribute("PktRatePerNodeHz", DoubleValue(generates ? s.pktRateHz : 0.0));
            p0->SetAttribute("PayloadBytes", UintegerValue(s.payloadBytes));
            p0->SetAttribute("DataStartS", DoubleValue(s.dataStartS));
            p0->SetNodeRole(isSink);
            p0->SetMetrics(metrics);
            stream += p0->AssignStreams(stream);
            metrics->RegisterNode(node->GetId(), isSink, device->GetPhy()->EM());
            publishedRoutings.push_back(p0);
            if (isSink)
            {
                sinkTable.emplace_back(static_cast<uint16_t>(node->GetId()), pos);
            }
            continue;
        }
        Ptr<ErgrEmhcRouting> routing = DynamicCast<ErgrEmhcRouting>(device->GetRouting());
        routing->SetAttribute("CellSizeM", DoubleValue(s.cellSizeM));
        routing->SetAttribute("BoxSizeM", DoubleValue(s.boxSizeM));
        routing->SetAttribute("TxRangeM", DoubleValue(s.rangeM));
        routing->SetAttribute("HeadsPerCell", UintegerValue(s.headsPerCell));
        routing->SetAttribute("FaceOnly", BooleanValue(s.faceOnly));
        routing->SetAttribute("BeaconPeriodS", DoubleValue(s.beaconPeriodS));
        routing->SetAttribute("LeanBeacons", BooleanValue(s.leanBeacons));
        routing->SetAttribute("ChannelAwareBeacons", BooleanValue(s.channelAwareBeacons));
        routing->SetAttribute("QuorumDetection", BooleanValue(s.quorumDetection));
        routing->SetAttribute("MacDelayAllowanceS",
                              DoubleValue(s.mac == "Csma" ? (params::mac::kCwMin + params::mac::kCwControl) * params::mac::kSlotS : 0.0));
        routing->SetAttribute("DeadQuorum", UintegerValue(s.deadQuorum));
        routing->SetAttribute("DeadGuardS", DoubleValue(s.deadGuardS));
        routing->SetAttribute("DeadReportRepeatS", DoubleValue(s.deadReportRepeatS));
        routing->SetAttribute("TrickleMaxFactor", DoubleValue(s.trickleMaxFactor));
        routing->SetAttribute("ElectionPeriodS", DoubleValue(s.electionPeriodS));
        routing->SetAttribute("ElectionBeta", DoubleValue(s.electionBeta));
        const bool generatesTraffic = (s.numSources == 0) || sourceNodes.count(i) > 0;
        routing->SetAttribute("PktRatePerNodeHz", DoubleValue(generatesTraffic ? s.pktRateHz : 0.0));
        routing->SetAttribute("PayloadBytes", UintegerValue(s.payloadBytes));
        routing->SetAttribute("DataStartS", DoubleValue(s.dataStartS));
        routing->SetNodeRole(isSink);
        routing->SetMetrics(metrics);
        routing->SetLocalisation(localisation);

        ObjectFactory selectorFactory;
        // "GeoOracle" is the oracle class with GeoOnly switched on; every other name maps to Selector<Name>.
        if (s.selector == "GeoOracle")
        {
            selectorFactory.SetTypeId("ns3::cbergr::SelectorOracle");
            selectorFactory.Set("GeoOnly", BooleanValue(true));
        }
        else
        {
            selectorFactory.SetTypeId("ns3::cbergr::Selector" + s.selector);
        }
        if (s.selector == "Oracle" || s.selector == "CbV5")
        {
            selectorFactory.Set("TxPowerFullW", DoubleValue(s.txPowerFullW)); // the energy term must use the scenario's radio
        }
        if (s.selector == "CbV5")
        {
            // B7: the energy price spreads the battery over about twice the run length (explicit SelectorAttributes override).
            selectorFactory.Set("TargetLifetimeS", DoubleValue(s.targetLifetimeFactor * s.simTimeS));
        }
        std::stringstream attrs(s.selectorAttributes);
        for (std::string item; std::getline(attrs, item, ';');)
        {
            size_t eq = item.find('=');
            if (eq != std::string::npos)
            {
                selectorFactory.Set(item.substr(0, eq), StringValue(item.substr(eq + 1)));
            }
        }
        Ptr<ForwarderSelector> selector = selectorFactory.Create<ForwarderSelector>();
        selector->SetOracle(oracle); // only the oracle selectors keep it
        routing->SetSelector(selector);
        if (s.logCounterfactual)
        {
            // What would the static rule (FW) have chosen at this very decision? Simulator-side what-if, never used by the node.
            ObjectFactory shadowFactory;
            shadowFactory.SetTypeId("ns3::cbergr::SelectorFixedWeight");
            std::stringstream sattrs(s.shadowSelectorAttributes);
            for (std::string item; std::getline(sattrs, item, ';');)
            {
                size_t eq = item.find('=');
                if (eq != std::string::npos)
                {
                    shadowFactory.Set(item.substr(0, eq), StringValue(item.substr(eq + 1)));
                }
            }
            routing->SetShadowSelector(shadowFactory.Create<ForwarderSelector>());
        }
        if (s.computeRegret)
        {
            routing->SetDecisionObserver(MakeRegretObserver(oracle, linkModel, metrics, node->GetId(), s));
        }
        routing->SetLinkModel(std::make_shared<AcousticLinkModel>(propagation->GetLinkModel()));
        {
            Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
            routing->SetNoiseMeter([propagation, mob]() {
                // the extra ambient noise (dB) the modem measures where the node is now (noise band / burst)
                return -propagation->GetNoiseBand().SnrChangeDb(mob->GetPosition(), Simulator::Now().GetSeconds());
            });
        }
        routing->SetSnrMeter([propagation](uint32_t tx, uint32_t rx, double& snr) {
            return propagation->MeasuredBeaconSnrDb(tx, rx, snr);
        });
        stream += routing->AssignStreams(stream); // after the selector is set, so it gets streams too

        metrics->RegisterNode(node->GetId(), isSink, device->GetPhy()->EM());
        routings.push_back(routing);
    }
    metrics->SetCellHeadProbe([routings](uint16_t cell) {
        for (const Ptr<ErgrEmhcRouting>& r : routings)
        {
            if (!r->IsSink() && r->IsAlive() && r->IsHead() && r->CellId() == cell)
            {
                return true;
            }
        }
        return false;
    });
    // Ground-truth head coverage, sampled every 25 s (reporting only).
    auto sampleCoverage = std::make_shared<std::function<void()>>();
    *sampleCoverage = [routings, metrics, sampleCoverage]() {
        std::map<uint16_t, std::pair<int, int>> cells; // cell -> (live ordinary nodes, live heads)
        for (const Ptr<ErgrEmhcRouting>& r : routings)
        {
            if (!r->IsSink() && r->IsAlive())
            {
                auto& c = cells[r->CellId()];
                c.first++;
                c.second += r->IsHead() ? 1 : 0;
            }
        }
        uint32_t headed = 0, nodes = 0, headless = 0;
        for (const auto& c : cells)
        {
            nodes += c.second.first;
            headed += c.second.second > 0 ? 1 : 0;
            headless += c.second.second > 0 ? 0 : c.second.first;
        }
        metrics->OnCoverageSample(cells.size(), headed, nodes, headless);
        if (metrics->GetConfig().logEvents)
        {
            std::ostringstream d;
            for (const auto& c : cells)
            {
                if (c.second.second == 0)
                {
                    d << c.first << ":" << c.second.first << " ";
                }
            }
            metrics->OnEvent(0, "HEADLESS_CELLS", d.str());
        }
        Simulator::Schedule(Seconds(25.0), *sampleCoverage);
    };
    Simulator::Schedule(Seconds(25.0), *sampleCoverage);
    for (Ptr<ErgrEmhcRouting>& routing : routings)
    {
        routing->Start();
    }
    for (Ptr<ErgrEmhcPublishedRouting>& routing : publishedRoutings)
    {
        routing->SetSinks(sinkTable);
        routing->Start();
    }

    // Failure injection (D§3 P5): at the chosen time, one random head dies in each of several random cells
    // that hold at least two live nodes, so a replacement exists.
    if (s.killHeadsAtS >= 0.0)
    {
        Ptr<UniformRandomVariable> pick = CreateObject<UniformRandomVariable>();
        pick->SetStream(stream++);
        const int cellsToHit = s.killHeadsCells;
        Simulator::Schedule(Seconds(s.killHeadsAtS), [routings, pick, cellsToHit]() {
            std::map<uint16_t, std::vector<Ptr<ErgrEmhcRouting>>> nodesByCell;
            for (const Ptr<ErgrEmhcRouting>& r : routings)
            {
                if (!r->IsSink() && r->IsAlive())
                {
                    nodesByCell[r->CellId()].push_back(r);
                }
            }
            std::vector<uint16_t> candidates;
            for (const auto& entry : nodesByCell)
            {
                bool hasHead = false;
                for (const auto& r : entry.second)
                {
                    hasHead = hasHead || r->IsHead();
                }
                if (entry.second.size() >= 2 && hasHead)
                {
                    candidates.push_back(entry.first);
                }
            }
            for (int k = 0; k < cellsToHit && !candidates.empty(); ++k)
            {
                size_t ci = pick->GetInteger(0, candidates.size() - 1);
                std::vector<Ptr<ErgrEmhcRouting>> heads;
                for (const auto& r : nodesByCell[candidates[ci]])
                {
                    if (r->IsHead())
                    {
                        heads.push_back(r);
                    }
                }
                heads[pick->GetInteger(0, heads.size() - 1)]->Kill();
                candidates.erase(candidates.begin() + ci);
            }
        });
    }

    // Cost and robustness probes (reporting only).
    metrics->SetSelectorDiagProbe([routings]() {
        std::map<std::string, double> out;
        for (const Ptr<ErgrEmhcRouting>& r : routings)
        {
            if (!r->IsSink() && r->GetSelector())
            {
                r->GetSelector()->Diagnostics(out);
            }
        }
        return out;
    });
    metrics->SetNeighbourProbe([routings]() {
        std::vector<uint32_t> sizes;
        for (const Ptr<ErgrEmhcRouting>& r : routings)
        {
            if (!r->IsSink() && r->IsAlive())
            {
                sizes.push_back(static_cast<uint32_t>(r->Table().Size()));
            }
        }
        return sizes;
    });
    if (s.logThetaTrace)
    {
        auto trace = std::make_shared<std::ofstream>(s.outDir + "/theta_trace.csv");
        *trace << "t_s,heads_with_labels";
        for (int i = 1; i <= params::feature::kFullDim; ++i)
        {
            *trace << ",theta" << i;
        }
        *trace << '\n';
        auto sample = std::make_shared<std::function<void()>>();
        *sample = [routings, trace, sample]() {
            std::map<std::string, double> out;
            for (const Ptr<ErgrEmhcRouting>& r : routings)
            {
                if (!r->IsSink() && r->IsAlive() && r->IsHead() && r->GetSelector())
                {
                    r->GetSelector()->Diagnostics(out);
                }
            }
            const double n = out.count("theta_heads") ? out["theta_heads"] : 0.0;
            *trace << Simulator::Now().GetSeconds() << ',' << n;
            for (int i = 0; i < params::feature::kFullDim; ++i)
            {
                *trace << ',' << (n > 0 ? out["theta_sum_" + std::to_string(i)] / n : 0.0);
            }
            *trace << std::endl;
            Simulator::Schedule(Seconds(100.0), *sample);
        };
        Simulator::Schedule(Seconds(100.0), *sample);
    }
    metrics->AddCostNote("sizeof_neighbour_entry_bytes", sizeof(NeighbourEntry));
    metrics->AddCostNote("sizeof_pending_attempt_bytes", sizeof(PendingAttempt));
    metrics->AddCostNote("sizeof_held_packet_bytes", sizeof(HeldPacket));
    metrics->AddCostNote("sizeof_cb_model_bytes", sizeof(CbLearnerModel<9>));
    metrics->AddCostNote("sizeof_decision_bytes", sizeof(Decision));

    // -------------------------------------------------------------- run ----
    Simulator::Schedule(Seconds(s.warmupS), &MetricsCollector::SnapshotEnergyAtWarmup, metrics);
    Simulator::Schedule(Seconds(s.simTimeS), [csmaMacs, metrics]() {
        uint64_t drops = 0, deferrals = 0;
        for (const Ptr<CsmaMac>& m : csmaMacs)
        {
            drops += m->Drops();
            deferrals += m->Deferrals();
        }
        metrics->SetMacStats(drops, deferrals);
    });
    Simulator::Schedule(Seconds(s.simTimeS), &MetricsCollector::Finalize, metrics);
    Simulator::Stop(Seconds(s.simTimeS + 0.001));

    auto wallStart = std::chrono::steady_clock::now();
    Simulator::Run();
    double wallS = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
    Simulator::Destroy();
    {
        struct rusage ru;
        getrusage(RUSAGE_SELF, &ru);
        std::ofstream res(s.outDir + "/resources.csv");
        res << "key,value\nwall_s," << wallS << "\nmax_rss_kb," << ru.ru_maxrss << '\n';
    }

    ScenarioResult result;
    result.pdr = metrics->Pdr();
    result.packetsCreated = metrics->PacketsCreated();
    result.packetsDelivered = metrics->PacketsDelivered();
    result.rxEnergyShare = metrics->RxEnergyShare();
    result.firstNodeDeathS = metrics->FirstNodeDeathS();
    result.wallSeconds = wallS;
    return result;
}

} // namespace cbergr
} // namespace ns3
