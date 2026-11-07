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
constexpr bool kEqualHminFallback = true; // [DESIGN] 5 Nov 2026 (E11): with no strictly better forwarder, an equal-Hmin head (like NG2T) may be used while Hpkt allows
constexpr bool kStrictProgress = true;   // [DESIGN] K§2.1 loop guard: candidate must be closer in hops or shallower
// [DECIDED 6 Nov 2026, k = 1 experiment] "Adjacent" = the ERGR-EMHC-g rule (26 adjacent cells, or 6 with FaceOnly; StrictProgress;
// predicted distance <= R). "Paper32" = P0's rule for the 433 m cells: the 32 neighbour cells of the paper (Fig. 3), G1 = lower Hmin
// (normal), G2 = equal or higher Hmin (only when G1 is empty, while Hpkt allows), no distance filter (every pair of points in two
// 32-neighbour cells of side R/(2 sqrt 3) is within R). CONFIRM E53.
constexpr const char* kNeighbourRule = "Adjacent";
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
constexpr double kNoiseBandXMinM = 750.0;   // [DESIGN] heterogeneous scenario: the band is the second cell column (x in [750, 1500))
constexpr double kNoiseBandXMaxM = 1500.0;
constexpr double kNoiseBandStartS = 0.0;    // [DESIGN] 7 Nov 2026: change scenario, the band is a burst between these times
constexpr double kNoiseBandEndS = -1.0;     // negative = never switched off
constexpr double kNoiseBandYMinM = -1.0e9;  // [DESIGN] 8 Nov 2026: no limit in y / depth unless a config sets one
constexpr double kNoiseBandYMaxM = 1.0e9;
constexpr double kNoiseBandZMinM = -1.0e9;
constexpr double kNoiseBandZMaxM = 1.0e9;
constexpr double kNoiseBandOffsetDb = 0.0;  // 0 = off; the heterogeneous scenario uses 10 dB
constexpr bool kIdealLinks = false;      // [DESIGN] test 16 only: frames inside R always decode (the paper's near-error-free channel)
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
constexpr double kMacDelayAllowanceS = 0.0; // the scenario sets (CwMin + CwControl) x slot for Mac=Csma: the ACK timeout must cover the MAC backoff of the DATA and of the ACK
constexpr double kAckSoundSpeedMps = 1500.0; // [SPEC] S§9.1 propagation speed used in the ACK timeout
constexpr bool kRetriesAlwaysAck = true;  // [SPEC] S§9.12 retries always request an ACK
constexpr int kSuspectTimeouts = 2;       // [DESIGN] D§3 P4 consecutive ACK timeouts before a forwarder is suspected
constexpr double kHandoverKeepS = 400.0;  // a received handover model waits this long for its node to become head
constexpr bool kLeanBeacons = true;       // [DESIGN] D§4.2 N-C, default ON since 5 Nov 2026 (beacons are ~28% of airtime); LeanBeacons=false is the leave-one-out run
constexpr double kTrickleMaxFactor = 2.0; // [CONFIRM] N-C: a stable head's beacon interval grows to at most this x BeaconPeriodS (design: 2-4)
constexpr double kPreElectionLeadS = 8.0; // N-C: members announce themselves this long before each election round
constexpr bool kHeaderFirstRx = false;    // [DESIGN] D§4.2 N-A
constexpr bool kAutoElection = true;     // false only in tests that force the roles by hand
constexpr int kDuplicateCapacity = 1024; // [SPEC] S§7 duplicate filter size
constexpr double kDuplicateTtlS = 600.0; // [SPEC] S§7 duplicate filter lifetime
} // namespace timers

// ------------------------------------------------------------------ MAC ----
// Mac=Csma (listen-before-talk with random backoff; the paper used a CSMA-based MAC) or Mac=Aloha (Aqua-Sim's
// AquaSimBroadcastMac: it checks the channel too, but backs off at most 0.1 s four times and then silently drops the frame).
namespace mac
{
constexpr const char* kMac = "Csma";      // [PAPER] "a CSMA-based MAC protocol" (Al-Salti et al. 2019, section 5); Aloha = robustness runs
constexpr double kSlotS = 0.1;            // [CONFIRM] backoff slot
constexpr int kCwMin = 8;                 // [CONFIRM] initial contention window (slots) for data, beacons, elections, handovers
constexpr int kCwMax = 128;               // [CONFIRM] largest contention window
constexpr int kCwControl = 2;             // [CONFIRM] ACKs and NACKs wait at most this many slots (they have a timer running)
constexpr int kMaxAttempts = 16;          // [CONFIRM] busy-channel deferrals before a frame is dropped
constexpr int kQueueLimit = 64;           // [CONFIRM] frames waiting in the MAC
} // namespace mac

// ----------------------------------------------------------- label policy ----
// 7 Nov 2026 (user request, Step 1a/b): the ACK timeout no longer erases the attempt. The sender retries at the timeout as before,
// but keeps a learning record until a late ACK or receipt bitmap arrives; at expiry an attempt with no evidence is CENSORED
// (we do not know whether the frame arrived), not labelled 0. Censored labels are dropped by default.
namespace feedback
{
// 9 Nov 2026: LabelRule = Evidence (default) | Censor (the 7 Nov rule) | TimeoutZero (the original rule: 0 at the ACK timeout).
// Evidence: after the ACK timeout the attempt is labelled (a) by a later receipt bitmap of the forwarder that covers the hopSeq (1 / 0);
// (b) else 0 once the forwarder is heard (its beacon, or an ACK addressed to us) after the receipt for this attempt was due; (c) else
// censored at expiry. A late ACK labels 1 whenever it comes while the record is open.
constexpr const char* kLabelRule = "Evidence";  // [DECIDED 9 Nov 2026]
constexpr double kReceiptGuardS = 5.0;          // [CONFIRM] the receipt is "due" at the forwarder's next beacon after the attempt + this guard
} // namespace feedback

// ----------------------------------------------------- head solicitation ----
// E34 (6 Nov 2026): a member must always know the heads of its own cell. A member with no usable head row HOLDS its packet,
// broadcasts a short SOLICIT (an election-kind frame, 9 B) and the heads of its cell answer with a beacon.
namespace solicit
{
constexpr bool kEnabled = true;           // [DECIDED 6 Nov 2026]
constexpr double kWaitS = 4.0;            // [CONFIRM] time to wait for an answer before asking again (reply jitter 1 s + CSMA + 2 airtimes + propagation)
constexpr int kRetries = 3;               // [CONFIRM] asks before the packets are given up (dropped as "no eligible forwarder")
constexpr double kReplyJitterS = 1.0;     // [CONFIRM] a head waits U(0, this) before answering
constexpr double kReplyMinGapS = 2.0;     // [CONFIRM] a head answers at most once in this time (one beacon serves every asker)
constexpr int kMaxWaiting = 32;           // [CONFIRM] packets a member may hold while it asks
} // namespace solicit

// ------------------------------------------------------- dead-head quorum ----
// D§3 P4: a head is declared dead when its beacon is overdue past its advertised next-beacon time + guard AND a quorum of
// observers agree. Off by default: the old rule (neighbour-table expiry after 3 beacon periods) stays as the backstop.
namespace deadhead
{
constexpr bool kQuorumDetection = false;  // [DESIGN] D§3 P4
constexpr double kCheckPeriodS = 10.0;    // [CONFIRM] how often a node looks for overdue heads
constexpr double kGuardS = 10.0;          // [CONFIRM] guard past the advertised next beacon (propagation 1 s + MAC delays + jitter)
constexpr int kQuorum = 2;                // [DESIGN] D§3 P4 "any 2 of"
constexpr double kReportTtlS = 100.0;     // [CONFIRM] a report counts this long (two beacon periods)
constexpr double kReportRepeatS = 50.0;   // [CONFIRM] an observer repeats its report at most this often while the head stays silent
constexpr double kReportJitterS = 3.0;    // [CONFIRM] random delay before sending a report, so observers do not collide
} // namespace deadhead

// ------------------------------------------------------------ election ----
namespace election
{
constexpr double kBeta = 0.9;            // [CONFIRM] Q2 weight of energy in the head score
constexpr double kSlotS = 1.0;           // [DESIGN] D§3 P3
constexpr double kJitterMaxS = 0.1;      // [DESIGN] D§3 P3
constexpr double kMembershipHysteresisM = 20.0; // [DESIGN] D§3 P3
constexpr bool kHelloScoreStepDown = false; // [DECIDED 6 Nov 2026] option, OFF = ERGR-EMHC-g as in round 5: step-down uses the score a claimant advertised in its Hello (CONFIRM E54)
constexpr bool kReassertHello = false;   // [DECIDED 6 Nov 2026] option, OFF = ERGR-EMHC-g as in round 5: a head that hears a WORSE claimant's Hello while the cell already has k better heads repeats its own Hello, so the claimant steps down at once (CONFIRM E54)
constexpr double kReassertMinGapS = 2.0; // [CONFIRM] a head re-asserts at most this often
constexpr double kReassertJitterS = 0.5; // [CONFIRM] random delay before re-asserting
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
constexpr int kAckDisagreeDraws = 4;          // [DECIDED 7 Nov 2026] AckGate=ThompsonDisagree: M extra posterior samples per gated decision
constexpr double kAckProbStdThreshold = 0.05; // [CONFIRM] ablation AckGate=ProbStd: ACK when p(1-p) width(x) exceeds this; tuned on training seeds
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
constexpr bool kNeighbourPrior = false;  // [DESIGN] R13: a new head without a handover starts from the mean of the neighbouring cells' shared models
constexpr bool kEnableSharing = true;    // [SPEC] S§10
constexpr bool kMarginFromRssi = true;   // [DESIGN] K§2.3
constexpr bool kOnwardFeature = false;   // [DESIGN] K§2.3 (D = 10 if true)
} // namespace learner

namespace selector
{
constexpr bool kRequestAckOnFirstAttempt = true; // FW, A1, oracles, RR+FB: same feedback as CB with AckMode=Every
constexpr bool kRoundRobinWithFeedback = false; // [SPEC] S§9.1: published ERGR-EMHC has no hop ACKs
// Q-learning (QELAR-style), spec S§11 B2 [CONFIRM C1]
constexpr double kQlAlpha = 0.3;
constexpr double kQlGamma = 0.8;
constexpr double kQlEpsilon = 0.1;
constexpr double kQlStepCost = 0.1;        // reward = success - cost
constexpr double kQlHminBonus = 0.1;       // initial Q = 1 + bonus * (myHmin - hmin_j)
constexpr double kQlValueScale = 0.001;    // advertised value is sent as int16 in units of this
// QELAR-G (9 Nov 2026): Hu & Fei, IEEE TMC 9(6) 796-809, 2010. Reward of a successful hop -g - a1 (c_i + c_j) + a2 (d_i + d_j), of a failed one
// -g - b1 c_i + b2 d_i, c = 1 - E_res/E_init, d = (2/pi) atan(E_res - mean E_res of the group). The paper's numeric defaults could NOT be
// retrieved (CONFIRM E50): the values below are my choices.
constexpr double kQelarG = 1.0;            // [CONFIRM] constant transmission cost
constexpr double kQelarAlpha1 = 0.5;       // [CONFIRM]
constexpr double kQelarAlpha2 = 0.5;       // [CONFIRM]
constexpr double kQelarBeta1 = 0.5;        // [CONFIRM]
constexpr double kQelarBeta2 = 0.5;        // [CONFIRM]
constexpr double kQelarGamma = 0.95;       // [CONFIRM] discount
constexpr double kQelarPriorSuccess = 1.0; // [CONFIRM] Beta prior of the per-neighbour success probability P (model-based Q, as in QELAR)
constexpr double kQelarPriorFailure = 1.0; // [CONFIRM]
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
constexpr double kTargetLifetimeS = 1e5; // [CONFIRM] B7: only the attribute default; scenarios set 2 x run length (below)
constexpr double kTargetLifetimeRunFactor = 2.0; // [DECIDED 5 Nov 2026] TargetLifetimeS = this x SimTimeS in every scenario
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

// ------------------------------------------------- P0 (published ERGR-EMHC) ----
// Al-Salti et al., Computer Networks 162 (2019) 106869. "Paper" = stated there; [CONFIRM] = our assumption.
namespace p0
{
constexpr double kCellSizeM = 1500.0 / (2.0 * 1.7320508075688772); // [PAPER] d = R / (2 sqrt 3) = 433.0 m
constexpr double kBeta = 0.9;              // [PAPER] Table 3: Hpkt = beta Hmin + (1 - beta) Hmax
constexpr double kHmaxFactor = 2.0;        // [CONFIRM] Hmax = factor x Hmin (the paper does not define Hmax)
constexpr double kIPeriodS = 50.0;         // [PAPER] Table 3 initialisation period
constexpr double kUpdatePeriodS = 500.0;   // [PAPER] Table 3 update (Hello) period of a head
constexpr double kNewElectionS = 400.0;    // [PAPER] Table 3 new election timer
constexpr double kEnergyThresholdJ = 10.0; // [PAPER] Table 3 epsilon: a head below it sends Retire
constexpr double kPriorityWeight = 0.5;    // [CONFIRM] w in P(i); "w in [0, 1]", value not given
constexpr bool kRandomTimers = false;      // [PAPER] eq. 4: timer = iPeriod x P(i). (The 6 Nov "random timers" instruction was withdrawn on 5 Nov: P0 is the published protocol unchanged.)
constexpr double kPositionCheckS = 5.0;    // [CONFIRM] how often a node checks whether it left its cell
constexpr int kVoidRetryLimit = 3;         // [CONFIRM] NegAck-triggered re-selections
constexpr double kNackProcS = 0.05;        // [CONFIRM] T_processing in eq. 10
} // namespace p0

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
constexpr int kNoiseMargin = 9;  // x10 (7 Nov 2026): x8's margin corrected by the candidate's advertised noise (ChannelAwareBeacons)
constexpr int kFullDim = 10;     // D with x10 (x11, the advertised loss rate, was removed on 9 Nov 2026: CHANGELOG)
} // namespace feature

// ------------------------------------------------- channel-aware beacons ----
// 8 Nov 2026 (user request, Step A): heads (and sinks) advertise the ambient noise they measure at their own position (1 byte).
// The second byte (loss rate of addressed DATA frames, feature x11) was removed on 9 Nov 2026. Off by default.
namespace channelaware
{
constexpr bool kEnabled = false;          // [DECIDED 7 Nov 2026] ChannelAwareBeacons
constexpr double kNoiseTauS = 30.0;       // [DECIDED 7 Nov 2026] EWMA time constant of the noise estimate
constexpr double kNoiseSamplePeriodS = 5.0; // [CONFIRM] how often a node samples its noise (a modem measures between frames)
constexpr double kNoiseRefDb = 20.0;      // [DESIGN] byte 0 = this level (dB, band noise level); 0.5 dB steps => covers 20..147.5 dB
constexpr double kNoiseStepDb = 0.5;      // [DECIDED 7 Nov 2026]
} // namespace channelaware

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
