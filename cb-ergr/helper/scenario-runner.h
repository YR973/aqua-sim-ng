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
    double noiseBandXMinM = params::channel::kNoiseBandXMinM;
    double noiseBandXMaxM = params::channel::kNoiseBandXMaxM;
    double noiseBandOffsetDb = params::channel::kNoiseBandOffsetDb;
    double noiseBandYMinM = params::channel::kNoiseBandYMinM;
    double noiseBandYMaxM = params::channel::kNoiseBandYMaxM;
    double noiseBandZMinM = params::channel::kNoiseBandZMinM;
    double noiseBandZMaxM = params::channel::kNoiseBandZMaxM;
    double noiseBandStartS = params::channel::kNoiseBandStartS;
    double noiseBandEndS = params::channel::kNoiseBandEndS;
    double targetLifetimeFactor = params::price::kTargetLifetimeRunFactor;
    std::string mac = params::mac::kMac;   // "Csma" or "Aloha"
    std::string protocol = "G";            // "G" = ERGR-EMHC-g, "P0" = ERGR-EMHC as published
    std::string selector = "RoundRobin";
    std::string flags = "";
    std::string selectorAttributes = "";   // "Name=value;Name=value" set on every selector, e.g. WithFeedback=true
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
    int numSources = 0;                    // 0: every node generates traffic; k > 0: only k randomly chosen nodes (the paper uses 10)
    double dataStartS = params::timers::kDataStartS;
    // grid / election
    double boxSizeM = params::grid::kBoxSizeM;
    double cellSizeM = params::grid::kCellSizeM;
    int headsPerCell = params::grid::kHeadsPerCell;
    bool faceOnly = params::grid::kFaceOnly;
    std::string neighbourRule = params::grid::kNeighbourRule;
    bool logAttempts = false;
    bool helloScoreStepDown = params::election::kHelloScoreStepDown;
    bool reassertHello = params::election::kReassertHello;
    double beaconPeriodS = params::timers::kBeaconPeriodS;
    bool quorumDetection = params::deadhead::kQuorumDetection;   // D3 P4
    int deadQuorum = params::deadhead::kQuorum;
    double deadGuardS = params::deadhead::kGuardS;
    double deadReportRepeatS = params::deadhead::kReportRepeatS;
    bool leanBeacons = params::timers::kLeanBeacons;        // N-C
    std::string labelRule = params::feedback::kLabelRule; // Evidence | Censor | TimeoutZero
    bool channelAwareBeacons = params::channelaware::kEnabled; // heads/sinks advertise the ambient noise at their position (+1 B)
    double trickleMaxFactor = params::timers::kTrickleMaxFactor;
    bool headerFirstRx = params::timers::kHeaderFirstRx;    // N-A
    double electionPeriodS = params::timers::kElectionPeriodS;
    double electionBeta = params::election::kBeta;
    // phy / channel
    double rangeM = params::phy::kTxRangeM;
    double bitRateBps = params::phy::kBitRateBps;
    double preambleS = params::phy::kPreambleS;
    bool idealLinks = params::channel::kIdealLinks;
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
    double killHeadsAtS = -1.0;            // failure injection: kill a head in several cells at this time (<0: off)
    int killHeadsCells = 5;                // in how many random cells
    bool regretAllPowerLevels = false;     // regret may use lower power levels (set when the method controls power)
    bool computeRegret = true;             // measure regret against the oracle at every decision
    // logging
    bool logNodes = params::logging::kLogNodes;
    bool logLinks = params::logging::kLogLinks;
    bool logPackets = params::logging::kLogPackets;
    bool logEvents = params::logging::kLogEvents;
    bool logDecisions = params::logging::kLogDecisions;
    bool logThetaTrace = false;            // theta_trace.csv: mean theta of the heads that have learned, every 100 s (CB only)
    bool measureCpu = false;               // time Select/OnSent/OnLabel (cost.csv); off by default, no overhead
    bool logCounterfactual = false;        // counterfactual.csv: would the frame have reached each eligible candidate? (simulator side)
    std::string shadowSelectorAttributes = ""; // attributes of the FixedWeight shadow selector used for counterfactual.csv ("" = physics weights)
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
