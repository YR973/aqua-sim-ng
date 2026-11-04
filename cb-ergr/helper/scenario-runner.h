/*
 * scenario-runner.h: build and run one complete CB-ERGR simulation (used by cb-ergr-scenario and the tests).
 * All settings live in ScenarioSettings; their defaults come only from cb-ergr-params.h.
 */

#ifndef CB_ERGR_SCENARIO_RUNNER_H
#define CB_ERGR_SCENARIO_RUNNER_H

#include "ns3/cb-ergr-params.h"
#include "ns3/metrics-collector.h"
#include "ns3/scenario-config.h"

#include <string>

namespace ns3
{
namespace cbergr
{

struct ScenarioSettings
{
    // run
    std::string selector = "RoundRobin";
    std::string flags = "";
    std::string outDir = "results/run";
    std::string configFile = "";
    int seed = params::scenario::kSeed;
    int run = params::scenario::kRun;
    double simTimeS = params::scenario::kSimTimeS;
    double warmupS = params::scenario::kWarmupS;
    // network
    int numNodes = params::scenario::kNumNodes;
    int numSinks = params::scenario::kNumSinks;
    int payloadBytes = params::scenario::kPayloadBytes;
    double pktRateHz = params::scenario::kPktRatePerNodeHz;
    double dataStartS = params::timers::kDataStartS;
    // grid / election
    double boxSizeM = params::grid::kBoxSizeM;
    double cellSizeM = params::grid::kCellSizeM;
    int headsPerCell = params::grid::kHeadsPerCell;
    bool faceOnly = params::grid::kFaceOnly;
    double beaconPeriodS = params::timers::kBeaconPeriodS;
    double electionPeriodS = params::timers::kElectionPeriodS;
    double electionBeta = params::election::kBeta;
    // phy / channel
    double rangeM = params::phy::kTxRangeM;
    double bitRateBps = params::phy::kBitRateBps;
    double preambleS = params::phy::kPreambleS;
    double shadowSigmaDb = params::channel::kShadowSigmaDb;
    double shadowTauS = params::channel::kShadowTauS;
    // energy
    double initialEnergyJ = params::energy::kInitialEnergyJ;
    double txPowerFullW = params::energy::kTxPowerFullW;
    double rxPowerW = params::energy::kRxPowerW;
    double idlePowerW = params::energy::kIdlePowerW;
    // mobility
    double currentSpeed = params::mobility::kCurrentSpeedMps;
    double locErrM = params::mobility::kLocErrM;
    double velErrMps = params::mobility::kVelErrMps;
    // logging
    bool logNodes = params::logging::kLogNodes;
    bool logLinks = params::logging::kLogLinks;
    bool logPackets = params::logging::kLogPackets;
    bool logEvents = params::logging::kLogEvents;
    bool logDecisions = params::logging::kLogDecisions;
    bool logFrames = params::logging::kLogFrames;
};

/// Register every setting for config-file / command-line handling and params_used.txt.
void RegisterSettings(ScenarioConfig& config, ScenarioSettings& settings);

/// Summary numbers of one run (also written to OutDir/summary.csv).
struct ScenarioResult
{
    double pdr = 0.0;
    uint32_t packetsCreated = 0;
    uint32_t packetsDelivered = 0;
    double rxEnergyShare = 0.0;
    double firstNodeDeathS = -1.0;
    double wallSeconds = 0.0;
};

/// Build the network, run it to SimTimeS, finalise the metrics and destroy the simulator.
ScenarioResult RunScenario(const ScenarioSettings& settings, const ScenarioConfig& config);

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SCENARIO_RUNNER_H */
