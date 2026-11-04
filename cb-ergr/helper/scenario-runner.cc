/*
 * scenario-runner.cc: builds nodes, mobility, channel, routing and metrics, runs, and finalises
 * (kickoff §4 D3). Nothing here defines a parameter default: see cb-ergr-params.h.
 */

#include "scenario-runner.h"

#include "ns3/aqua-sim-ng-module.h"
#include "ns3/cb-ergr-helper.h"
#include "ns3/core-module.h"
#include "ns3/current-drift-mobility.h"
#include "ns3/ergr-emhc-routing.h"
#include "ns3/localisation-noise.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace ns3
{
namespace cbergr
{

void
RegisterSettings(ScenarioConfig& c, ScenarioSettings& s)
{
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
    c.Add("PktRatePerNodeHz", &s.pktRateHz, "Data packets per node per second");
    c.Add("DataStartS", &s.dataStartS, "Traffic start time (s)");
    c.Add("BoxSizeM", &s.boxSizeM, "Box side (m)");
    c.Add("CellSizeM", &s.cellSizeM, "Grid cell side (m)");
    c.Add("HeadsPerCell", &s.headsPerCell, "Heads per cell (k)");
    c.Add("FaceOnly", &s.faceOnly, "Only face-adjacent cells are neighbours");
    c.Add("BeaconPeriodS", &s.beaconPeriodS, "Beacon interval (s)");
    c.Add("ElectionPeriodS", &s.electionPeriodS, "Periodic election interval (s)");
    c.Add("ElectionBeta", &s.electionBeta, "Energy weight in the head score");
    c.Add("RangeM", &s.rangeM, "Transmission range R (m)");
    c.Add("BitRateBps", &s.bitRateBps, "Bit rate (bit/s)");
    c.Add("TPre", &s.preambleS, "Preamble duration (s)");
    c.Add("ShadowSigmaDb", &s.shadowSigmaDb, "Shadowing std (dB)");
    c.Add("ShadowTauS", &s.shadowTauS, "Shadowing correlation time (s)");
    c.Add("InitialEnergyJ", &s.initialEnergyJ, "Initial battery (J)");
    c.Add("TxPowerFullW", &s.txPowerFullW, "Transmit power draw at full level (W)");
    c.Add("RxPowerW", &s.rxPowerW, "Receive power draw (W)");
    c.Add("IdlePowerW", &s.idlePowerW, "Idle power draw (W)");
    c.Add("CurrentSpeed", &s.currentSpeed, "Ocean current speed scale U (m/s)");
    c.Add("LocErrM", &s.locErrM, "Position error std per axis (m)");
    c.Add("VelErrMps", &s.velErrMps, "Velocity error std per axis (m/s)");
    c.Add("LogNodes", &s.logNodes, "Write nodes.csv");
    c.Add("LogLinks", &s.logLinks, "Write links.csv");
    c.Add("LogPackets", &s.logPackets, "Write packets.csv");
    c.Add("LogEvents", &s.logEvents, "Write events.csv");
    c.Add("LogDecisions", &s.logDecisions, "Write decisions.csv");
    c.Add("LogFrames", &s.logFrames, "Write frames.csv (large)");
}


namespace
{
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
    CbErgrHelper::ConnectLinkTrace(propagation, metrics);

    AquaSimHelper asHelper = AquaSimHelper::Default();
    asHelper.SetChannel(channel);
    asHelper.SetMac("ns3::AquaSimBroadcastMac");
    asHelper.SetRouting("ns3::cbergr::ErgrEmhcRouting");
    CbErgrHelper::ConfigureHelper(asHelper, s.preambleS, s.initialEnergyJ, s.txPowerFullW, s.rxPowerW,
                                  s.idlePowerW);

    // ----------------------------------------------- mobility and devices --
    Ptr<UniformRandomVariable> placement = CreateObject<UniformRandomVariable>();
    Ptr<LocalisationNoise> localisation = CreateObject<LocalisationNoise>();
    localisation->SetAttribute("LocErrM", DoubleValue(s.locErrM));
    localisation->SetAttribute("VelErrMps", DoubleValue(s.velErrMps));

    std::vector<Ptr<ErgrEmhcRouting>> routings;
    int64_t stream = 1;
    stream += propagation->AssignStreams(stream);
    placement->SetStream(stream++);
    stream += localisation->AssignStreams(stream);

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
        CbErgrHelper::ConnectPhyTrace(device, metrics, node->GetId());
        if (isSink)
        {
            device->GetPhy()->EM()->SetInitialEnergy(1e12); // sinks never run out
            device->GetPhy()->EM()->SetEnergy(1e12);
        }
        device->GetMac()->AssignStreams(stream++);

        Ptr<ErgrEmhcRouting> routing = DynamicCast<ErgrEmhcRouting>(device->GetRouting());
        routing->SetAttribute("CellSizeM", DoubleValue(s.cellSizeM));
        routing->SetAttribute("BoxSizeM", DoubleValue(s.boxSizeM));
        routing->SetAttribute("TxRangeM", DoubleValue(s.rangeM));
        routing->SetAttribute("HeadsPerCell", UintegerValue(s.headsPerCell));
        routing->SetAttribute("FaceOnly", BooleanValue(s.faceOnly));
        routing->SetAttribute("BeaconPeriodS", DoubleValue(s.beaconPeriodS));
        routing->SetAttribute("ElectionPeriodS", DoubleValue(s.electionPeriodS));
        routing->SetAttribute("ElectionBeta", DoubleValue(s.electionBeta));
        routing->SetAttribute("PktRatePerNodeHz", DoubleValue(s.pktRateHz));
        routing->SetAttribute("PayloadBytes", UintegerValue(s.payloadBytes));
        routing->SetAttribute("DataStartS", DoubleValue(s.dataStartS));
        routing->SetNodeRole(isSink);
        routing->SetMetrics(metrics);
        routing->SetLocalisation(localisation);
        stream += routing->AssignStreams(stream);

        ObjectFactory selectorFactory;
        selectorFactory.SetTypeId("ns3::cbergr::Selector" + s.selector);
        std::stringstream attrs(s.selectorAttributes);
        for (std::string item; std::getline(attrs, item, ';');)
        {
            size_t eq = item.find('=');
            if (eq != std::string::npos)
            {
                selectorFactory.Set(item.substr(0, eq), StringValue(item.substr(eq + 1)));
            }
        }
        routing->SetSelector(selectorFactory.Create<ForwarderSelector>());

        metrics->RegisterNode(node->GetId(), isSink, device->GetPhy()->EM());
        routings.push_back(routing);
    }
    for (Ptr<ErgrEmhcRouting>& routing : routings)
    {
        routing->Start();
    }

    // -------------------------------------------------------------- run ----
    Simulator::Schedule(Seconds(s.warmupS), &MetricsCollector::SnapshotEnergyAtWarmup, metrics);
    Simulator::Schedule(Seconds(s.simTimeS), &MetricsCollector::Finalize, metrics);
    Simulator::Stop(Seconds(s.simTimeS + 0.001));

    auto wallStart = std::chrono::steady_clock::now();
    Simulator::Run();
    double wallS = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
    Simulator::Destroy();

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
