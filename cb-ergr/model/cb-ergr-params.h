/*
 * cb-ergr-params.h: the ONLY place where parameter defaults are defined.
 *
 * Change a default here, rebuild once, and every class and script picks it up.
 * A run can still override any value without rebuilding:
 *     header default  <  configs/<name>.txt (--ConfigFile=...)  <  command line.
 *
 * Status tags on each value:
 *   [SPEC]    implementation spec v5 (docs/cb-ergr-v5-implementation-spec.md), "S§"
 *   [DESIGN]  design v5.2 / kickoff section 2, "D§" / "K§"
 *   [CONFIRM] unverified, see doc/CONFIRM.md
 *   [TUNED]   chosen on training seeds 101-110 (none yet)
 *
 * Units are in the name suffix: M metres, S seconds, J joules, W watts,
 * Db decibels, Bps bit/s, Hz hertz.
 */

#ifndef CB_ERGR_PARAMS_H
#define CB_ERGR_PARAMS_H

#include <array>

namespace ns3
{
namespace cbergr
{
namespace params
{

// ---------------------------------------------------------------- grid ----
namespace grid
{
constexpr double kBoxSizeM = 3000.0;     // [SPEC] S§5.2 3 km cube
constexpr double kCellSizeM = 750.0;     // [DESIGN] D§3 P1 (4^3 cells); 600 m is a sweep value
constexpr double kPublishedCellSizeM = 866.0; // [CONFIRM] A1: P0 uses R/sqrt(3) ~ 866 m
constexpr int kHeadsPerCell = 2;         // [DESIGN] D§3 P2; P0 uses 1
constexpr bool kFaceOnly = false;        // [DESIGN] K§2.1: false = 26 adjacent cells
constexpr bool kStrictProgress = true;   // [DESIGN] K§2.1 loop guard: candidate must be closer in hops or shallower
} // namespace grid

// ----------------------------------------------------------------- phy ----
namespace phy
{
constexpr double kTxRangeM = 1500.0;     // [SPEC] S§5.2 R
constexpr double kBitRateBps = 5000.0;   // [CONFIRM] Q4
constexpr double kPreambleS = 0.1;       // [SPEC] S§4.1 TPre
constexpr double kCarrierKHz = 20.0;     // [SPEC] S§4.4
constexpr double kBandwidthHz = 5000.0;  // [SPEC] S§4.4 B
constexpr double kSpreadingK = 1.5;      // [SPEC] S§4.4 spreading factor
constexpr double kShippingActivity = 0.5; // [SPEC] S§4.4 s
constexpr double kWindSpeedMps = 5.0;    // [SPEC] S§4.4 w
constexpr double kReferenceAcousticPtW = 0.2818; // upstream PT default; the 'full' level. Level k scales it by kTxPowerLevelScale[k]
constexpr double kRxThreshold = 0.5;     // PHY RXThresh; propagation reports pR = 1 (ok) or 0 (lost), see cb-ergr-propagation.h
constexpr double kCalibFrameBytes = 150.0; // [SPEC] S§4.4 frame length used to calibrate the source level
constexpr double kCalibPer = 0.5;        // [SPEC] S§4.4 data PER at R, mean shadowing, full power
constexpr double kSnrReqPer = 0.1;       // [SPEC] S§8.2 PER used to define SnrReqDb
constexpr int kNumPowerLevels = 3;       // [SPEC] S§4.3
constexpr std::array<double, 3> kTxPowerLevelScale = {0.25, 0.5, 1.0}; // [SPEC] S§4.3
constexpr std::array<double, 3> kSourceLevelOffsetDb = {-6.0, -3.0, 0.0}; // [SPEC] S§4.3
} // namespace phy

// -------------------------------------------------------------- energy ----
namespace energy
{
constexpr double kInitialEnergyJ = 300.0; // [CONFIRM] Q4; recalibrate for ERGR-EMHC-g (D§4.1)
constexpr double kTxPowerFullW = 2.0;     // [CONFIRM] Q4
constexpr double kRxPowerW = 0.75;        // [CONFIRM] Q4
constexpr double kIdlePowerW = 0.008;     // [CONFIRM] Q4
} // namespace energy

// ------------------------------------------------------------- channel ----
namespace channel
{
constexpr double kShadowSigmaDb = 5.0;   // [SPEC] S§4.5
constexpr double kShadowTauS = 120.0;    // [SPEC] S§4.5
constexpr double kShadowTickS = 5.0;     // [DESIGN] time grid of the process; see gm-shadowing.h
} // namespace channel

// ------------------------------------------------------------ mobility ----
namespace mobility
{
constexpr double kCurrentSpeedMps = 1.0; // [SPEC] S§4.6 U; sweep {0, 0.5, 1, 2}
constexpr double kVerticalDriftFactor = 0.1; // [SPEC] S§4.6 vertical drift = 0.1 U
constexpr double kUpdateIntervalS = 5.0; // [DESIGN] mobility integration step; positions are extrapolated between steps
constexpr double kOuTauS = 300.0;        // [CONFIRM] pilot: OU correlation time of the per-node perturbation
constexpr double kOuSigmaFraction = 0.3; // [CONFIRM] pilot: OU std per axis as a fraction of CurrentSpeed
constexpr double kMeanderPeriodS = 3600.0; // [CONFIRM] pilot: period of the meandering current / vertical drift
constexpr double kLocErrM = 20.0;        // [SPEC] S§4.7 per axis
constexpr double kVelErrMps = 0.1;       // [SPEC] S§4.7
} // namespace mobility

// ------------------------------------------------------------ scenario ----
namespace scenario
{
constexpr int kNumNodes = 250;           // [SPEC] S§5.2
constexpr int kNumSinks = 4;             // [SPEC] S§5.2 surface, quarter points
constexpr int kPayloadBytes = 150;       // [SPEC] S§5.2
constexpr double kPktRatePerNodeHz = 0.07; // [CONFIRM] Q4
constexpr double kSimTimeS = 20000.0;    // [SPEC] S§12
constexpr double kWarmupS = 2000.0;      // [SPEC] S§12 excluded from metrics
constexpr int kSeed = 1;                 // [SPEC] S§10
constexpr int kRun = 1;                  // [SPEC] S§10
} // namespace scenario

// -------------------------------------------------------------- timers ----
namespace timers
{
constexpr double kBeaconPeriodS = 50.0;  // [CONFIRM] A8
constexpr int kNeighbourTimeoutBeacons = 3; // [SPEC] S§7.1 (x beacon period)
constexpr int kReceiptKeepBeacons = 3;   // [SPEC] S§6.3
constexpr int kBlobKeepBeacons = 10;     // [SPEC] S§8.10
constexpr int kExpiryBeacons = 3;        // [SPEC] S§9.1 MExpiry = 3 x beacon period
constexpr int kMaxTx = 3;                // [SPEC] S§9.8
constexpr double kAckProcS = 0.05;       // [SPEC] S§9.1 TProc
constexpr double kAckGuardS = 0.2;       // [SPEC] S§9.1 TGuard
constexpr double kPriceTickS = 600.0;    // [SPEC] S§8.9
constexpr double kElectionPeriodS = 400.0; // [CONFIRM] Q2
constexpr double kElectionOffsetPerCellS = 2.0; // [DESIGN] D§3 P3 elections offset by cell id (cell k starts k*2 s late)
constexpr double kFirstElectionS = 55.0; // [DESIGN] D§3 P6 after every node has beaconed once (one beacon period + 5 s)
constexpr double kNackWindowS = 8.0;     // [CONFIRM] A6 how long a forwarded packet is remembered for void NACKs
constexpr int kVoidRetryLimit = 3;       // [CONFIRM] A6 NACK-triggered re-selections before the packet is dropped
constexpr double kDataStartS = 300.0;    // [DESIGN] D§3 P6 data starts after start-up
constexpr double kBeaconJitterFraction = 0.0; // beacons are strictly periodic after a random first phase
constexpr double kPhyRxDelayAirtimes = 1.0; // [MEASURED] Aqua-Sim hands a frame to the MAC 2 airtimes after it starts arriving (collision check + signal cache); the ACK timeout allows for the extra one, CONFIRM E17
constexpr double kAckSoundSpeedMps = 1500.0; // [SPEC] S§9.1 propagation speed used in the ACK timeout
constexpr bool kRetriesAlwaysAck = true;  // [SPEC] S§9.12 retries always request an ACK
constexpr bool kAutoElection = true;     // false only in tests that force the roles by hand
constexpr int kDuplicateCapacity = 1024; // [SPEC] S§7 duplicate filter size
constexpr double kDuplicateTtlS = 600.0; // [SPEC] S§7 duplicate filter lifetime
} // namespace timers

// ------------------------------------------------------------ election ----
namespace election
{
constexpr double kBeta = 0.9;            // [CONFIRM] Q2 weight of energy in the head score
constexpr double kSlotS = 1.0;           // [DESIGN] D§3 P3
constexpr double kJitterMaxS = 0.1;      // [DESIGN] D§3 P3
constexpr double kMembershipHysteresisM = 20.0; // [DESIGN] D§3 P3
} // namespace election

// ------------------------------------------------------------- learner ----
namespace learner
{
constexpr double kForgetGamma = 0.98;    // [SPEC] S§8.3, per kForgetPeriodS
constexpr double kForgetPeriodS = 60.0;  // [SPEC] S§8.3 T0
constexpr double kLambda0 = 1.0;         // [SPEC] S§8.3 prior precision
constexpr double kExplorationScale = 1.0; // [SPEC] S§8.4 V
constexpr double kKappaP = 0.5;          // [SPEC] S§8.5
constexpr double kMu = 0.1;              // [SPEC] S§8.5
constexpr double kKappa = 1.0;           // [SPEC] S§8.5 exponent of phi(e)
constexpr double kBatteryFloor = 0.01;   // [SPEC] S§8.5 max(e, 0.01)
constexpr double kAckWidthThreshold = 0.30; // [SPEC] S§8.6 TauW
constexpr double kAuditRate = 0.10;      // [SPEC] S§8.6 QAudit
constexpr double kEntryAgeScaleS = 1000.0; // [SPEC] S§8.1
constexpr double kQueueMax = 10.0;       // [SPEC] S§8.1
constexpr double kVMaxMps = 2.0;         // [SPEC] S§8.1
constexpr double kMarginOffsetDb = 10.0; // [SPEC] S§8.1 x8 = (margin + 10) / 30
constexpr double kMarginScaleDb = 30.0;  // [SPEC] S§8.1
constexpr double kSigmoidClamp = 30.0;   // [SPEC] S§8.13
constexpr double kCholeskyJitter = 1e-9; // [SPEC] S§8.12
constexpr int kCholeskyMaxRetries = 3;   // [SPEC] S§8.12
constexpr bool kEnablePower = false;     // [DESIGN] K§2.3 optional, ablation only
constexpr bool kEnableLifetime = true;   // [SPEC] S§10
constexpr bool kEnableSharing = true;    // [SPEC] S§10
constexpr bool kMarginFromRssi = true;   // [DESIGN] K§2.3
constexpr bool kOnwardFeature = false;   // [DESIGN] K§2.3 (D = 10 if true)
} // namespace learner

namespace selector
{
constexpr bool kRoundRobinWithFeedback = false; // [SPEC] S§9.1: published ERGR-EMHC has no hop ACKs
// Q-learning (QELAR-style), spec S§11 B2 [CONFIRM C1]
constexpr double kQlAlpha = 0.3;
constexpr double kQlGamma = 0.8;
constexpr double kQlEpsilon = 0.1;
constexpr double kQlStepCost = 0.1;        // reward = success - cost
constexpr double kQlHminBonus = 0.1;       // initial Q = 1 + bonus * (myHmin - hmin_j)
constexpr double kQlValueScale = 0.001;    // advertised value is sent as int16 in units of this
// CARP-style EWMA of hop success, S§11 B3
constexpr double kCarpBeta = 0.2;
constexpr double kCarpInitial = 0.5;
// Linear function-approximation Q, S§11 B4
constexpr double kLfaStep = 0.05;
constexpr double kLfaEpsilon = 0.1;
constexpr double kLfaGamma = 0.8;
// Fixed-weight selector (K§2.4 FW): hand-set logistic weights on x1..x9; see selector-fixed-weight.cc
constexpr std::array<double, 9> kFwWeights = {-0.4, 0.5, 0.8, 0.2, 0.1, -0.2, 0.2, 9.0, -3.0};
} // namespace selector

// --------------------------------------------------------------- price ----
namespace price
{
constexpr double kLambdaInit = 0.25;     // [SPEC] S§8.9
constexpr double kEta = 20.0;            // [SPEC] S§8.9
constexpr double kTargetLifetimeS = 1e5; // [CONFIRM] B7
} // namespace price

// ------------------------------------------------------------- sharing ----
namespace sharing
{
constexpr double kTauSync = 0.7;         // [SPEC] S§8.10 log-det change that triggers a blob
constexpr double kCosGate = 0.5;         // [SPEC] S§8.10
constexpr double kMatureTrace = 50.0;    // [SPEC] S§8.10
constexpr double kBetaHandover = 0.7;    // [SPEC] S§8.10, unplanned takeover only [DESIGN] D§3 P8
constexpr double kBetaInflate = 0.7;     // [SPEC] S§8.11, on death / unplanned loss only
constexpr double kThetaScale = 1.0 / 1024.0; // [SPEC] S§6.3 int16 quantisation
constexpr double kDiagScale = 1.0 / 16.0;    // [SPEC] S§6.3 uint16 quantisation
} // namespace sharing

// ------------------------------------------------------------- feature ----
// Zero-based positions in the feature vector (spec S§8.1 x1..x9).
namespace feature
{
constexpr int kDistance = 0;     // x1
constexpr int kHopProgress = 1;  // x2
constexpr int kDepthAdvantage = 2; // x3
constexpr int kBattery = 3;      // x4
constexpr int kEntryAge = 4;     // x5
constexpr int kQueue = 5;        // x6
constexpr int kClosingSpeed = 6; // x7
constexpr int kSnrMargin = 7;    // x8
constexpr int kIntercept = 8;    // x9
constexpr int kBaseDim = 9;      // D without the optional onward feature
} // namespace feature

// ------------------------------------------------------------- logging ----
namespace logging
{
constexpr bool kLogNodes = true;         // [DESIGN] K§2.6
constexpr bool kLogLinks = true;
constexpr bool kLogPackets = true;
constexpr bool kLogEvents = true;
constexpr bool kLogDecisions = false;
constexpr bool kLogFrames = false;       // large, debug only
constexpr bool kLogNodeTimeline = false;
constexpr double kNodeLogIntervalS = 1000.0;
} // namespace logging

} // namespace params
} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_PARAMS_H */
