/*
 * CB-ERGR test suite (spec §14, kickoff §4). Run: ./test.py -s cb-ergr
 *
 * Tests are named after the behaviour they protect, each with a one-line
 * comment naming the spec rule. Sections follow the build steps D0..D7.
 */

#include "ns3/acoustic-link-model.h"
#include "ns3/aqua-sim-header-mac.h"
#include "ns3/aqua-sim-ng-module.h"
#include "ns3/core-module.h"
#include "ns3/cb-ergr-helper.h"
#include "ns3/ergr-emhc-routing.h"
#include "ns3/mobility-module.h"
#include "ns3/oracle-evaluator.h"
#include "ns3/selector-carp-ewma.h"
#include "ns3/tx-power-level-tag.h"
#include "ns3/selector-cb-v5.h"
#include "ns3/selector-fixed-weight.h"
#include "ns3/selector-lfa-q.h"
#include "ns3/selector-oracle.h"
#include "ns3/selector-q-learning.h"
#include "ns3/selector-round-robin.h"
#include "ns3/aqua-sim-phy-cmn.h"
#include "ns3/boolean.h"
#include "ns3/cb-learner-model.h"
#include "ns3/cb-ergr-config.h"
#include "ns3/double.h"
#include "ns3/ergr-headers.h"
#include "ns3/grid-geometry.h"
#include "ns3/neighbour-table.h"
#include "ns3/recent-set.h"
#include "ns3/scenario-runner.h"
#include "ns3/energy-price.h"
#include "ns3/gm-shadowing.h"
#include "ns3/receipt-window.h"
#include "ns3/serial-number.h"
#include "ns3/simulator.h"
#include "ns3/small-matrix.h"
#include "ns3/test.h"
#include "ns3/type-id.h"
#include "ns3/uinteger.h"

#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <vector>
#include <fstream>
#include <random>
#include <sstream>

using namespace ns3;
using namespace ns3::cbergr;

namespace
{
const double kTol = 1e-9;
}

// ============================================================ D0 ===========

// M0: the Aqua-Sim NG classes named in the spec exist under these names.
class UpstreamTypesAreRegistered : public TestCase
{
  public:
    UpstreamTypesAreRegistered()
        : TestCase("D0: upstream Aqua-Sim NG types are registered")
    {
    }

  private:
    void DoRun() override
    {
        const char* names[] = {"ns3::AquaSimPhyCmn",
                               "ns3::AquaSimEnergyModel",
                               "ns3::AquaSimRouting",
                               "ns3::AquaSimChannel",
                               "ns3::AquaSimRangePropagation",
                               "ns3::AquaSimBroadcastMac",
                               "ns3::AquaSimAloha"};
        for (const char* n : names)
        {
            TypeId tid;
            NS_TEST_ASSERT_MSG_EQ(TypeId::LookupByNameFailSafe(n, &tid), true, "missing type " << n);
        }
    }
};

// ============================================================ D1 ===========

// Spec test 1 (§8.12): Cholesky, triangular solves and log-det on a known SPD matrix.
class CholeskyAndSolvesMatchHandComputedValues : public TestCase
{
  public:
    CholeskyAndSolvesMatchHandComputedValues()
        : TestCase("D1 test1: Cholesky, solves, logdet, trace on a known matrix")
    {
    }

  private:
    void DoRun() override
    {
        // Classic example: A = L L^T with L = [[2,0,0],[6,1,0],[-8,5,3]].
        Mat<3> A;
        const double av[9] = {4, 12, -16, 12, 37, -43, -16, -43, 98};
        for (int i = 0; i < 9; ++i)
        {
            A.a[i] = av[i];
        }
        Mat<3> L;
        NS_TEST_ASSERT_MSG_EQ(Cholesky(A, L), true, "A is positive definite");
        const double lv[9] = {2, 0, 0, 6, 1, 0, -8, 5, 3};
        for (int i = 0; i < 9; ++i)
        {
            NS_TEST_ASSERT_MSG_EQ_TOL(L.a[i], lv[i], kTol, "L entry " << i);
        }

        // b = A * [1,2,3] = [-20,-43,192]; forward solve gives y = [-10,17,9].
        Vec<3> b;
        b[0] = -20;
        b[1] = -43;
        b[2] = 192;
        Vec<3> y = ForwardSolve(L, b);
        NS_TEST_ASSERT_MSG_EQ_TOL(y[0], -10, kTol, "forward y0");
        NS_TEST_ASSERT_MSG_EQ_TOL(y[1], 17, kTol, "forward y1");
        NS_TEST_ASSERT_MSG_EQ_TOL(y[2], 9, kTol, "forward y2");

        Vec<3> x = SolveSPD(L, b);
        NS_TEST_ASSERT_MSG_EQ_TOL(x[0], 1, 1e-8, "solve x0");
        NS_TEST_ASSERT_MSG_EQ_TOL(x[1], 2, 1e-8, "solve x1");
        NS_TEST_ASSERT_MSG_EQ_TOL(x[2], 3, 1e-8, "solve x2");

        // det(A) = (2*1*3)^2 = 36.
        NS_TEST_ASSERT_MSG_EQ_TOL(LogDetFromChol(L), std::log(36.0), kTol, "logdet");
        NS_TEST_ASSERT_MSG_EQ_TOL(A.Trace(), 139.0, kTol, "trace");

        // BackSolve solves L^T x = y: with y = L^T [1,2,3] the answer is [1,2,3].
        Vec<3> yt;
        yt[0] = 2 * 1 + 6 * 2 - 8 * 3;
        yt[1] = 1 * 2 + 5 * 3;
        yt[2] = 3 * 3;
        Vec<3> xt = BackSolve(L, yt);
        NS_TEST_ASSERT_MSG_EQ_TOL(xt[0], 1, kTol, "back x0");
        NS_TEST_ASSERT_MSG_EQ_TOL(xt[1], 2, kTol, "back x1");
        NS_TEST_ASSERT_MSG_EQ_TOL(xt[2], 3, kTol, "back x2");
    }
};

// Spec §8.12: a barely-singular matrix is rescued by jitter; a hopeless one resets to lambda0*I.
class CholeskyFallbackJittersThenResets : public TestCase
{
  public:
    CholeskyFallbackJittersThenResets()
        : TestCase("D1 test1: Cholesky jitter fallback and reset")
    {
    }

  private:
    void DoRun() override
    {
        Mat<3> ones; // all-ones is positive semi-definite, so the plain factorisation fails
        for (auto& e : ones.a)
        {
            e = 1.0;
        }
        Mat<3> L;
        NS_TEST_ASSERT_MSG_EQ(Cholesky(ones, L), false, "singular matrix must fail plain Cholesky");
        CholeskyStatus status = CholeskyWithFallback(ones, L, 1.0);
        NS_TEST_ASSERT_MSG_EQ((status == CholeskyStatus::Jittered), true, "jitter should rescue it");

        Mat<3> negative = Mat<3>::ScaledIdentity(-1.0);
        status = CholeskyWithFallback(negative, L, 2.5);
        NS_TEST_ASSERT_MSG_EQ((status == CholeskyStatus::Reset), true, "negative definite must reset");
        for (int i = 0; i < 3; ++i)
        {
            NS_TEST_ASSERT_MSG_EQ_TOL(negative.At(i, i), 2.5, kTol, "reset to lambda0 * I");
        }
    }
};

// Spec §8.13 + test 2: sigmoid is clamped.
class SigmoidIsClamped : public TestCase
{
  public:
    SigmoidIsClamped()
        : TestCase("D1 test2: sigmoid clamps its argument to [-30, 30]")
    {
    }

  private:
    void DoRun() override
    {
        NS_TEST_ASSERT_MSG_EQ_TOL(Sigmoid(0.0), 0.5, kTol, "sigmoid(0)");
        NS_TEST_ASSERT_MSG_EQ_TOL(Sigmoid(1e6), Sigmoid(30.0), 0.0, "upper clamp");
        NS_TEST_ASSERT_MSG_EQ_TOL(Sigmoid(-1e6), Sigmoid(-30.0), 0.0, "lower clamp");
        NS_TEST_ASSERT_MSG_EQ((Sigmoid(-1e6) > 0.0), true, "never exactly 0");
        NS_TEST_ASSERT_MSG_EQ((Sigmoid(1e6) < 1.0 + 1e-12), true, "never above 1");
    }
};

// Spec test 2: Newton steps on a toy 2-feature logistic problem reduce the loss and
// reach the regularised optimum (found independently by gradient descent; no sklearn here).
class LogisticNewtonStepConvergesOnToyData : public TestCase
{
  public:
    LogisticNewtonStepConvergesOnToyData()
        : TestCase("D1 test2: logistic Newton step decreases loss and converges")
    {
    }

  private:
    struct Sample
    {
        Vec<2> x;
        int y;
    };

    // Regularised logistic loss: sum log-loss + 0.5 * lambda * |theta|^2 (lambda = 1 = lambda0).
    static double Loss(const std::vector<Sample>& data, const Vec<2>& theta)
    {
        double loss = 0.5 * theta.Dot(theta);
        for (const Sample& s : data)
        {
            double p = Sigmoid(theta.Dot(s.x));
            loss -= s.y ? std::log(p) : std::log(1.0 - p);
        }
        return loss;
    }

    void DoRun() override
    {
        std::mt19937 gen(12345);
        std::uniform_real_distribution<double> uni(-2.0, 2.0);
        std::uniform_real_distribution<double> coin(0.0, 1.0);
        std::vector<Sample> data;
        for (int n = 0; n < 100; ++n)
        {
            Sample s;
            s.x[0] = uni(gen);
            s.x[1] = 1.0; // intercept
            double pTrue = Sigmoid(2.0 * s.x[0] - 0.5);
            s.y = coin(gen) < pTrue ? 1 : 0;
            data.push_back(s);
        }

        // Reference optimum by plain gradient descent (independent of the code under test).
        Vec<2> ref;
        for (int it = 0; it < 200000; ++it)
        {
            Vec<2> grad = ref; // gradient of the regulariser
            for (const Sample& s : data)
            {
                double resid = Sigmoid(ref.Dot(s.x)) - s.y;
                grad[0] += resid * s.x[0];
                grad[1] += resid * s.x[1];
            }
            ref.AddScaled(grad, -0.002);
        }

        // Newton steps with the exact Hessian, using our matrix primitives.
        Vec<2> theta;
        double previousLoss = Loss(data, theta);
        for (int it = 0; it < 15; ++it)
        {
            Mat<2> H = Mat<2>::ScaledIdentity(1.0);
            Vec<2> grad = theta;
            for (const Sample& s : data)
            {
                double p = Sigmoid(theta.Dot(s.x));
                AddOuter(H, s.x, p * (1.0 - p));
                grad[0] += (p - s.y) * s.x[0];
                grad[1] += (p - s.y) * s.x[1];
            }
            Mat<2> L;
            CholeskyWithFallback(H, L, 1.0);
            theta.AddScaled(SolveSPD(L, grad), -1.0);
            double loss = Loss(data, theta);
            NS_TEST_ASSERT_MSG_EQ((loss <= previousLoss + 1e-9), true, "Newton loss must not rise");
            previousLoss = loss;
        }
        NS_TEST_ASSERT_MSG_EQ_TOL(theta[0], ref[0], 0.05, "weight 0 vs reference");
        NS_TEST_ASSERT_MSG_EQ_TOL(theta[1], ref[1], 0.05, "weight 1 vs reference");

        // The online learner (send evidence + one step per label) should also cut the loss.
        CbLearnerModel<2> model;
        Vec<2> zero;
        double lossBefore = Loss(data, zero);
        double clock = 0.0;
        for (const Sample& s : data)
        {
            model.BeginDecision(clock);
            model.AddSendEvidence(s.x);
            model.UpdateWithLabel(s.x, s.y);
            clock += 1.0;
        }
        NS_TEST_ASSERT_MSG_EQ((Loss(data, model.Theta()) < lossBefore), true, "online pass reduces loss");
    }
};

// Spec test 3 (§8.4): Thompson samples have covariance A^-1 and mean thetaHat.
template <int D>
class ThompsonSamplesHaveInverseCovariance : public TestCase
{
  public:
    ThompsonSamplesHaveInverseCovariance()
        : TestCase("D1 test3: Thompson sample covariance ~ A^-1 (D=" + std::to_string(D) + ")")
    {
    }

  private:
    void DoRun() override
    {
        // A known SPD matrix: diagonal d_i = 2 + i, off-diagonal coupling 0.3 between neighbours.
        Mat<D> A;
        for (int i = 0; i < D; ++i)
        {
            A.At(i, i) = 2.0 + i;
            if (i + 1 < D)
            {
                A.At(i, i + 1) = 0.3;
                A.At(i + 1, i) = 0.3;
            }
        }
        CbLearnerModel<D> model;
        Vec<D> theta0;
        for (int i = 0; i < D; ++i)
        {
            theta0[i] = 0.1 * i - 0.2;
        }
        model.ImportHandover({theta0, A}, true, 1.0, 0.0);
        model.BeginDecision(0.0);

        // Reference A^-1, column by column.
        Mat<D> L;
        Mat<D> Acopy = A;
        Cholesky(Acopy, L);
        Mat<D> inverse;
        for (int j = 0; j < D; ++j)
        {
            Vec<D> e;
            e[j] = 1.0;
            Vec<D> col = SolveSPD(L, e);
            for (int i = 0; i < D; ++i)
            {
                inverse.At(i, j) = col[i];
            }
        }

        const int n = 10000;
        std::mt19937 gen(777);
        std::normal_distribution<double> normal(0.0, 1.0);
        std::vector<Vec<D>> draws(n);
        Vec<D> mean;
        for (int s = 0; s < n; ++s)
        {
            Vec<D> z;
            for (int i = 0; i < D; ++i)
            {
                z[i] = normal(gen);
            }
            draws[s] = model.SampleTheta(z);
            mean.AddScaled(draws[s], 1.0 / n);
        }
        double diffSq = 0.0;
        double refSq = 0.0;
        for (int i = 0; i < D; ++i)
        {
            for (int j = 0; j < D; ++j)
            {
                double cov = 0.0;
                for (int s = 0; s < n; ++s)
                {
                    cov += (draws[s][i] - mean[i]) * (draws[s][j] - mean[j]);
                }
                cov /= (n - 1);
                diffSq += (cov - inverse.At(i, j)) * (cov - inverse.At(i, j));
                refSq += inverse.At(i, j) * inverse.At(i, j);
            }
        }
        NS_TEST_ASSERT_MSG_EQ((std::sqrt(diffSq / refSq) < 0.05), true,
                              "relative Frobenius error of covariance must be < 5%");
        for (int i = 0; i < D; ++i)
        {
            NS_TEST_ASSERT_MSG_EQ_TOL(mean[i], theta0[i], 0.02, "sample mean ~ thetaHat");
        }
    }
};

// Spec test 4 (§7.2): receipt window for in-order, gaps, wrap-around, jumps >= 32.
class ReceiptWindowTracksGapsAndWrap : public TestCase
{
  public:
    ReceiptWindowTracksGapsAndWrap()
        : TestCase("D1 test4: ReceiptWindow in-order, gaps, wrap-around, big jumps")
    {
    }

  private:
    void DoRun() override
    {
        ReceiptWindow inOrder;
        inOrder.Mark(10);
        inOrder.Mark(11);
        inOrder.Mark(12);
        NS_TEST_ASSERT_MSG_EQ(inOrder.Highest(), 12, "highest in order");
        NS_TEST_ASSERT_MSG_EQ(inOrder.Mask(), 0x7u, "three bits set");

        ReceiptWindow gap;
        gap.Mark(10);
        gap.Mark(13); // 11 and 12 missing
        NS_TEST_ASSERT_MSG_EQ(gap.Mask(), 0x9u, "bits 0 and 3");
        NS_TEST_ASSERT_MSG_EQ((LookupReceipt(13, gap.Mask(), 12) == ReceiptStatus::Missing), true, "12 lost");
        NS_TEST_ASSERT_MSG_EQ((LookupReceipt(13, gap.Mask(), 10) == ReceiptStatus::Received), true, "10 got");
        gap.Mark(11); // late arrival fills bit 2
        NS_TEST_ASSERT_MSG_EQ(gap.Mask(), 0xDu, "late arrival sets its bit");

        ReceiptWindow wrap;
        wrap.Mark(65534);
        wrap.Mark(65535);
        wrap.Mark(0);
        wrap.Mark(1);
        NS_TEST_ASSERT_MSG_EQ(wrap.Highest(), 1, "highest after wrap");
        NS_TEST_ASSERT_MSG_EQ(wrap.Mask(), 0xFu, "four consecutive across the wrap");

        ReceiptWindow jump;
        jump.Mark(5);
        jump.Mark(100); // jump >= 32 clears the old history
        NS_TEST_ASSERT_MSG_EQ(jump.Mask(), 0x1u, "big jump resets the mask");
        jump.Mark(70); // k = 30, inside the window
        const uint32_t expectedMask = 0x1u | (1u << 30);
        NS_TEST_ASSERT_MSG_EQ(jump.Mask(), expectedMask, "k = 30 recorded");
        jump.Mark(60); // k = 40, too old: ignored
        NS_TEST_ASSERT_MSG_EQ(jump.Mask(), expectedMask, "k = 40 ignored");
        NS_TEST_ASSERT_MSG_EQ((LookupReceipt(100, jump.Mask(), 60) == ReceiptStatus::OutOfWindow), true,
                              "older than the window carries no information");
        NS_TEST_ASSERT_MSG_EQ((LookupReceipt(100, jump.Mask(), 101) == ReceiptStatus::OutOfWindow), true,
                              "newer than highest carries no information");
    }
};

// Spec test 5 (§6.1): RFC 1982 serial-number comparison edge cases.
class SerialNumberArithmeticFollowsRfc1982 : public TestCase
{
  public:
    SerialNumberArithmeticFollowsRfc1982()
        : TestCase("D1 test5: RFC 1982 serial comparison")
    {
    }

  private:
    void DoRun() override
    {
        NS_TEST_ASSERT_MSG_EQ(SerialGreater(1, 0), true, "1 > 0");
        NS_TEST_ASSERT_MSG_EQ(SerialGreater(0, 1), false, "0 < 1");
        NS_TEST_ASSERT_MSG_EQ(SerialGreater(5, 5), false, "equal is not greater");
        NS_TEST_ASSERT_MSG_EQ(SerialGreater(0, 65535), true, "0 follows 65535");
        NS_TEST_ASSERT_MSG_EQ(SerialGreater(65535, 0), false, "65535 precedes 0");
        NS_TEST_ASSERT_MSG_EQ(SerialGreater(32767, 0), true, "distance 2^15 - 1 is newer");
        NS_TEST_ASSERT_MSG_EQ(SerialGreater(32768, 0), false, "distance 2^15 is undefined: false");
        NS_TEST_ASSERT_MSG_EQ(SerialGreater(0, 32768), false, "distance 2^15 is undefined: false");
        NS_TEST_ASSERT_MSG_EQ(SerialDiff(0, 65535), 1, "diff across the wrap");
        NS_TEST_ASSERT_MSG_EQ(SerialDiff(65535, 0), -1, "negative diff across the wrap");
        NS_TEST_ASSERT_MSG_EQ(SerialDiff(100, 70), 30, "plain diff");
    }
};

// Spec test 7 (§8.3): with no data, forgetting drives A to lambda0*I and theta to thetaCell.
class ForgettingDecaysToThePrior : public TestCase
{
  public:
    ForgettingDecaysToThePrior()
        : TestCase("D1 test7: forgetting decays A -> lambda0*I and theta -> thetaCell")
    {
    }

  private:
    void DoRun() override
    {
        CbLearnerModel<9> model;
        Vec<9> prior;
        Vec<9> x;
        for (int i = 0; i < 9; ++i)
        {
            prior[i] = 0.1 * (i + 1);
            x[i] = (i % 2) ? 0.7 : 0.3;
        }
        model.SetCellPrior(prior);
        double now = 0.0;
        for (int k = 0; k < 30; ++k)
        {
            model.BeginDecision(now);
            model.AddSendEvidence(x);
            model.UpdateWithLabel(x, k % 2);
            now += 5.0;
        }
        NS_TEST_ASSERT_MSG_EQ((model.TraceOwn() > 9.0 + 0.1), true, "evidence accumulated");

        // One forgetting period (60 s) keeps a fraction gamma of the evidence.
        model.BeginDecision(now); // align the clock so exactly 60 s elapse below
        double traceBefore = model.TraceOwn();
        model.BeginDecision(now + 60.0);
        double expected = 0.98 * traceBefore + 0.02 * 9.0;
        NS_TEST_ASSERT_MSG_EQ_TOL(model.TraceOwn(), expected, 1e-9, "w = gamma after T0");

        // Effectively infinite time: back to the prior.
        model.BeginDecision(now + 1e9);
        for (int i = 0; i < 9; ++i)
        {
            NS_TEST_ASSERT_MSG_EQ_TOL(model.Theta()[i], prior[i], 1e-9, "theta -> thetaCell");
            for (int j = 0; j < 9; ++j)
            {
                const double expectedEntry = (i == j) ? 1.0 : 0.0;
                NS_TEST_ASSERT_MSG_EQ_TOL(model.OwnPrecision().At(i, j), expectedEntry, 1e-9,
                                          "A -> lambda0 I");
            }
        }
    }
};

// Spec test 8 (§8.9): the energy price rises when overspending, falls when underspending, and is >= 0.
class EnergyPriceMovesTheRightWayAndStaysNonNegative : public TestCase
{
  public:
    EnergyPriceMovesTheRightWayAndStaysNonNegative()
        : TestCase("D1 test8: energy price direction and floor at 0")
    {
    }

  private:
    void DoRun() override
    {
        EnergyPrice::Config cfg; // E0 = 300 J, target 1e5 s => budget 0.003 W; tick 600 s
        NS_TEST_ASSERT_MSG_EQ_TOL(EnergyPrice(cfg, 300.0).BudgetRateW(), 0.003, 1e-12, "budget rate");

        EnergyPrice over(cfg, 300.0);
        double before = over.Lambda();
        over.Tick(294.0); // 6 J in 600 s = 0.01 W > 0.003 W
        NS_TEST_ASSERT_MSG_EQ_TOL(over.Lambda(), before + 20.0 * (0.01 - 0.003), 1e-9, "overspend step");

        EnergyPrice under(cfg, 300.0);
        before = under.Lambda();
        under.Tick(300.0); // spent nothing
        NS_TEST_ASSERT_MSG_EQ_TOL(under.Lambda(), before - 20.0 * 0.003, 1e-9, "underspend step");

        for (int i = 0; i < 100; ++i)
        {
            under.Tick(300.0);
            NS_TEST_ASSERT_MSG_EQ((under.Lambda() >= 0.0), true, "price never negative");
        }
        NS_TEST_ASSERT_MSG_EQ_TOL(under.Lambda(), 0.0, 1e-12, "price floors at 0");
    }
};

// K§4 D1 new test (D§3 P8): the same shared message applied twice must not double count.
class SharedEvidenceIsNotDoubleCounted : public TestCase
{
  public:
    SharedEvidenceIsNotDoubleCounted()
        : TestCase("D1 new: incremental sharing does not double count")
    {
    }

  private:
    void DoRun() override
    {
        CbLearnerModel<9> model;
        model.BeginDecision(0.0);
        double ownTrace = model.TraceOwn();

        Vec<9> evidence;
        for (int i = 0; i < 9; ++i)
        {
            evidence[i] = 2.0;
        }
        model.MergeSharedEvidence(7, evidence, 0.0);
        double logdetOnce = model.LogDetEffective();
        model.MergeSharedEvidence(7, evidence, 0.0); // identical message again
        NS_TEST_ASSERT_MSG_EQ_TOL(model.LogDetEffective(), logdetOnce, 1e-12, "same message twice");
        NS_TEST_ASSERT_MSG_EQ_TOL(model.EffectivePrecision().Trace(), ownTrace + 9 * 2.0, 1e-12,
                                  "effective trace = own + one copy");

        // A newer message from the same source REPLACES the old contribution.
        Vec<9> bigger;
        for (int i = 0; i < 9; ++i)
        {
            bigger[i] = 5.0;
        }
        model.MergeSharedEvidence(7, bigger, 0.0);
        NS_TEST_ASSERT_MSG_EQ_TOL(model.EffectivePrecision().Trace(), ownTrace + 9 * 5.0, 1e-12,
                                  "replaced, not added");

        // A second, different source adds its own contribution.
        model.MergeSharedEvidence(8, evidence, 0.0);
        NS_TEST_ASSERT_MSG_EQ_TOL(model.EffectivePrecision().Trace(), ownTrace + 9 * 5.0 + 9 * 2.0, 1e-12,
                                  "second source adds");
        model.DropSource(8);
        NS_TEST_ASSERT_MSG_EQ_TOL(model.EffectivePrecision().Trace(), ownTrace + 9 * 5.0, 1e-12,
                                  "dropped source removed");

        // Shared evidence ages like own evidence: after one T0 only a fraction gamma is left.
        model.BeginDecision(60.0);
        NS_TEST_ASSERT_MSG_EQ_TOL(model.EffectivePrecision().Trace(),
                                  model.TraceOwn() + 0.98 * 9 * 5.0, 1e-9, "shared evidence decays");
    }
};

// K§2.3 / D§3 P8: planned handover is unscaled, unplanned takeover scales A by betaHandover.
class HandoverScalesOnlyWhenUnplanned : public TestCase
{
  public:
    HandoverScalesOnlyWhenUnplanned()
        : TestCase("D1 new: planned handover keeps A, unplanned scales it")
    {
    }

  private:
    void DoRun() override
    {
        CbLearnerModel<9> oldHead;
        Vec<9> x;
        for (int i = 0; i < 9; ++i)
        {
            x[i] = 0.5;
        }
        for (int k = 0; k < 20; ++k)
        {
            oldHead.BeginDecision(k);
            oldHead.AddSendEvidence(x);
            oldHead.UpdateWithLabel(x, 1);
        }
        auto snapshot = oldHead.ExportSnapshot();

        CbLearnerModel<9> planned;
        planned.ImportHandover(snapshot, true, 0.7, 20.0);
        CbLearnerModel<9> unplanned;
        unplanned.ImportHandover(snapshot, false, 0.7, 20.0);
        for (int i = 0; i < 9; ++i)
        {
            NS_TEST_ASSERT_MSG_EQ_TOL(planned.Theta()[i], snapshot.theta[i], 1e-12, "theta copied");
            NS_TEST_ASSERT_MSG_EQ_TOL(unplanned.Theta()[i], snapshot.theta[i], 1e-12, "theta copied");
        }
        NS_TEST_ASSERT_MSG_EQ_TOL(planned.TraceOwn(), snapshot.A.Trace(), 1e-9, "planned: unscaled");
        NS_TEST_ASSERT_MSG_EQ_TOL(unplanned.TraceOwn(), 0.7 * snapshot.A.Trace(), 1e-9, "unplanned: x0.7");

        // Inflation after a death pulls A toward lambda0*I: trace goes down but not below the prior.
        double traceBefore = planned.TraceOwn();
        planned.InflateAfterLoss(0.7);
        NS_TEST_ASSERT_MSG_EQ_TOL(planned.TraceOwn(), 0.7 * traceBefore + 0.3 * 9.0, 1e-9, "inflation");
    }
};

// Spec §8.10: cell prior is the evidence-weighted mean of accepted peer thetas.
class CellPriorAveragesAcceptedPeers : public TestCase
{
  public:
    CellPriorAveragesAcceptedPeers()
        : TestCase("D1 new: cell prior weights peers and applies the cosine gate")
    {
    }

  private:
    void DoRun() override
    {
        Vec<3> self;
        self[0] = 1.0;
        std::vector<PeerBlob<3>> peers(2);
        peers[0].theta[0] = 2.0;     // same direction as self
        peers[0].diagA[0] = 3.0;     // weight s = 3
        peers[1].theta[1] = 4.0;     // orthogonal to self (cosine 0)
        peers[1].diagA[1] = 1.0;     // weight s = 1

        Vec<3> out;
        // Mature node: the orthogonal peer is rejected, so the prior is peer 0's theta.
        NS_TEST_ASSERT_MSG_EQ(ComputeCellPrior(self, 100.0, peers, 50.0, 0.5, out), true, "accepted");
        NS_TEST_ASSERT_MSG_EQ_TOL(out[0], 2.0, kTol, "only peer 0");
        NS_TEST_ASSERT_MSG_EQ_TOL(out[1], 0.0, kTol, "peer 1 gated out");

        // Immature node (trace < 50) accepts both, weighted 3:1.
        NS_TEST_ASSERT_MSG_EQ(ComputeCellPrior(self, 10.0, peers, 50.0, 0.5, out), true, "accepted");
        NS_TEST_ASSERT_MSG_EQ_TOL(out[0], (3.0 * 2.0) / 4.0, kTol, "weighted x");
        NS_TEST_ASSERT_MSG_EQ_TOL(out[1], (1.0 * 4.0) / 4.0, kTol, "weighted y");

        Vec<3> untouched;
        untouched[2] = 9.0;
        std::vector<PeerBlob<3>> none;
        NS_TEST_ASSERT_MSG_EQ(ComputeCellPrior(self, 10.0, none, 50.0, 0.5, untouched), false, "no peers");
        NS_TEST_ASSERT_MSG_EQ_TOL(untouched[2], 9.0, kTol, "left unchanged");
    }
};


// ============================================================ D2 ===========

// Spec test 10 (§4.1): with the preamble patch a 150 B frame at 5 kbit/s and TPre = 0.3 s lasts 0.54 s.
class AirtimeIncludesTheExactPreamble : public TestCase
{
  public:
    AirtimeIncludesTheExactPreamble()
        : TestCase("D2 test10: airtime is preamble plus bits over rate (patch P1)")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<AquaSimPhyCmn> phy = CreateObject<AquaSimPhyCmn>();
        phy->SetAttribute("Preamble", DoubleValue(0.3));
        phy->Modulation(nullptr)->SetAttribute("SPS", UintegerValue(5000)); // 5 kbit/s, 1 bit/symbol

        // Upstream behaviour (patch off): the 0.3 s preamble is truncated to 0 s.
        NS_TEST_ASSERT_MSG_EQ_TOL(phy->CalcTxTime(150).GetSeconds(), 0.24, 1e-6, "upstream truncates");

        phy->SetAttribute("ExactPreamble", BooleanValue(true));
        NS_TEST_ASSERT_MSG_EQ_TOL(phy->CalcTxTime(150).GetSeconds(), 0.54, 1e-6, "0.3 + 0.24 s");

        phy->SetAttribute("Preamble", DoubleValue(0.0));
        NS_TEST_ASSERT_MSG_EQ_TOL(phy->CalcTxTime(150).GetSeconds(), 0.24, 1e-6, "TPre = 0");
        phy->SetAttribute("Preamble", DoubleValue(1.0));
        NS_TEST_ASSERT_MSG_EQ_TOL(phy->CalcTxTime(150).GetSeconds(), 1.24, 1e-6, "TPre = 1 s");
        Simulator::Destroy();
    }
};

// Spec §4.4: Thorp and Stojanovic values, and the source level is calibrated to PER = 0.5 at R.
class LinkBudgetIsCalibratedAtRange : public TestCase
{
  public:
    LinkBudgetIsCalibratedAtRange()
        : TestCase("D2: link budget, calibration at R, monotone PER")
    {
    }

  private:
    void DoRun() override
    {
        AcousticLinkModel link;
        // Thorp at 20 kHz: 0.11*400/401 + 44*400/4500 + 2.75e-4*400 + 0.003 = 4.1338 dB/km.
        NS_TEST_ASSERT_MSG_EQ_TOL(AcousticLinkModel::ThorpDbPerKm(20.0), 4.133837, 1e-5, "Thorp");
        double psd = AcousticLinkModel::NoisePsdDb(20.0, 0.5, 5.0);
        NS_TEST_ASSERT_MSG_EQ((psd > 39.5 && psd < 41.5), true, "wind-dominated noise ~ 40 dB at 20 kHz");

        // TL(1500 m) = 15 log10(1500) + 4.1338 * 1.5.
        double expectedTl = 15.0 * std::log10(1500.0) + 4.133837 * 1.5;
        NS_TEST_ASSERT_MSG_EQ_TOL(link.TransmissionLossDb(1500.0), expectedTl, 1e-4, "TL at R");

        // Calibration: PER(150 B) = 0.5 at R with no shadowing at full power.
        double sl = link.FullSourceLevelDb();
        double snrAtR = link.SnrDb(sl, 1500.0, 0.0);
        NS_TEST_ASSERT_MSG_EQ_TOL(link.PerFromSnr(snrAtR, 150), 0.5, 1e-9, "PER(R) = 0.5");

        // PER rises with distance and falls with power.
        double previous = -1.0;
        for (double d = 100.0; d <= 1500.0; d += 100.0)
        {
            double per = link.PerFromSnr(link.SnrDb(sl, d, 0.0), 150);
            NS_TEST_ASSERT_MSG_EQ((per >= previous), true, "PER non-decreasing in distance");
            previous = per;
        }
        double perFull = link.PerFromSnr(link.SnrDb(link.SourceLevelDb(0.2818), 1400.0, 0.0), 150);
        double perQuarter = link.PerFromSnr(link.SnrDb(link.SourceLevelDb(0.2818 * 0.25), 1400.0, 0.0), 150);
        NS_TEST_ASSERT_MSG_EQ((perQuarter > perFull), true, "lower power, more loss");
        NS_TEST_ASSERT_MSG_EQ_TOL(link.SourceLevelDb(0.2818 * 0.25), sl - 6.0206, 1e-3, "quarter power = -6 dB");

        // SnrReqDb is the SNR at which PER = 0.1; margin at R is therefore slightly negative.
        NS_TEST_ASSERT_MSG_EQ_TOL(link.PerFromSnr(link.SnrReqDb(150), 150), 0.1, 1e-9, "SnrReq gives PER 0.1");
        NS_TEST_ASSERT_MSG_EQ((link.MarginDb(sl, 1500.0, 150) < 0.0), true, "margin at R < 0");
        NS_TEST_ASSERT_MSG_EQ((link.MarginDb(sl, 500.0, 150) > 5.0), true, "comfortable margin at 500 m");

        // Longer frames fail more often at the same SNR.
        NS_TEST_ASSERT_MSG_EQ((link.PerFromSnr(snrAtR, 300) > link.PerFromSnr(snrAtR, 150)), true, "length");
        // Shorter frames (beacons, ACKs) are more robust: ACK of 6 B at R.
        NS_TEST_ASSERT_MSG_EQ((link.PerFromSnr(snrAtR, 6) < 0.05), true, "short frames survive at R");
    }
};

// Spec §4.5: stationary std and mean, correlation e^-1 at one tau, symmetry, and order independence.
class ShadowingIsGaussMarkovAndAccessOrderFree : public TestCase
{
  public:
    ShadowingIsGaussMarkovAndAccessOrderFree()
        : TestCase("D2: shadowing statistics, symmetry, order independence")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<GmShadowing> sh = CreateObject<GmShadowing>();
        sh->SetBaseSeed(42);
        const int links = 3000;
        const double sigma = 5.0;
        const double tau = 120.0;

        double sum = 0, sumSq = 0, cross = 0, sumLater = 0, sumLaterSq = 0, crossFar = 0;
        for (int l = 0; l < links; ++l)
        {
            double a = sh->GetShadowDb(l, l + 5000, Seconds(1000));
            double b = sh->GetShadowDb(l, l + 5000, Seconds(1000 + tau));
            double c = sh->GetShadowDb(l, l + 5000, Seconds(1000 + tau + 10 * tau));
            sum += a;
            sumSq += a * a;
            sumLater += b;
            sumLaterSq += b * b;
            cross += a * b;
            crossFar += b * c;
        }
        double mean = sum / links;
        double std = std::sqrt(sumSq / links - mean * mean);
        NS_TEST_ASSERT_MSG_EQ_TOL(mean, 0.0, 0.3, "mean ~ 0");
        NS_TEST_ASSERT_MSG_EQ_TOL(std, sigma, 0.3, "std ~ sigma");

        double meanLater = sumLater / links;
        double stdLater = std::sqrt(sumLaterSq / links - meanLater * meanLater);
        double corrTau = (cross / links - mean * meanLater) / (std * stdLater);
        NS_TEST_ASSERT_MSG_EQ_TOL(corrTau, std::exp(-1.0), 0.06, "correlation at one tau");
        NS_TEST_ASSERT_MSG_EQ_TOL(crossFar / links / (sigma * sigma), 0.0, 0.08, "uncorrelated at 10 tau");

        // Symmetric in the two node ids.
        NS_TEST_ASSERT_MSG_EQ_TOL(sh->GetShadowDb(3, 9, Seconds(50)), sh->GetShadowDb(9, 3, Seconds(50)), 0.0,
                                  "symmetric");

        // Order independence: asking at t = 100 and 200 first does not change the value at t = 500.
        Ptr<GmShadowing> direct = CreateObject<GmShadowing>();
        direct->SetBaseSeed(7);
        Ptr<GmShadowing> peeked = CreateObject<GmShadowing>();
        peeked->SetBaseSeed(7);
        peeked->GetShadowDb(1, 2, Seconds(100));
        peeked->GetShadowDb(1, 2, Seconds(200));
        NS_TEST_ASSERT_MSG_EQ_TOL(direct->GetShadowDb(1, 2, Seconds(500)),
                                  peeked->GetShadowDb(1, 2, Seconds(500)), 0.0, "peeking is harmless");

        // sigma = 0 switches shadowing off.
        Ptr<GmShadowing> off = CreateObject<GmShadowing>();
        off->SetAttribute("ShadowSigmaDb", DoubleValue(0.0));
        NS_TEST_ASSERT_MSG_EQ_TOL(off->GetShadowDb(1, 2, Seconds(10)), 0.0, 0.0, "sigma 0");
    }
};


// ============================================================ D3 ===========

// Spec test 9 (§6.5): serialise/deserialise round trips, and GetSerializedSize equals the bytes written.
class HeadersRoundTripWithExactSizes : public TestCase
{
  public:
    HeadersRoundTripWithExactSizes()
        : TestCase("D3 test9: header round trips and exact sizes")
    {
    }

  private:
    template <typename H>
    void CheckSize(const H& header, uint32_t expected, const std::string& name)
    {
        Ptr<Packet> p = Create<Packet>(0);
        p->AddHeader(header);
        NS_TEST_ASSERT_MSG_EQ(header.GetSerializedSize(), expected, name << " declared size");
        NS_TEST_ASSERT_MSG_EQ(p->GetSize(), expected, name << " bytes actually written");
    }

    void DoRun() override
    {
        ErgrDataHeader d;
        d.senderId = 11;
        d.forwarderId = 22;
        d.sourceId = 33;
        d.sourceSeq = 65535;
        d.hpkt = 4;
        d.hopSeq = 40000;
        d.SetFlags(true, false, 2);
        d.genTimeMs = 123456789;
        CheckSize(d, 17, "DATA");
        Ptr<Packet> p = Create<Packet>(150);
        p->AddHeader(d);
        NS_TEST_ASSERT_MSG_EQ(PeekFrameType(p), static_cast<uint8_t>(FrameType::Data), "type byte");
        ErgrDataHeader d2;
        p->RemoveHeader(d2);
        NS_TEST_ASSERT_MSG_EQ(p->GetSize(), 150u, "payload untouched");
        NS_TEST_ASSERT_MSG_EQ(d2.forwarderId, 22, "forwarder");
        NS_TEST_ASSERT_MSG_EQ(d2.sourceSeq, 65535, "seq");
        NS_TEST_ASSERT_MSG_EQ(d2.hopSeq, 40000, "hopSeq");
        NS_TEST_ASSERT_MSG_EQ(d2.genTimeMs, 123456789u, "genTime");
        NS_TEST_ASSERT_MSG_EQ(d2.GetAckRequest(), true, "AR");
        NS_TEST_ASSERT_MSG_EQ(d2.GetPriority(), false, "priority");
        NS_TEST_ASSERT_MSG_EQ(int(d2.GetPowerLevel()), 2, "power level");

        ErgrAckHeader a;
        a.ackerId = 5;
        a.senderId = 6;
        a.hopSeq = 7;
        CheckSize(a, 7, "ACK");

        // Beacon without and with receipts and blob; negative position/velocity must survive.
        ErgrBeaconHeader b;
        b.nodeId = 9;
        b.cellId = 63;
        b.SetRole(true, 2, false);
        b.hmin = 3;
        b.pos[0] = -12;
        b.pos[1] = 2999;
        b.pos[2] = 0;
        b.velMmps[0] = -1500;
        b.velMmps[1] = 20;
        b.velMmps[2] = 32000;
        b.battery = 200;
        b.queueLen = 4;
        b.nextBeaconDs = 500;
        CheckSize(b, 25, "BEACON base");
        b.receipts.push_back({3, 65535, 0xF0F0F0F0u});
        b.receipts.push_back({4, 1, 0x1u});
        CheckSize(b, 25 + 16, "BEACON + 2 receipts");
        b.hasBlob = true;
        for (int i = 0; i < ModelBlob::kDim; ++i)
        {
            b.blob.theta[i] = static_cast<int16_t>(-1000 * i);
            b.blob.diagA[i] = static_cast<uint16_t>(60000 + i);
        }
        CheckSize(b, 25 + 16 + 36, "BEACON + receipts + blob");
        Ptr<Packet> bp = Create<Packet>(0);
        bp->AddHeader(b);
        ErgrBeaconHeader b2;
        bp->RemoveHeader(b2);
        NS_TEST_ASSERT_MSG_EQ(bp->GetSize(), 0u, "all bytes consumed");
        NS_TEST_ASSERT_MSG_EQ(b2.nodeId, 9, "node");
        NS_TEST_ASSERT_MSG_EQ(b2.IsHead(), true, "head");
        NS_TEST_ASSERT_MSG_EQ(int(b2.HeadRank()), 2, "rank");
        NS_TEST_ASSERT_MSG_EQ(b2.IsSink(), false, "sink");
        NS_TEST_ASSERT_MSG_EQ(int(b2.pos[0]), -12, "negative pos");
        NS_TEST_ASSERT_MSG_EQ(int(b2.velMmps[0]), -1500, "negative vel");
        NS_TEST_ASSERT_MSG_EQ(int(b2.velMmps[2]), 32000, "large vel");
        NS_TEST_ASSERT_MSG_EQ(b2.receipts.size(), 2u, "two receipts");
        NS_TEST_ASSERT_MSG_EQ(b2.receipts[0].mask, 0xF0F0F0F0u, "mask");
        NS_TEST_ASSERT_MSG_EQ(b2.receipts[0].highest, 65535, "highest");
        NS_TEST_ASSERT_MSG_EQ(b2.hasBlob, true, "blob flag");
        NS_TEST_ASSERT_MSG_EQ(int(b2.blob.theta[3]), -3000, "blob theta");
        NS_TEST_ASSERT_MSG_EQ(int(b2.blob.diagA[8]), 60008, "blob diag");
        NS_TEST_ASSERT_MSG_EQ(b2.BaseBytes() + b2.ReceiptBytes() + b2.BlobBytes(), b2.GetSerializedSize(),
                              "size classes add up");

        ErgrElectionHeader e;
        e.kind = ErgrElectionHeader::Retire;
        e.nodeId = 1;
        e.cellId = 2;
        e.rank = 2;
        e.scoreQ = 65535;
        CheckSize(e, 9, "ELECTION");
        e.kind = ErgrElectionHeader::DeadReport; // reporter in nodeId, silent head's cell in cellId, its id in scoreQ
        e.scoreQ = 1234;
        CheckSize(e, 9, "ELECTION (dead report)");
        ErgrNackHeader n;
        n.nackerId = 1;
        n.upstreamId = 2;
        n.sourceId = 3;
        n.sourceSeq = 4;
        CheckSize(n, 9, "NACK");
    }
};

// Grid geometry (K§2.1): 4^3 cells at 750 m, 26 neighbours inside, fewer at borders, 6 face-only.
class GridGeometryIndexesCellsAndNeighbours : public TestCase
{
  public:
    GridGeometryIndexesCellsAndNeighbours()
        : TestCase("D3: grid cells, neighbours and hysteresis margin")
    {
    }

  private:
    void DoRun() override
    {
        GridGeometry g(3000.0, 750.0);
        NS_TEST_ASSERT_MSG_EQ(g.CellsPerAxis(), 4, "3000 / 750");
        NS_TEST_ASSERT_MSG_EQ(g.NumCells(), 64, "64 cells");
        NS_TEST_ASSERT_MSG_EQ(g.CellIdOf(Vector(10, 10, 10)), 0, "origin cell");
        NS_TEST_ASSERT_MSG_EQ(g.CellIdOf(Vector(800, 10, 10)), 1, "next in x");
        NS_TEST_ASSERT_MSG_EQ(g.CellIdOf(Vector(10, 800, 10)), 4, "next in y");
        NS_TEST_ASSERT_MSG_EQ(g.CellIdOf(Vector(10, 10, 800)), 16, "next in z");
        NS_TEST_ASSERT_MSG_EQ(g.CellIdOf(Vector(-50, 5000, 3000)), 0 + 4 * 3 + 16 * 3, "clamped into the box");

        uint16_t interior = g.CellIdOf(Vector(1125, 1125, 1125)); // cell (1,1,1)
        NS_TEST_ASSERT_MSG_EQ(g.NeighbourCells(interior, false).size(), 26u, "interior: 26");
        NS_TEST_ASSERT_MSG_EQ(g.NeighbourCells(interior, true).size(), 6u, "interior face-only: 6");
        NS_TEST_ASSERT_MSG_EQ(g.NeighbourCells(0, false).size(), 7u, "corner: 7");
        NS_TEST_ASSERT_MSG_EQ(g.NeighbourCells(0, true).size(), 3u, "corner face-only: 3");
        NS_TEST_ASSERT_MSG_EQ(g.AreNeighbours(0, 1, true), true, "face neighbours");
        NS_TEST_ASSERT_MSG_EQ(g.AreNeighbours(0, 5, true), false, "edge neighbour is not face-only");
        NS_TEST_ASSERT_MSG_EQ(g.AreNeighbours(0, 5, false), true, "edge neighbour");
        NS_TEST_ASSERT_MSG_EQ(g.AreNeighbours(0, 2, false), false, "two cells away");
        NS_TEST_ASSERT_MSG_EQ(g.AreNeighbours(7, 7, false), false, "not its own neighbour");

        Vector c = g.CellCentre(interior);
        NS_TEST_ASSERT_MSG_EQ_TOL(c.x, 1125.0, 1e-9, "centre x");
        NS_TEST_ASSERT_MSG_EQ_TOL(g.MaxCentreDistanceM(), 0.5 * std::sqrt(3.0) * 750.0, 1e-9, "half diagonal");
        NS_TEST_ASSERT_MSG_EQ_TOL(g.InsideMarginM(c, interior), 375.0, 1e-9, "centre is 375 m from every face");
        NS_TEST_ASSERT_MSG_EQ_TOL(g.InsideMarginM(Vector(1500 + 10, 1125, 1125), interior), -10.0, 1e-9,
                                  "outside the cell is negative");
        // 600 m cells: 5 per axis.
        NS_TEST_ASSERT_MSG_EQ(GridGeometry(3000.0, 600.0).CellsPerAxis(), 5, "5^3 cells at 600 m");
        NS_TEST_ASSERT_MSG_EQ(GridGeometry(3000.0, 866.0).CellsPerAxis(), 4, "R/sqrt(3) cells");
    }
};

// Neighbour table (§7.1): update, head change detection, expiry, entry age reset when a node becomes head.
class NeighbourTableTracksHeadsAndExpires : public TestCase
{
  public:
    NeighbourTableTracksHeadsAndExpires()
        : TestCase("D3: neighbour table update, head change, expiry")
    {
    }

  private:
    void DoRun() override
    {
        NeighbourTable table;
        NeighbourEntry e;
        e.id = 7;
        e.cellId = 3;
        e.beaconTime = Seconds(10);
        TableUpdateResult r = table.Update(e);
        NS_TEST_ASSERT_MSG_EQ(r.isNew, true, "first beacon creates the row");
        e.beaconTime = Seconds(60);
        r = table.Update(e);
        NS_TEST_ASSERT_MSG_EQ(r.isNew, false, "second beacon refreshes");
        NS_TEST_ASSERT_MSG_EQ(r.headStatusChanged, false, "no change");
        NS_TEST_ASSERT_MSG_EQ_TOL(table.Find(7)->firstHeard.GetSeconds(), 10.0, 1e-9, "age kept");

        e.beaconTime = Seconds(110);
        e.isHead = true;
        e.headRank = 1;
        r = table.Update(e);
        NS_TEST_ASSERT_MSG_EQ(r.headStatusChanged, true, "became head");
        NS_TEST_ASSERT_MSG_EQ_TOL(table.Find(7)->firstHeard.GetSeconds(), 110.0, 1e-9, "age reset on becoming head (K§2.3)");

        NeighbourEntry other;
        other.id = 8;
        other.beaconTime = Seconds(200);
        table.Update(other);
        std::vector<uint16_t> removed = table.Expire(Seconds(300), Seconds(150));
        NS_TEST_ASSERT_MSG_EQ(removed.size(), 1u, "node 7 (last heard 110 s) is stale at 300 s");
        NS_TEST_ASSERT_MSG_EQ(int(removed[0]), 7, "removed id");
        NS_TEST_ASSERT_MSG_EQ(table.Size(), 1u, "node 8 stays");

        // Position prediction uses the advertised velocity.
        NeighbourEntry moving;
        moving.id = 9;
        moving.pos = Vector(100, 0, 0);
        moving.vel = Vector(2, 0, -1);
        moving.beaconTime = Seconds(10);
        Vector p = moving.PredictedPos(Seconds(20));
        NS_TEST_ASSERT_MSG_EQ_TOL(p.x, 120.0, 1e-9, "x");
        NS_TEST_ASSERT_MSG_EQ_TOL(p.z, -10.0, 1e-9, "z");
    }
};

// Duplicate filter (§7): capacity and TTL.
class RecentSetRemembersWithinCapacityAndTtl : public TestCase
{
  public:
    RecentSetRemembersWithinCapacityAndTtl()
        : TestCase("D3: duplicate filter capacity and TTL")
    {
    }

  private:
    void DoRun() override
    {
        RecentSet set(3, Seconds(100));
        NS_TEST_ASSERT_MSG_EQ(set.Insert({1, 1}, Seconds(0)), true, "new");
        NS_TEST_ASSERT_MSG_EQ(set.Insert({1, 1}, Seconds(1)), false, "duplicate");
        set.Insert({1, 2}, Seconds(2));
        set.Insert({1, 3}, Seconds(3));
        set.Insert({1, 4}, Seconds(4)); // pushes {1,1} out by capacity
        NS_TEST_ASSERT_MSG_EQ(set.Insert({1, 1}, Seconds(5)), true, "forgotten by capacity");
        NS_TEST_ASSERT_MSG_EQ(set.Insert({1, 4}, Seconds(6)), false, "still remembered");
        NS_TEST_ASSERT_MSG_EQ(set.Insert({1, 4}, Seconds(500)), true, "forgotten by TTL");
    }
};


// Run a small complete scenario and return the result; the simulator is destroyed afterwards.
static ScenarioResult
RunTinyScenario(const std::string& outDir, int seed, double speed, int nodes = 30, double simTimeS = 700)
{
    ScenarioSettings settings;
    settings.outDir = outDir;
    settings.seed = seed;
    settings.numNodes = nodes;
    settings.simTimeS = simTimeS;
    settings.warmupS = 300;
    settings.pktRateHz = 0.01;
    settings.currentSpeed = speed;
    ScenarioConfig config;
    RegisterSettings(config, settings);
    return RunScenario(settings, config);
}

static std::string
ReadFile(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Spec test 15: the same seed and run give byte-identical output files.
class SameSeedGivesIdenticalOutput : public TestCase
{
  public:
    SameSeedGivesIdenticalOutput()
        : TestCase("D3 test15: determinism (same seed, identical CSVs)")
    {
    }

  private:
    void DoRun() override
    {
        std::string base = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-det";
        RunTinyScenario(base + "-a", 7, 1.0);
        RunTinyScenario(base + "-b", 7, 1.0);
        RunTinyScenario(base + "-c", 8, 1.0);
        for (const char* file : {"summary.csv", "packets.csv", "nodes.csv", "events.csv"})
        {
            NS_TEST_ASSERT_MSG_EQ((ReadFile(base + "-a/" + file) == ReadFile(base + "-b/" + file)), true,
                                  file << " differs between two runs with the same seed");
        }
        NS_TEST_ASSERT_MSG_EQ((ReadFile(base + "-a/packets.csv") != ReadFile(base + "-c/packets.csv")), true,
                              "a different seed should give a different run");
        std::filesystem::remove_all(base + "-a");
        std::filesystem::remove_all(base + "-b");
        std::filesystem::remove_all(base + "-c");
    }
};

// A whole small run: elections happen, packets are delivered, files are written, and the
// consistency checks of K§2.6 (run inside Finalize) pass.
class SmallScenarioRunsAndIsConsistent : public TestCase
{
  public:
    SmallScenarioRunsAndIsConsistent()
        : TestCase("D3: small scenario runs, delivers packets and passes consistency checks")
    {
    }

  private:
    void DoRun() override
    {
        std::string dir = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-small";
        ScenarioResult r = RunTinyScenario(dir, 3, 1.0);
        NS_TEST_ASSERT_MSG_EQ((r.packetsCreated > 20), true, "traffic was generated");
        NS_TEST_ASSERT_MSG_EQ((r.packetsDelivered > 0), true, "some packets reached a sink");
        NS_TEST_ASSERT_MSG_EQ((r.pdr > 0.0 && r.pdr <= 1.0), true, "PDR in (0, 1]");
        for (const char* file : {"summary.csv", "packets.csv", "nodes.csv", "events.csv", "params_used.txt"})
        {
            NS_TEST_ASSERT_MSG_EQ(std::filesystem::exists(dir + "/" + file), true, file << " written");
        }
        NS_TEST_ASSERT_MSG_EQ((ReadFile(dir + "/events.csv").find("HEAD_ELECTED") != std::string::npos), true,
                              "heads were elected");
        std::filesystem::remove_all(dir);
    }
};


// ============================================================ D4 ===========

namespace
{
using CsvRow = std::map<std::string, std::string>;

// Read a CSV with a header row into a list of rows keyed by column name.
std::vector<CsvRow>
ReadCsv(const std::string& path)
{
    std::vector<CsvRow> rows;
    std::ifstream in(path);
    std::string line;
    std::vector<std::string> header;
    bool first = true;
    while (std::getline(in, line))
    {
        std::vector<std::string> cells;
        std::stringstream ss(line);
        for (std::string cell; std::getline(ss, cell, ',');)
        {
            cells.push_back(cell);
        }
        if (first)
        {
            header = cells;
            first = false;
            continue;
        }
        CsvRow row;
        for (size_t i = 0; i < header.size() && i < cells.size(); ++i)
        {
            row[header[i]] = cells[i];
        }
        rows.push_back(row);
    }
    return rows;
}

// Three static nodes in a vertical line: source S (id 2, deep) -> relay F (id 1) -> sink (id 0, shallow).
// S and F are forced to be heads, so no election is needed. S cannot reach the sink directly (2200 m > R).
struct LineNetwork
{
    Ptr<MetricsCollector> metrics = CreateObject<MetricsCollector>();
    Ptr<CbErgrPropagation> propagation;
    Ptr<ErgrEmhcRouting> sink, relay, source;

    LineNetwork(const std::string& outDir, const std::string& selectorAttributes, double extraLoss)
    {
        MetricsCollector::Config mc;
        mc.outDir = outDir;
        mc.simTimeS = 1.0e9;
        mc.logDecisions = true;
        mc.packetTtlS = 100.0;
        metrics->Configure(mc);

        NodeContainer nodes;
        nodes.Create(3);
        Ptr<AquaSimChannel> channel = CbErgrHelper::CreateChannel(propagation, 0.0, 120.0);
        propagation->SetAttribute("ExtraLossProbability", DoubleValue(extraLoss));
        propagation->AssignStreams(10);
        CbErgrHelper::ConnectLinkTrace(propagation, metrics);

        AquaSimHelper asHelper = AquaSimHelper::Default();
        asHelper.SetChannel(channel);
        asHelper.SetMac("ns3::AquaSimBroadcastMac");
        asHelper.SetRouting("ns3::cbergr::ErgrEmhcRouting");
        CbErgrHelper::ConfigureHelper(asHelper, 0.1, 1e9, 2.0, 0.75, 0.008);

        const Vector positions[3] = {Vector(1100, 1100, 100), Vector(1100, 1100, 1500), Vector(1100, 1100, 2300)};
        Ptr<ErgrEmhcRouting>* roles[3] = {&sink, &relay, &source};
        for (int i = 0; i < 3; ++i)
        {
            Ptr<MobilityModel> mobility = CreateObject<ConstantPositionMobilityModel>();
            nodes.Get(i)->AggregateObject(mobility);
            mobility->SetPosition(positions[i]);
            Ptr<AquaSimNetDevice> device = CreateObject<AquaSimNetDevice>();
            asHelper.Create(nodes.Get(i), device);
            CbErgrHelper::ConfigureDevice(device, 5000.0);
            device->GetMac()->AssignStreams(20 + i);
            Ptr<ErgrEmhcRouting> routing = DynamicCast<ErgrEmhcRouting>(device->GetRouting());
            routing->SetAttribute("AutoElection", BooleanValue(false));
            routing->SetAttribute("PktRatePerNodeHz", DoubleValue(0.0));
            routing->AssignStreams(30 + 2 * i);
            routing->SetNodeRole(i == 0);
            routing->SetMetrics(metrics);
            ObjectFactory factory;
            factory.SetTypeId("ns3::cbergr::SelectorRoundRobin");
            std::stringstream attrs(selectorAttributes);
            for (std::string item; std::getline(attrs, item, ';');)
            {
                size_t eq = item.find('=');
                factory.Set(item.substr(0, eq), StringValue(item.substr(eq + 1)));
            }
            routing->SetSelector(factory.Create<ForwarderSelector>());
            metrics->RegisterNode(i, i == 0, device->GetPhy()->EM());
            *roles[i] = routing;
        }
        for (auto* r : roles)
        {
            (*r)->Start();
        }
        relay->ForceRole(1);
        source->ForceRole(1);
    }

    // Inject 'count' packets at the source, 'gapS' apart, starting at 200 s (beacons have converged by then).
    void Inject(int count, double gapS)
    {
        for (int k = 0; k < count; ++k)
        {
            Simulator::Schedule(Seconds(200.0 + k * gapS), &ErgrEmhcRouting::InjectPacket, source);
        }
    }

    void RunAndFinish(double untilS)
    {
        Simulator::Schedule(Seconds(untilS), &MetricsCollector::Finalize, metrics);
        Simulator::Stop(Seconds(untilS + 0.001));
        Simulator::Run();
        Simulator::Destroy();
    }
};

CsvRow
NodeRow(const std::vector<CsvRow>& nodes, int id)
{
    for (const CsvRow& r : nodes)
    {
        if (std::stoi(r.at("node")) == id)
        {
            return r;
        }
    }
    return CsvRow();
}
} // namespace

// Spec test 12 (§14): 3-node line with 30% forced loss. Every attempt gets exactly one label, receipt
// bitmaps produce explicit 0s, retries use new hopSeqs, and the relay never forwards a duplicate.
class ForcedLossGivesOneLabelPerAttempt : public TestCase
{
  public:
    ForcedLossGivesOneLabelPerAttempt()
        : TestCase("D4 test12: forced loss, one label per attempt, bitmap explicit zeros, fresh hopSeqs")
    {
    }

  private:
    void DoRun() override
    {
        std::string dir = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-line30";
        RngSeedManager::SetSeed(5);
        LineNetwork net(dir, "WithFeedback=true;AckRequest=false", 0.30);
        net.Inject(80, 12.0);
        net.RunAndFinish(1500.0); // 150 s expiry + beacon period after the last packet at 1148 s

        // Every attempt got exactly one label (and none is still pending).
        NS_TEST_ASSERT_MSG_EQ((net.metrics->TotalAttempts() > 80), true, "retries happened");
        NS_TEST_ASSERT_MSG_EQ(net.metrics->TotalLabels(), net.metrics->TotalAttempts(), "one label per attempt");

        std::vector<CsvRow> decisions = ReadCsv(dir + "/decisions.csv");
        std::set<std::string> hopSeqs;
        int bitmapOnes = 0, bitmapZeros = 0, expiry = 0, sourceRows = 0;
        for (const CsvRow& r : decisions)
        {
            if (r.at("node") != "2")
            {
                continue;
            }
            sourceRows++;
            hopSeqs.insert(r.at("hop_seq"));
            bitmapOnes += (r.at("label_source") == "BITMAP" && r.at("label") == "1");
            bitmapZeros += (r.at("label_source") == "BITMAP" && r.at("label") == "0");
            expiry += (r.at("label_source") == "EXPIRY");
        }
        NS_TEST_ASSERT_MSG_EQ(hopSeqs.size(), static_cast<size_t>(sourceRows), "every attempt has its own hopSeq");
        NS_TEST_ASSERT_MSG_EQ((bitmapOnes > 0), true, "bitmap labelled some attempts 1");
        NS_TEST_ASSERT_MSG_EQ((bitmapZeros > 0), true, "bitmap produced explicit 0s");
        NS_TEST_ASSERT_MSG_EQ((expiry > 0), true, "silent attempts ended by expiry, not by success");

        // No duplicate is forwarded downstream: the relay makes one first attempt per distinct packet it accepted.
        std::vector<CsvRow> nodes = ReadCsv(dir + "/nodes.csv");
        CsvRow relayRow = NodeRow(nodes, 1);
        NS_TEST_ASSERT_MSG_EQ(relayRow.at("first_attempts"), relayRow.at("data_accepted"),
                              "relay forwards each accepted packet once");
        NS_TEST_ASSERT_MSG_EQ((std::stoi(relayRow.at("data_duplicates")) > 0), true,
                              "duplicates reached the relay and were filtered");
        if (!std::getenv("CBERGR_KEEP"))
        {
            std::filesystem::remove_all(dir); // set CBERGR_KEEP=1 to inspect the CSVs
        }
    }
};

// Spec test 13 as changed by kickoff §2.3: when the ACK is lost, the timeout gives label 0 at once; the
// retry (new hopSeq) is deduplicated at the relay, which ACKs it again.
class LostAckGivesTimeoutLabelAndDeduplicatedRetry : public TestCase
{
  public:
    LostAckGivesTimeoutLabelAndDeduplicatedRetry()
        : TestCase("D4 test13: lost ACK -> timeout label 0, retry deduplicated at the relay")
    {
    }

  private:
    void DoRun() override
    {
        std::string dir = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-lostack";
        RngSeedManager::SetSeed(6);
        LineNetwork net(dir, "WithFeedback=true;AckRequest=true", 0.0);
        int acksDropped = 0;
        net.propagation->SetFrameFilter([&acksDropped](uint32_t tx, uint32_t rx, uint8_t type) {
            bool firstAck = (type == static_cast<uint8_t>(FrameType::Ack)) && tx == 1 && rx == 2 && acksDropped < 1;
            acksDropped += firstAck ? 1 : 0;
            return firstAck; // lose exactly the first ACK from the relay to the source
        });
        net.Inject(1, 10.0);
        net.RunAndFinish(600.0);

        std::vector<CsvRow> decisions = ReadCsv(dir + "/decisions.csv");
        std::vector<CsvRow> sourceRows;
        for (const CsvRow& r : decisions)
        {
            if (r.at("node") == "2")
            {
                sourceRows.push_back(r);
            }
        }
        NS_TEST_ASSERT_MSG_EQ(sourceRows.size(), 2u, "first attempt and one retry");
        NS_TEST_ASSERT_MSG_EQ(sourceRows[0].at("label"), "0", "lost ACK: label 0");
        NS_TEST_ASSERT_MSG_EQ(sourceRows[0].at("label_source"), "ACK_TIMEOUT", "from the ACK timer");
        NS_TEST_ASSERT_MSG_EQ(sourceRows[0].at("truth_received"), "1", "the relay had received it (ground truth)");
        NS_TEST_ASSERT_MSG_EQ(sourceRows[1].at("label"), "1", "retry acknowledged");
        NS_TEST_ASSERT_MSG_EQ(sourceRows[1].at("label_source"), "ACK", "by an ACK");
        NS_TEST_ASSERT_MSG_EQ((sourceRows[0].at("hop_seq") != sourceRows[1].at("hop_seq")), true, "fresh hopSeq");
        NS_TEST_ASSERT_MSG_EQ(sourceRows[1].at("ar"), "1", "retries always ask for an ACK");
        NS_TEST_ASSERT_MSG_EQ(net.metrics->TotalFalseNegatives(), 1u, "one false negative");

        std::vector<CsvRow> nodes = ReadCsv(dir + "/nodes.csv");
        CsvRow relayRow = NodeRow(nodes, 1);
        NS_TEST_ASSERT_MSG_EQ(relayRow.at("data_duplicates"), "1", "the retry was a duplicate at the relay");
        NS_TEST_ASSERT_MSG_EQ(relayRow.at("data_accepted"), "1", "forwarded once");
        NS_TEST_ASSERT_MSG_EQ(relayRow.at("acks_sent"), "2", "the duplicate is acknowledged too");
        NS_TEST_ASSERT_MSG_EQ(net.metrics->PacketsDelivered(), 1u, "delivered exactly once");
        if (!std::getenv("CBERGR_KEEP"))
        {
            std::filesystem::remove_all(dir);
        }
    }
};


// ============================================================ D5 ===========

namespace
{
// A CB selector with a default link model and a fixed airtime, without a simulator.
Ptr<SelectorCbV5>
MakeCbSelector(const std::string& attributes, uint16_t nodeId = 1, uint16_t cell = 5)
{
    ObjectFactory factory;
    factory.SetTypeId("ns3::cbergr::SelectorCbV5");
    std::stringstream ss(attributes);
    for (std::string item; std::getline(ss, item, ';');)
    {
        size_t eq = item.find('=');
        factory.Set(item.substr(0, eq), StringValue(item.substr(eq + 1)));
    }
    Ptr<SelectorCbV5> sel = factory.Create<SelectorCbV5>();
    SelectorServices services;
    services.nodeId = nodeId;
    services.beaconPeriodS = 50.0;
    services.initialEnergyJ = 300.0;
    services.link = std::make_shared<AcousticLinkModel>();
    services.residualEnergyJ = []() { return 300.0; };
    services.airtimeS = [](uint32_t bytes) { return 0.1 + bytes * 8.0 / 5000.0; };
    services.cellId = [cell]() { return cell; };
    services.isHead = []() { return true; };
    services.isNeighbourCell = [cell](uint16_t other) { return std::abs(int(other) - int(cell)) == 1; }; // neighbours: adjacent ids
    sel->SetServices(services);
    return sel;
}

PacketCtx
MakeCtx()
{
    PacketCtx ctx;
    ctx.key = E2EKey{7, 1};
    ctx.hpkt = 3;
    ctx.myHmin = 3;
    ctx.myPos = Vector(1000, 1000, 2000);
    ctx.myVel = Vector(0, 0, 0);
    ctx.myBattery = 0.9;
    ctx.dataBytes = 167;
    return ctx;
}

NeighbourEntry
MakeNeighbour(uint16_t id, double snrDb)
{
    NeighbourEntry nb;
    nb.id = id;
    nb.pos = Vector(1300, 1000, 1500);   // 300 m east, 500 m shallower than the sender
    nb.vel = Vector(-1.0, 0, 0);         // moving toward the sender
    nb.hmin = 2;
    nb.battery = 0.8;
    nb.queueLen = 4;
    nb.beaconTime = Seconds(1000);
    nb.firstHeard = Seconds(700);
    nb.lastSnrDb = snrDb;
    nb.hasSnr = true;
    nb.isHead = true;
    return nb;
}
} // namespace

// Spec test 6 (§8.1): features of a fixed neighbour row, computed by hand.
class FeaturesMatchHandComputedValues : public TestCase
{
  public:
    FeaturesMatchHandComputedValues()
        : TestCase("D5 test6: BuildFeatures on a fixed row (hand-computed x1..x9)")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<SelectorCbV5> sel = MakeCbSelector("MarginFromRssi=true");
        AcousticLinkModel link;
        PacketCtx ctx = MakeCtx();
        double snrReq = link.SnrReqDb(ctx.dataBytes);
        NeighbourEntry nb = MakeNeighbour(9, snrReq + 5.0); // beacon SNR 5 dB above what data needs
        Time now = Seconds(1000);                           // beacon just received: no extrapolation

        auto x = sel->BuildFeatures(ctx, nb, 2, now);
        const double distance = std::sqrt(300.0 * 300.0 + 500.0 * 500.0);              // 583.095 m
        const double closing = 300.0 / distance;                                         // -d|r|/dt, v_rel = (-1,0,0)
        NS_TEST_ASSERT_MSG_EQ_TOL(x[0], distance / 1500.0, 1e-9, "x1 distance");
        NS_TEST_ASSERT_MSG_EQ_TOL(x[1], 1.0, 1e-12, "x2 progress: hmin 2 < hpkt 3");
        NS_TEST_ASSERT_MSG_EQ_TOL(x[2], ((2000.0 - 1500.0) / 1500.0 + 1.0) / 2.0, 1e-9, "x3 depth advantage");
        NS_TEST_ASSERT_MSG_EQ_TOL(x[3], 0.8, 1e-12, "x4 battery");
        NS_TEST_ASSERT_MSG_EQ_TOL(x[4], 0.3, 1e-12, "x5 age: 300 s / 1000 s");
        NS_TEST_ASSERT_MSG_EQ_TOL(x[5], 0.4, 1e-12, "x6 queue: 4 / 10");
        NS_TEST_ASSERT_MSG_EQ_TOL(x[6], (closing / 2.0 + 1.0) / 2.0, 1e-9, "x7 closing speed");
        NS_TEST_ASSERT_MSG_EQ_TOL(x[7], (5.0 + 10.0) / 30.0, 1e-9, "x8 margin 5 dB at full power");
        NS_TEST_ASSERT_MSG_EQ_TOL(x[8], 1.0, 1e-12, "x9 intercept");

        // Only x8 depends on the power level: the lowest level is 6 dB weaker.
        auto xLow = sel->BuildFeatures(ctx, nb, 0, now);
        NS_TEST_ASSERT_MSG_EQ_TOL(xLow[7], (5.0 - 6.0 + 10.0) / 30.0, 1e-9, "x8 at level 0");
        for (int i = 0; i < 9; ++i)
        {
            if (i != 7)
            {
                NS_TEST_ASSERT_MSG_EQ_TOL(xLow[i], x[i], 1e-12, "other features unchanged by power");
            }
        }
        // Everything stays in [0, 1], also for an extreme neighbour.
        NeighbourEntry far = MakeNeighbour(10, -100.0);
        far.pos = Vector(9000, 9000, 9000);
        far.battery = 3.0;
        far.queueLen = 255;
        auto xf = sel->BuildFeatures(ctx, far, 2, now);
        for (int i = 0; i < 9; ++i)
        {
            NS_TEST_ASSERT_MSG_EQ((xf[i] >= 0.0 && xf[i] <= 1.0), true, "feature " << i << " clipped to [0,1]");
        }

        // Without RSSI the margin comes from the model at the predicted distance (spec §8.2, no shadowing).
        Ptr<SelectorCbV5> modelOnly = MakeCbSelector("MarginFromRssi=false");
        double expectedMargin = link.MarginDb(link.FullSourceLevelDb(), distance, ctx.dataBytes);
        NS_TEST_ASSERT_MSG_EQ_TOL(modelOnly->PredictedMarginDb(ctx, nb, 2, now), expectedMargin, 1e-9, "model margin");
        // The beacon position drifts with its velocity: after 100 s the neighbour has moved 100 m west.
        auto later = sel->BuildFeatures(ctx, nb, 2, Seconds(1100));
        NS_TEST_ASSERT_MSG_EQ((later[0] < x[0]), true, "predicted distance shrinks as it approaches");
    }
};

// Spec §8.5: the score, with and without the lifetime terms.
class ScoreCombinesSuccessProgressAndEnergy : public TestCase
{
  public:
    ScoreCombinesSuccessProgressAndEnergy()
        : TestCase("D5: score = p * progress - lambda * Etx - mu * phi(battery)")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<SelectorCbV5> sel = MakeCbSelector("EnableLifetime=true;Mu=0.1;KappaP=0.5");
        SelectorCbV5::Features x;
        x[1] = 1.0;                                         // makes progress
        NS_TEST_ASSERT_MSG_EQ_TOL(sel->Score(0.8, x, 2.0, 0.8), 0.8 * 1.0 - 0.1 * 0.25, 1e-12,
                                  "no price yet (lambda 0); phi(0.8) = 0.25");
        x[1] = 0.0;                                         // no progress: weight 1 - 0.5
        NS_TEST_ASSERT_MSG_EQ_TOL(sel->Score(0.8, x, 2.0, 0.8), 0.8 * 0.5 - 0.1 * 0.25, 1e-12, "no progress");
        NS_TEST_ASSERT_MSG_EQ_TOL(sel->Score(0.8, x, 2.0, 1.0), 0.4, 1e-12, "full battery has no penalty");
        NS_TEST_ASSERT_MSG_EQ_TOL(sel->Score(0.8, x, 2.0, 0.0), 0.4 - 0.1 * 99.0, 1e-9, "empty battery clamps at 0.01");

        Ptr<SelectorCbV5> off = MakeCbSelector("EnableLifetime=false");
        NS_TEST_ASSERT_MSG_EQ_TOL(off->Score(0.8, x, 2.0, 0.3), 0.4, 1e-12, "lifetime off: no energy terms");
    }
};

// Spec §8.6: when an ACK is requested, in each mode.
class AckGateFollowsTheModeAndUncertainty : public TestCase
{
  public:
    AckGateFollowsTheModeAndUncertainty()
        : TestCase("D5: NeedsAck in Every, BitmapOnly and Gated modes")
    {
    }

  private:
    void DoRun() override
    {
        PacketCtx first = MakeCtx();            // attempt 1, hpkt == myHmin: not a priority packet
        PacketCtx retry = MakeCtx();
        retry.attempt = 2;
        PacketCtx priority = MakeCtx();
        priority.hpkt = 4;                      // hpkt > myHmin

        Ptr<SelectorCbV5> every = MakeCbSelector("AckMode=Every");
        NS_TEST_ASSERT_MSG_EQ(every->NeedsAck(first, 0.0), true, "Every: always");

        Ptr<SelectorCbV5> bitmap = MakeCbSelector("AckMode=BitmapOnly");
        NS_TEST_ASSERT_MSG_EQ(bitmap->NeedsAck(first, 9.9), false, "BitmapOnly: not on first attempts");
        NS_TEST_ASSERT_MSG_EQ(bitmap->NeedsAck(retry, 0.0), true, "BitmapOnly: retries ask");

        Ptr<SelectorCbV5> gated = MakeCbSelector("AckMode=Gated;QAudit=0;TauW=0.3");
        NS_TEST_ASSERT_MSG_EQ(gated->NeedsAck(first, 0.1), false, "confident, no audit: no ACK");
        NS_TEST_ASSERT_MSG_EQ(gated->NeedsAck(first, 0.31), true, "unsure (width > TauW)");
        NS_TEST_ASSERT_MSG_EQ(gated->NeedsAck(retry, 0.0), true, "retry");
        NS_TEST_ASSERT_MSG_EQ(gated->NeedsAck(priority, 0.0), true, "priority packet");

        Ptr<SelectorCbV5> audit = MakeCbSelector("AckMode=Gated;QAudit=1");
        NS_TEST_ASSERT_MSG_EQ(audit->NeedsAck(first, 0.0), true, "QAudit = 1 audits every attempt");

        // AckGate=ProbStd: std of p ~ p (1 - p) width against ProbStdThreshold; the width threshold TauW is then ignored.
        Ptr<SelectorCbV5> probStd = MakeCbSelector("AckMode=Gated;QAudit=0;AckGate=ProbStd;ProbStdThreshold=0.05");
        NS_TEST_ASSERT_MSG_EQ(probStd->NeedsAck(first, 1.0, 0.99), false, "p near 1: tiny std, no ACK even with a wide model");
        NS_TEST_ASSERT_MSG_EQ(probStd->NeedsAck(first, 1.0, 0.5), true, "p = 0.5, width 1: std 0.25 > 0.05");
        NS_TEST_ASSERT_MSG_EQ(probStd->NeedsAck(first, 0.1, 0.5), false, "p = 0.5, width 0.1: std 0.025 <= 0.05");
        NS_TEST_ASSERT_MSG_EQ(probStd->NeedsAck(retry, 0.0, 0.99), true, "retries always ask");
        NS_TEST_ASSERT_MSG_EQ(gated->NeedsAck(first, 1.0, 0.99), true, "the spec (Logit) gate ignores p");
    }
};

// The learner end to end (without the simulator): it discovers which candidate's link works.
class CbLearnsToPreferTheReliableLink : public TestCase
{
  public:
    CbLearnsToPreferTheReliableLink()
        : TestCase("D5: Select + OnSent + OnLabel learn to prefer the reliable link")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<SelectorCbV5> sel = MakeCbSelector("EnableLifetime=false;AckMode=Every");
        sel->AssignStreams(500);
        AcousticLinkModel link;
        PacketCtx ctx = MakeCtx();
        double snrReq = link.SnrReqDb(ctx.dataBytes);
        NeighbourEntry good = MakeNeighbour(11, snrReq + 15.0);   // strong link
        NeighbourEntry bad = MakeNeighbour(12, snrReq - 12.0);    // weak link, everything else equal
        std::vector<Candidate> eligible = {{11, &good}, {12, &bad}};

        std::mt19937 gen(99);
        std::uniform_real_distribution<double> coin(0.0, 1.0);
        int goodInLast100 = 0;
        for (int n = 0; n < 400; ++n)
        {
            Time now = Seconds(1000 + 5.0 * n);
            Decision d = sel->Select(ctx, eligible, now);
            NS_TEST_ASSERT_MSG_EQ(d.valid, true, "a decision is made");
            sel->OnSent(d, now);
            double pTrue = (d.fwdId == 11) ? 0.95 : 0.15;
            sel->OnLabel(d, coin(gen) < pTrue ? 1 : 0, LabelSource::Ack, now + Seconds(2));
            goodInLast100 += (n >= 300 && d.fwdId == 11);
        }
        NS_TEST_ASSERT_MSG_EQ((goodInLast100 > 85), true, "chooses the reliable link > 85% of the time (" << goodInLast100 << ")");
        NS_TEST_ASSERT_MSG_EQ((sel->Model().TraceOwn() > 9.0 + 5.0), true, "evidence accumulated");
    }
};

// Sharing (spec §8.10, K§2.3): a head's model reaches its co-head on a beacon and counts once.
class SharedModelRaisesTheCoHeadsConfidence : public TestCase
{
  public:
    SharedModelRaisesTheCoHeadsConfidence()
        : TestCase("D5: model blob on a beacon is received by the co-head and not double counted")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<SelectorCbV5> a = MakeCbSelector("EnableLifetime=false", 1, 5);
        Ptr<SelectorCbV5> b = MakeCbSelector("EnableLifetime=false", 2, 5);
        AcousticLinkModel link;
        PacketCtx ctx = MakeCtx();
        NeighbourEntry nb = MakeNeighbour(11, link.SnrReqDb(ctx.dataBytes) + 10.0);
        std::vector<Candidate> eligible = {{11, &nb}};
        for (int n = 0; n < 40; ++n)
        {
            Time now = Seconds(1000 + n);
            Decision d = a->Select(ctx, eligible, now);
            a->OnSent(d, now);
            a->OnLabel(d, 1, LabelSource::Ack, now);
        }
        ErgrBeaconHeader beacon;
        beacon.nodeId = 1;
        beacon.cellId = 5;
        a->OnBeaconTx(beacon, Seconds(1040));
        NS_TEST_ASSERT_MSG_EQ(beacon.hasBlob, true, "enough new evidence: a blob is attached");
        ErgrBeaconHeader again;
        again.nodeId = 1;
        again.cellId = 5;
        a->OnBeaconTx(again, Seconds(1041));
        NS_TEST_ASSERT_MSG_EQ(again.hasBlob, false, "no new evidence since the last blob: none attached");

        b->OnBeaconRx(beacon, Seconds(1040));
        b->Select(ctx, eligible, Seconds(1040));
        double effectiveOnce = b->Model().EffectivePrecision().Trace();
        NS_TEST_ASSERT_MSG_EQ((effectiveOnce > b->Model().TraceOwn() + 1.0), true, "the co-head's evidence counts");
        b->OnBeaconRx(beacon, Seconds(1040));       // the same message again
        double effectiveTwice = b->Model().EffectivePrecision().Trace();
        NS_TEST_ASSERT_MSG_EQ_TOL(effectiveTwice, effectiveOnce, 1e-9, "a repeated blob is not counted twice");

        // A blob from another cell is ignored.
        Ptr<SelectorCbV5> c = MakeCbSelector("EnableLifetime=false", 3, 6);
        c->OnBeaconRx(beacon, Seconds(1040));
        NS_TEST_ASSERT_MSG_EQ_TOL(c->Model().EffectivePrecision().Trace(), c->Model().TraceOwn(), 1e-12,
                                  "other cell: ignored");
        // Quantisation: the received theta is within one step (1/1024) of the sender's.
        NS_TEST_ASSERT_MSG_EQ_TOL(b->Model().CellPrior()[7], a->Model().Theta()[7], 1.0 / 1024.0, "theta within quantisation");
    }
};



// R13: a head that starts without a handover and without a model from its own cell starts from the neighbouring cell's model.
class NewHeadStartsFromTheNeighbourCellsModel : public TestCase
{
  public:
    NewHeadStartsFromTheNeighbourCellsModel()
        : TestCase("R13: NeighbourPrior starts a new head from a neighbouring cell's model")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<SelectorCbV5> teacher = MakeCbSelector("EnableLifetime=false", 1, 6);
        AcousticLinkModel link;
        PacketCtx ctx = MakeCtx();
        NeighbourEntry nb = MakeNeighbour(11, link.SnrReqDb(ctx.dataBytes) + 10.0);
        std::vector<Candidate> eligible = {{11, &nb}};
        for (int n = 0; n < 40; ++n)
        {
            Time now = Seconds(1000 + n);
            Decision d = teacher->Select(ctx, eligible, now);
            teacher->OnSent(d, now);
            teacher->OnLabel(d, 1, LabelSource::Ack, now);
        }
        ErgrBeaconHeader beacon;
        beacon.nodeId = 1;
        beacon.cellId = 6;
        teacher->OnBeaconTx(beacon, Seconds(1040));
        NS_TEST_ASSERT_MSG_EQ(beacon.hasBlob, true, "the teacher shares a model");

        Ptr<SelectorCbV5> warm = MakeCbSelector("EnableLifetime=false;NeighbourPrior=true", 2, 5);
        Ptr<SelectorCbV5> cold = MakeCbSelector("EnableLifetime=false", 3, 5);
        for (Ptr<SelectorCbV5> s : {warm, cold})
        {
            s->OnBeaconRx(beacon, Seconds(1041));
            NS_TEST_ASSERT_MSG_EQ_TOL(s->Model().Theta().Norm(), 0.0, 1e-12, "nothing is adopted before the node is head");
            s->OnRoleChange(true, Seconds(1042));
        }
        double distance = 0.0;
        for (int i = 0; i < 9; ++i)
        {
            distance += std::fabs(warm->Model().Theta()[i] - teacher->Model().Theta()[i]);
        }
        NS_TEST_ASSERT_MSG_EQ((distance < 9.0 / 1024.0 * 2.0), true, "warm head starts at the teacher's theta (quantisation only)");
        NS_TEST_ASSERT_MSG_EQ_TOL(cold->Model().Theta().Norm(), 0.0, 1e-12, "without NeighbourPrior the head starts cold");
        // The blob of a cell that is not a neighbour is ignored.
        Ptr<SelectorCbV5> far = MakeCbSelector("EnableLifetime=false;NeighbourPrior=true", 4, 9);
        far->OnBeaconRx(beacon, Seconds(1041));
        far->OnRoleChange(true, Seconds(1042));
        NS_TEST_ASSERT_MSG_EQ_TOL(far->Model().Theta().Norm(), 0.0, 1e-12, "a distant cell's model is ignored");
    }
};

// ============================================================ D6 ===========

namespace
{
template <typename T>
Ptr<T>
MakeBaseline(const std::string& typeName, const std::string& attributes)
{
    ObjectFactory factory;
    factory.SetTypeId(typeName);
    std::stringstream ss(attributes);
    for (std::string item; std::getline(ss, item, ';');)
    {
        size_t eq = item.find('=');
        factory.Set(item.substr(0, eq), StringValue(item.substr(eq + 1)));
    }
    Ptr<T> sel = factory.Create<T>();
    SelectorServices services;
    services.nodeId = 1;
    services.beaconPeriodS = 50.0;
    services.link = std::make_shared<AcousticLinkModel>();
    services.airtimeS = [](uint32_t bytes) { return 0.1 + bytes * 8.0 / 5000.0; };
    sel->SetServices(services);
    return sel;
}
} // namespace

// FW (K§2.4): fixed weights prefer the link with the larger SNR margin, never learn, and ask for ACKs.
class FixedWeightPrefersTheBetterLinkAndNeverLearns : public TestCase
{
  public:
    FixedWeightPrefersTheBetterLinkAndNeverLearns()
        : TestCase("D6: FW picks the stronger link and does not change with labels")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<SelectorFixedWeight> fw = MakeBaseline<SelectorFixedWeight>("ns3::cbergr::SelectorFixedWeight", "");
        AcousticLinkModel link;
        PacketCtx ctx = MakeCtx();
        double snrReq = link.SnrReqDb(ctx.dataBytes);
        NeighbourEntry good = MakeNeighbour(11, snrReq + 12.0);
        NeighbourEntry bad = MakeNeighbour(12, snrReq - 8.0);
        std::vector<Candidate> eligible = {{12, &bad}, {11, &good}};
        Decision d = fw->Select(ctx, eligible, Seconds(1000));
        NS_TEST_ASSERT_MSG_EQ(int(d.fwdId), 11, "stronger link wins");
        NS_TEST_ASSERT_MSG_EQ(d.ackRequest, true, "FW asks for ACKs like CB with AckMode=Every");
        NS_TEST_ASSERT_MSG_EQ((d.pMean > 0.0 && d.pMean < 1.0), true, "a probability");
        for (int n = 0; n < 50; ++n)
        {
            fw->OnLabel(d, 0, LabelSource::Ack, Seconds(1001 + n)); // bad news changes nothing
        }
        NS_TEST_ASSERT_MSG_EQ(int(fw->Select(ctx, eligible, Seconds(2000)).fwdId), 11, "same choice after labels");

        // The Weights attribute replaces the hand-set physics weights (tuned FW); without it the physics weights apply.
        double physicsLogit = 0.0;
        for (int i = 0; i < 9; ++i)
        {
            physicsLogit += params::selector::kFwWeights[i] * d.x[i];
        }
        NS_TEST_ASSERT_MSG_EQ_TOL(d.pMean, Sigmoid(physicsLogit), 1e-12, "default = physics weights");
        Ptr<SelectorFixedWeight> tuned =
            MakeBaseline<SelectorFixedWeight>("ns3::cbergr::SelectorFixedWeight", "Weights=0,0,0,0,0,0,0,10,-3");
        Decision dt = tuned->Select(ctx, eligible, Seconds(1000));
        NS_TEST_ASSERT_MSG_EQ_TOL(dt.pMean, Sigmoid(10.0 * dt.x[7] - 3.0), 1e-12, "explicit weights are used");
    }
};

// CARP (§11 B3): EWMA update rule, choice by EWMA times progress, no ACK on first attempts.
class CarpEwmaFollowsItsUpdateRule : public TestCase
{
  public:
    CarpEwmaFollowsItsUpdateRule()
        : TestCase("D6: CARP EWMA update and choice")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<SelectorCarpEwma> carp = MakeBaseline<SelectorCarpEwma>("ns3::cbergr::SelectorCarpEwma", "Beta=0.2;Initial=0.5");
        PacketCtx ctx = MakeCtx();
        NeighbourEntry a = MakeNeighbour(21, 0.0);
        NeighbourEntry b = MakeNeighbour(22, 0.0);
        std::vector<Candidate> eligible = {{21, &a}, {22, &b}};
        Decision d = carp->Select(ctx, eligible, Seconds(1000));
        NS_TEST_ASSERT_MSG_EQ(d.ackRequest, false, "CARP style: no ACK on first attempts");
        NS_TEST_ASSERT_MSG_EQ(int(d.fwdId), 21, "tie: lower id");
        Decision toA;
        toA.fwdId = 21;
        carp->OnLabel(toA, 0, LabelSource::Bitmap, Seconds(1001));
        NS_TEST_ASSERT_MSG_EQ_TOL(carp->Ewma(21), 0.8 * 0.5 + 0.2 * 0.0, 1e-12, "EWMA after a 0");
        NS_TEST_ASSERT_MSG_EQ_TOL(carp->Ewma(22), 0.5, 1e-12, "untouched");
        NS_TEST_ASSERT_MSG_EQ(int(carp->Select(ctx, eligible, Seconds(1002)).fwdId), 22, "moves to the better one");
        carp->OnLabel(toA, 1, LabelSource::Bitmap, Seconds(1003));
        NS_TEST_ASSERT_MSG_EQ_TOL(carp->Ewma(21), 0.8 * 0.4 + 0.2 * 1.0, 1e-12, "EWMA after a 1");
    }
};

// QL (§11 B2): Q update with the advertised next-state value, terminal sinks, value advertised on the beacon.
class QLearningUpdatesAndAdvertisesItsValue : public TestCase
{
  public:
    QLearningUpdatesAndAdvertisesItsValue()
        : TestCase("D6: Q-learning update rule, terminal sink and beacon value (+2 B)")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<SelectorQLearning> ql =
            MakeBaseline<SelectorQLearning>("ns3::cbergr::SelectorQLearning", "Epsilon=0;Alpha=0.3;Gamma=0.8;StepCost=0.1");
        ql->AssignStreams(900);
        PacketCtx ctx = MakeCtx();
        NeighbourEntry relay = MakeNeighbour(31, 0.0);
        relay.hasQ = true;
        relay.advertisedQ = 0.9;
        NeighbourEntry sink = MakeNeighbour(32, 0.0);
        sink.isSink = true;
        sink.hmin = 0;
        std::vector<Candidate> eligible = {{31, &relay}, {32, &sink}};

        Decision d = ql->Select(ctx, eligible, Seconds(1000));
        // Initial Q = 1 + 0.1 * (myHmin 3 - hmin_j): relay (hmin 2) = 1.1, sink (hmin 0) = 1.3.
        NS_TEST_ASSERT_MSG_EQ(int(d.fwdId), 32, "initial guess prefers the sink (lower Hmin)");
        NS_TEST_ASSERT_MSG_EQ_TOL(d.score, 1.3, 1e-12, "value used");
        NS_TEST_ASSERT_MSG_EQ_TOL(d.aux, 0.0, 1e-12, "a sink is terminal: next value 0");
        ql->OnLabel(d, 0, LabelSource::AckTimeout, Seconds(1002));
        // Q <- 1.3 + 0.3 * ((0 - 0.1) + 0.8 * 0 - 1.3) = 0.88
        NS_TEST_ASSERT_MSG_EQ_TOL(ql->Q(32), 1.3 + 0.3 * (-0.1 - 1.3), 1e-12, "update after a failure");

        Decision toRelay;
        toRelay.fwdId = 31;
        toRelay.score = 1.1;
        toRelay.aux = 0.9;
        ql->OnLabel(toRelay, 1, LabelSource::Ack, Seconds(1003));
        // target = (1 - 0.1) + 0.8 * 0.9 = 1.62; Q <- 1.1 + 0.3 * (1.62 - 1.1) = 1.256
        NS_TEST_ASSERT_MSG_EQ_TOL(ql->Q(31), 1.1 + 0.3 * (1.62 - 1.1), 1e-12, "update with next-state value");

        ErgrBeaconHeader beacon;
        ql->Select(ctx, eligible, Seconds(1004));
        ql->OnBeaconTx(beacon, Seconds(1005));
        NS_TEST_ASSERT_MSG_EQ(beacon.hasQ, true, "the value rides on the beacon");
        NS_TEST_ASSERT_MSG_EQ(beacon.QBytes(), 2u, "two bytes of overhead");
        ErgrBeaconHeader plain;
        NS_TEST_ASSERT_MSG_EQ(beacon.GetSerializedSize(), plain.GetSerializedSize() + 2, "counted in the size");
        Ptr<Packet> p = Create<Packet>(0);
        p->AddHeader(beacon);
        ErgrBeaconHeader back;
        p->RemoveHeader(back);
        NS_TEST_ASSERT_MSG_EQ(back.hasQ, true, "survives a round trip");
        NS_TEST_ASSERT_MSG_EQ(int(back.qValue), int(beacon.qValue), "value");
    }
};

// LinQ (§11 B4): semi-gradient TD(0) on the CB features.
class LinearQMovesItsWeightsTowardTheTarget : public TestCase
{
  public:
    LinearQMovesItsWeightsTowardTheTarget()
        : TestCase("D6: LinQ TD(0) weight update")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<SelectorLfaQ> lfa =
            MakeBaseline<SelectorLfaQ>("ns3::cbergr::SelectorLfaQ", "Epsilon=0;Step=0.05;Gamma=0.8;StepCost=0.1");
        lfa->AssignStreams(901);
        PacketCtx ctx = MakeCtx();
        NeighbourEntry nb = MakeNeighbour(41, 0.0);
        nb.hasQ = true;
        nb.advertisedQ = 0.5;
        std::vector<Candidate> eligible = {{41, &nb}};
        Decision d = lfa->Select(ctx, eligible, Seconds(1000));
        NS_TEST_ASSERT_MSG_EQ_TOL(d.score, 0.0, 1e-12, "weights start at zero");
        NS_TEST_ASSERT_MSG_EQ_TOL(d.aux, 0.5, 1e-12, "advertised next-state value");
        lfa->OnLabel(d, 1, LabelSource::Ack, Seconds(1002));
        // target = (1 - 0.1) + 0.8 * 0.5 = 1.3; error = 1.3 - 0 = 1.3; w = 0.05 * 1.3 * x
        for (int i = 0; i < 9; ++i)
        {
            NS_TEST_ASSERT_MSG_EQ_TOL(lfa->Weights()[i], 0.05 * 1.3 * d.x[i], 1e-12, "w_" << i);
        }
        Decision again = lfa->Select(ctx, eligible, Seconds(1003));
        NS_TEST_ASSERT_MSG_EQ((again.score > d.score), true, "the value of the same option rose");
    }
};


// Oracle evaluator (§11 O/GO): true-world hop success is the link model at the true distance and shadowing.
class OracleSuccessMatchesTheLinkModel : public TestCase
{
  public:
    OracleSuccessMatchesTheLinkModel()
        : TestCase("D6: OracleEvaluator hop success = 1 - PER at the true distance")
    {
    }

  private:
    void DoRun() override
    {
        auto link = std::make_shared<AcousticLinkModel>();
        Ptr<GmShadowing> shadowing = CreateObject<GmShadowing>();
        shadowing->SetBaseSeed(11);
        Ptr<OracleEvaluator> oracle = CreateObject<OracleEvaluator>();
        oracle->Configure([](uint32_t id) { return Vector(id == 0 ? 0.0 : (id == 1 ? 750.0 : 1600.0), 0, 0); },
                          shadowing, link, 1500.0);
        const Time t = Seconds(100);
        double geo = oracle->HopSuccess(0, 1, 2, 167, t, false);
        double expected = 1.0 - link->PerFromSnr(link->SnrDb(link->FullSourceLevelDb(), 750.0, 0.0), 167);
        NS_TEST_ASSERT_MSG_EQ_TOL(geo, expected, 1e-12, "geometry-only value");
        double withShadow = oracle->HopSuccess(0, 1, 2, 167, t, true);
        double shadowDb = shadowing->GetShadowDb(0, 1, t);
        double expectedShadow =
            1.0 - link->PerFromSnr(link->SnrDb(link->FullSourceLevelDb(), 750.0, shadowDb), 167);
        NS_TEST_ASSERT_MSG_EQ_TOL(withShadow, expectedShadow, 1e-12, "value with the true shadowing");
        NS_TEST_ASSERT_MSG_EQ_TOL(oracle->HopSuccess(0, 2, 2, 167, t, true), 0.0, 0.0, "beyond the range: zero");
        NS_TEST_ASSERT_MSG_EQ((oracle->HopSuccess(0, 1, 0, 167, t, false) < geo), true, "lower power, lower success");
        NS_TEST_ASSERT_MSG_EQ_TOL(oracle->Distance(0, 1), 750.0, 1e-12, "distance");

        // Noise band (heterogeneous scenario): +10 dB noise at receivers with x in [500, 1000) lowers the SNR by 10 dB.
        NoiseBand band;
        band.xMinM = 500.0;
        band.xMaxM = 1000.0;
        band.offsetDb = 10.0;
        oracle->SetNoiseBand(band);
        double inBand = oracle->HopSuccess(0, 1, 2, 167, t, true);
        double expectedBand =
            1.0 - link->PerFromSnr(link->SnrDb(link->FullSourceLevelDb(), 750.0, shadowDb - 10.0), 167);
        NS_TEST_ASSERT_MSG_EQ_TOL(inBand, expectedBand, 1e-12, "receiver inside the band: 10 dB less SNR");
        NS_TEST_ASSERT_MSG_EQ_TOL(oracle->HopSuccess(1, 0, 2, 167, t, true),
                                  1.0 - link->PerFromSnr(link->SnrDb(link->FullSourceLevelDb(), 750.0, shadowing->GetShadowDb(1, 0, t)), 167),
                                  1e-12, "transmitter inside the band (receiver outside): unchanged");
    }
};

class OracleSelectorPicksTheTrulyBestHop : public TestCase
{
  public:
    OracleSelectorPicksTheTrulyBestHop()
        : TestCase("D6: O and GO pick the best true hop")
    {
    }

  private:
    void DoRun() override
    {
        auto link = std::make_shared<AcousticLinkModel>();
        Ptr<GmShadowing> shadowing = CreateObject<GmShadowing>();
        shadowing->SetAttribute("ShadowSigmaDb", DoubleValue(0.0));
        Ptr<OracleEvaluator> oracle = CreateObject<OracleEvaluator>();
        // node 1 (me) at the origin, node 2 at 600 m, node 3 at 1400 m, both equally "progressing"
        oracle->Configure([](uint32_t id) { return Vector(id == 1 ? 0.0 : (id == 2 ? 600.0 : 1400.0), 0, 0); },
                          shadowing, link, 1500.0);
        Ptr<SelectorOracle> o = MakeBaseline<SelectorOracle>("ns3::cbergr::SelectorOracle", "");
        o->SetOracle(oracle);
        SelectorServices services;
        services.nodeId = 1;
        o->SetServices(services);
        PacketCtx ctx = MakeCtx();
        NeighbourEntry near = MakeNeighbour(2, 0.0);
        NeighbourEntry far = MakeNeighbour(3, 0.0);
        std::vector<Candidate> eligible = {{2, &near}, {3, &far}};
        Decision d = o->Select(ctx, eligible, Seconds(100));
        NS_TEST_ASSERT_MSG_EQ(int(d.fwdId), 2, "the near node has the better link");
        NS_TEST_ASSERT_MSG_EQ((d.pMean > 0.9), true, "reported success is the true probability");
        NS_TEST_ASSERT_MSG_EQ(d.ackRequest, true, "same feedback as the other methods");

        // Without an oracle there is no decision (a learner can never get one).
        Ptr<SelectorOracle> blind = MakeBaseline<SelectorOracle>("ns3::cbergr::SelectorOracle", "");
        blind->SetServices(services);
        NS_TEST_ASSERT_MSG_EQ(blind->Select(ctx, eligible, Seconds(100)).valid, false, "no oracle, no decision");
    }
};

class FittedLogisticUsesItsOfflineWeights : public TestCase
{
  public:
    FittedLogisticUsesItsOfflineWeights()
        : TestCase("D6: A1 parses its weights and applies the fixed logistic rule")
    {
    }

  private:
    void DoRun() override
    {
        // Only the margin matters (weight 10 on x8), everything else zero.
        Ptr<SelectorFittedLogistic> a1 = MakeBaseline<SelectorFittedLogistic>(
            "ns3::cbergr::SelectorFittedLogistic", "Weights=0,0,0,0,0,0,0,10,-3");
        AcousticLinkModel link;
        PacketCtx ctx = MakeCtx();
        double snrReq = link.SnrReqDb(ctx.dataBytes);
        NeighbourEntry strong = MakeNeighbour(51, snrReq + 15.0);
        NeighbourEntry weak = MakeNeighbour(52, snrReq - 5.0);
        std::vector<Candidate> eligible = {{52, &weak}, {51, &strong}};
        Decision d = a1->Select(ctx, eligible, Seconds(1000));
        NS_TEST_ASSERT_MSG_EQ(int(d.fwdId), 51, "stronger margin wins");
        NS_TEST_ASSERT_MSG_EQ_TOL(a1->Weights()[7], 10.0, 1e-12, "weight parsed");
        double expectedLogit = 10.0 * d.x[7] - 3.0;
        NS_TEST_ASSERT_MSG_EQ_TOL(d.pMean, Sigmoid(expectedLogit), 1e-12, "sigmoid(w . x)");
    }
};


// ============================================================ D7 ===========

// Spec test 14 (§14) with the K§2.3 rules: a planned handover passes (theta, A) unscaled; an unplanned takeover
// (the old head died) starts from the last shared blob with A scaled by BetaHandover = 0.7.
class HandoverGivesTheNewHeadTheOldModel : public TestCase
{
  public:
    HandoverGivesTheNewHeadTheOldModel()
        : TestCase("D7 test14: planned handover and unplanned takeover reproduce the old head's model")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<SelectorCbV5> oldHead = MakeCbSelector("EnableLifetime=false", 1, 5);
        AcousticLinkModel link;
        PacketCtx ctx = MakeCtx();
        NeighbourEntry good = MakeNeighbour(11, link.SnrReqDb(ctx.dataBytes) + 12.0);
        NeighbourEntry bad = MakeNeighbour(12, link.SnrReqDb(ctx.dataBytes) - 6.0);
        std::vector<Candidate> eligible = {{11, &good}, {12, &bad}};
        std::mt19937 gen(5);
        std::uniform_real_distribution<double> coin(0.0, 1.0);
        for (int n = 0; n < 120; ++n)
        {
            Time now = Seconds(1000 + 3.0 * n);
            Decision d = oldHead->Select(ctx, eligible, now);
            oldHead->OnSent(d, now);
            oldHead->OnLabel(d, coin(gen) < (d.fwdId == 11 ? 0.9 : 0.2) ? 1 : 0, LabelSource::Ack, now);
        }
        const Time tEnd = Seconds(1000 + 3.0 * 120);

        // ---- planned handover: header round trip, then the successor becomes head.
        ErgrHandoverHeader out;
        out.fromId = 1;
        out.toId = 2;
        out.cellId = 5;
        NS_TEST_ASSERT_MSG_EQ(oldHead->OnHandoverOut(out, tEnd), true, "there is a model to hand over");
        Ptr<Packet> wire = Create<Packet>(0);
        wire->AddHeader(out);
        NS_TEST_ASSERT_MSG_EQ(wire->GetSize(), 115u, "handover frame is 115 B");
        NS_TEST_ASSERT_MSG_EQ(PeekFrameType(wire), static_cast<uint8_t>(FrameType::Handover), "type byte");
        ErgrHandoverHeader in;
        wire->RemoveHeader(in);

        Ptr<SelectorCbV5> successor = MakeCbSelector("EnableLifetime=false", 2, 5);
        successor->OnHandoverIn(in, tEnd);
        successor->OnRoleChange(true, tEnd);
        const auto& oldModel = oldHead->Model();
        const auto& newModel = successor->Model();
        for (int i = 0; i < 9; ++i)
        {
            NS_TEST_ASSERT_MSG_EQ_TOL(newModel.Theta()[i], oldModel.Theta()[i], 1.0 / 1024.0, "theta_" << i << " within quantisation");
            NS_TEST_ASSERT_MSG_EQ_TOL(newModel.OwnPrecision().At(i, i), oldModel.OwnPrecision().At(i, i), 1.0 / 16.0,
                                      "A_" << i << i << " within quantisation");
        }
        NS_TEST_ASSERT_MSG_EQ_TOL(newModel.TraceOwn(), oldModel.TraceOwn(), 9.0 / 16.0, "planned handover is unscaled");

        // ---- unplanned takeover: the old head only ever sent blobs on beacons; it then dies.
        ErgrBeaconHeader blobBeacon;
        blobBeacon.nodeId = 1;
        blobBeacon.cellId = 5;
        oldHead->OnBeaconTx(blobBeacon, tEnd);
        NS_TEST_ASSERT_MSG_EQ(blobBeacon.hasBlob, true, "the old head had shared a blob");
        Ptr<SelectorCbV5> taker = MakeCbSelector("EnableLifetime=false", 3, 5);
        taker->OnBeaconRx(blobBeacon, tEnd);
        taker->OnRoleChange(true, tEnd + Seconds(60));
        double evidence = 0.0;
        for (int i = 0; i < 9; ++i)
        {
            NS_TEST_ASSERT_MSG_EQ_TOL(taker->Model().Theta()[i], oldModel.Theta()[i], 1.0 / 1024.0,
                                      "unplanned: theta_" << i << " within quantisation");
            evidence += oldModel.OwnEvidenceDiag()[i];
        }
        NS_TEST_ASSERT_MSG_EQ_TOL(taker->Model().TraceOwn(), 0.7 * (9.0 + evidence), 9.0 * 0.7 / 16.0 + 0.2,
                                  "unplanned: A scaled by BetaHandover = 0.7");
        NS_TEST_ASSERT_MSG_EQ((taker->Model().TraceOwn() < newModel.TraceOwn()), true, "less confident than after a planned handover");

        // A node with no model to hand over sends nothing.
        Ptr<SelectorCbV5> fresh = MakeCbSelector("EnableLifetime=false", 4, 5);
        ErgrHandoverHeader nothing;
        NS_TEST_ASSERT_MSG_EQ(fresh->OnHandoverOut(nothing, tEnd), false, "nothing learned: no handover");
    }
};

// Four static nodes: sink (0), two relays (1, 2) at the same depth, source (3). Both relays are heads.
struct TwoRelayNetwork
{
    Ptr<MetricsCollector> metrics = CreateObject<MetricsCollector>();
    Ptr<CbErgrPropagation> propagation;
    std::vector<Ptr<ErgrEmhcRouting>> routing;
    std::vector<Ptr<AquaSimEnergyModel>> energy;

    explicit TwoRelayNetwork(const std::string& outDir, bool headerFirstRx = false)
    {
        MetricsCollector::Config mc;
        mc.outDir = outDir;
        mc.simTimeS = 1.0e9;
        mc.logDecisions = true;
        metrics->Configure(mc);
        NodeContainer nodes;
        nodes.Create(4);
        Ptr<AquaSimChannel> channel = CbErgrHelper::CreateChannel(propagation, 0.0, 120.0);
        propagation->AssignStreams(10);
        CbErgrHelper::ConnectLinkTrace(propagation, metrics);
        AquaSimHelper asHelper = AquaSimHelper::Default();
        asHelper.SetChannel(channel);
        asHelper.SetMac("ns3::AquaSimBroadcastMac");
        asHelper.SetRouting("ns3::cbergr::ErgrEmhcRouting");
        CbErgrHelper::ConfigureHelper(asHelper, 0.1, 1e9, 2.0, 0.75, 0.008);
        const Vector positions[4] = {Vector(1100, 1100, 100), Vector(900, 1100, 1500), Vector(1300, 1100, 1500),
                                     Vector(1100, 1100, 2300)};
        for (int i = 0; i < 4; ++i)
        {
            Ptr<MobilityModel> mobility = CreateObject<ConstantPositionMobilityModel>();
            nodes.Get(i)->AggregateObject(mobility);
            mobility->SetPosition(positions[i]);
            Ptr<AquaSimNetDevice> device = CreateObject<AquaSimNetDevice>();
            asHelper.Create(nodes.Get(i), device);
            CbErgrHelper::ConfigureDevice(device, 5000.0);
            if (headerFirstRx)
            {
                CbErgrHelper::EnableHeaderFirstRx(device, i, 0.1, 5000.0);
            }
            device->GetMac()->AssignStreams(20 + i);
            Ptr<ErgrEmhcRouting> r = DynamicCast<ErgrEmhcRouting>(device->GetRouting());
            r->SetAttribute("AutoElection", BooleanValue(false));
            r->SetAttribute("PktRatePerNodeHz", DoubleValue(0.0));
            r->AssignStreams(30 + 2 * i);
            r->SetNodeRole(i == 0);
            r->SetMetrics(metrics);
            ObjectFactory factory;
            factory.SetTypeId("ns3::cbergr::SelectorRoundRobin");
            factory.Set("WithFeedback", BooleanValue(true));
            r->SetSelector(factory.Create<ForwarderSelector>());
            metrics->RegisterNode(i, i == 0, device->GetPhy()->EM());
            energy.push_back(device->GetPhy()->EM());
            routing.push_back(r);
        }
        for (auto& r : routing)
        {
            r->Start();
        }
        routing[1]->ForceRole(1);
        routing[2]->ForceRole(1);
        routing[3]->ForceRole(1); // the source is a head too (a member would hand over to a head of its own cell)
    }
};

// D§3 P4 suspect tier: after two ACK timeouts in a row the dead relay is skipped for a beacon interval,
// so the source stops wasting attempts on it and uses the other relay.
class DeadRelayIsSuspectedAndAvoided : public TestCase
{
  public:
    DeadRelayIsSuspectedAndAvoided()
        : TestCase("D7: two ACK timeouts make a dead forwarder suspect and traffic moves to the other relay")
    {
    }

  private:
    void DoRun() override
    {
        std::string dir = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-suspect";
        RngSeedManager::SetSeed(9);
        TwoRelayNetwork net(dir);
        for (int k = 0; k < 40; ++k)
        {
            Simulator::Schedule(Seconds(200.0 + 6.0 * k), &ErgrEmhcRouting::InjectPacket, net.routing[3]);
        }
        const double killAt = 260.0;
        Simulator::Schedule(Seconds(killAt), &ErgrEmhcRouting::Kill, net.routing[1]);
        Simulator::Schedule(Seconds(900.0), &MetricsCollector::Finalize, net.metrics);
        Simulator::Stop(Seconds(900.001));
        Simulator::Run();
        Simulator::Destroy();

        std::vector<CsvRow> events = ReadCsv(dir + "/events.csv");
        int suspects = 0;
        for (const CsvRow& e : events)
        {
            suspects += (e.at("event") == "HEAD_SUSPECT" && e.at("details") == "fwd=1");
        }
        NS_TEST_ASSERT_MSG_EQ((suspects >= 1), true, "the dead relay was suspected");

        // Attempts from the source to the dead relay in the 40 s after it died (excluding what was in flight).
        std::vector<CsvRow> decisions = ReadCsv(dir + "/decisions.csv");
        int toDeadSoon = 0, toLiveSoon = 0;
        for (const CsvRow& r : decisions)
        {
            double t = std::stod(r.at("time_s"));
            if (r.at("node") == "3" && t > killAt + 2.0 && t < killAt + 40.0)
            {
                (r.at("fwd") == "1" ? toDeadSoon : toLiveSoon)++;
            }
        }
        NS_TEST_ASSERT_MSG_EQ((toDeadSoon <= 3), true, "at most a few attempts to the dead relay (" << toDeadSoon << ")");
        NS_TEST_ASSERT_MSG_EQ((toLiveSoon >= 3), true, "traffic goes to the live relay (" << toLiveSoon << ")");
        NS_TEST_ASSERT_MSG_EQ((net.metrics->PacketsDelivered() > 20), true, "most packets still arrive");
        std::filesystem::remove_all(dir);
    }
};

// Failure injection in a full scenario: killed heads are replaced by an election.
class KilledHeadsAreReplacedByElections : public TestCase
{
  public:
    KilledHeadsAreReplacedByElections()
        : TestCase("D7: failure injection kills heads in several cells and the cells elect new ones")
    {
    }

  private:
    void DoRun() override
    {
        std::string dir = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-kill";
        ScenarioSettings settings;
        settings.outDir = dir;
        settings.seed = 4;
        settings.numNodes = 60;
        settings.cellSizeM = 1500.0; // 8 big cells with several nodes each
        settings.simTimeS = 1500;
        settings.warmupS = 300;
        settings.pktRateHz = 0.004;
        settings.currentSpeed = 0.0; // static, so cells keep their nodes
        settings.killHeadsAtS = 800;
        settings.killHeadsCells = 3;
        ScenarioConfig config;
        RegisterSettings(config, settings);
        RunScenario(settings, config);

        std::vector<CsvRow> events = ReadCsv(dir + "/events.csv");
        std::set<std::string> killedCells;
        for (const CsvRow& e : events)
        {
            if (e.at("event") == "NODE_KILLED")
            {
                NS_TEST_ASSERT_MSG_EQ((std::stod(e.at("time_s")) >= 800.0 - 1e-6), true, "killed at the chosen time");
                size_t at = e.at("details").find("cell=");
                std::string rest = e.at("details").substr(at + 5);
                killedCells.insert(rest.substr(0, rest.find(' ')));   // details look like "cell=2 head=1"
            }
        }
        NS_TEST_ASSERT_MSG_EQ(killedCells.size(), 3u, "three cells lost a head");
        // Every such cell elects a head again after the failure.
        for (const std::string& cell : killedCells)
        {
            bool replaced = false;
            for (const CsvRow& e : events)
            {
                if (e.at("event") == "HEAD_ELECTED" && std::stod(e.at("time_s")) > 800.0 &&
                    e.at("details").find("cell=" + cell + " ") != std::string::npos)
                {
                    replaced = true;
                }
            }
            NS_TEST_ASSERT_MSG_EQ(replaced, true, "cell " << cell << " elected a new head");
        }
        std::filesystem::remove_all(dir);
    }
};


// D§3 P4 quorum: a killed head is declared dead by a quorum well before the 3-period table expiry; with an impossible
// quorum no quorum declaration happens at all (one node alone never declares a head dead).
class QuorumDetectsAKilledHeadEarlyAndNeedsAQuorum : public TestCase
{
  public:
    QuorumDetectsAKilledHeadEarlyAndNeedsAQuorum()
        : TestCase("D3 P4: quorum dead-head detection is early, needs the quorum, and is off by default")
    {
    }

  private:
    struct Outcome
    {
        double firstQuorumAfterKillS = -1.0;
        int quorumDeclarations = 0;
        int reports = 0;
        double firstKillS = -1.0;
    };

    static Outcome Run(const std::string& dir, bool quorum, int needed)
    {
        ScenarioSettings settings;
        settings.outDir = dir;
        settings.seed = 4;
        settings.numNodes = 60;
        settings.cellSizeM = 1500.0;
        settings.simTimeS = 1300;
        settings.warmupS = 300;
        settings.pktRateHz = 0.004;
        settings.currentSpeed = 0.0;
        settings.killHeadsAtS = 800;
        settings.killHeadsCells = 3;
        settings.quorumDetection = quorum;
        settings.deadQuorum = needed;
        ScenarioConfig config;
        RegisterSettings(config, settings);
        RunScenario(settings, config);
        Outcome o;
        for (const CsvRow& e : ReadCsv(dir + "/events.csv"))
        {
            const double t = std::stod(e.at("time_s"));
            if (e.at("event") == "NODE_KILLED" && o.firstKillS < 0)
            {
                o.firstKillS = t;
            }
            if (e.at("event") == "DEAD_REPORT")
            {
                o.reports++;
            }
            if (e.at("event") == "HEAD_DEAD" && e.at("details").find("quorum") != std::string::npos)
            {
                o.quorumDeclarations++;
                if (o.firstQuorumAfterKillS < 0 && t >= 800.0)
                {
                    o.firstQuorumAfterKillS = t - 800.0;
                }
            }
        }
        std::filesystem::remove_all(dir);
        return o;
    }

    void DoRun() override
    {
        std::string base = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-quorum";
        Outcome off = Run(base + "-off", false, 2);
        Outcome on = Run(base + "-on", true, 2);
        Outcome impossible = Run(base + "-imp", true, 1000);
        NS_TEST_ASSERT_MSG_EQ(off.quorumDeclarations, 0, "off by default: no quorum declarations");
        NS_TEST_ASSERT_MSG_EQ(off.reports, 0, "off by default: no dead reports");
        NS_TEST_ASSERT_MSG_EQ((on.quorumDeclarations > 0), true, "a quorum declared the killed head dead");
        NS_TEST_ASSERT_MSG_EQ((on.firstQuorumAfterKillS >= 0.0 && on.firstQuorumAfterKillS < 150.0), true,
                              "detected within the 3-period backstop (150 s): " << on.firstQuorumAfterKillS);
        NS_TEST_ASSERT_MSG_EQ((on.reports > 0), true, "observers sent reports");
        NS_TEST_ASSERT_MSG_EQ(impossible.quorumDeclarations, 0, "no quorum, no declaration");
    }
};


// ============================================================ D7 energy ====

// N-A (D§4.2): overheard unicast DATA is charged for preamble + header only; everything else in full.
class HeaderFirstReceiveChargesOnlyOverheardData : public TestCase
{
  public:
    HeaderFirstReceiveChargesOnlyOverheardData()
        : TestCase("D7: header-first receive charge per frame type")
    {
    }

  private:
    static Ptr<Packet> Frame(const Header& routingHeader, uint32_t payload)
    {
        Ptr<Packet> p = Create<Packet>(payload);
        p->AddHeader(routingHeader);
        MacHeader mac;
        p->AddHeader(mac);
        return p;
    }

    void DoRun() override
    {
        ErgrDataHeader data;
        data.forwarderId = 7;
        Ptr<Packet> toOther = Frame(data, 150);
        // 0.1 s preamble + 17 B header at 5 kbit/s (0.0272 s)
        NS_TEST_ASSERT_MSG_EQ_TOL(CbErgrHelper::HeaderFirstChargeS(toOther, 9, 0.1, 5000.0), 0.1272, 1e-9,
                                  "overheard data: preamble + header");
        NS_TEST_ASSERT_MSG_EQ_TOL(CbErgrHelper::HeaderFirstChargeS(toOther, 7, 0.1, 5000.0), -1.0, 0.0,
                                  "addressed to me: whole frame");
        ErgrBeaconHeader beacon;
        NS_TEST_ASSERT_MSG_EQ_TOL(CbErgrHelper::HeaderFirstChargeS(Frame(beacon, 0), 9, 0.1, 5000.0), -1.0, 0.0,
                                  "beacons are received in full");
        ErgrAckHeader ack;
        ack.senderId = 7;
        NS_TEST_ASSERT_MSG_EQ_TOL(CbErgrHelper::HeaderFirstChargeS(Frame(ack, 0), 9, 0.1, 5000.0), -1.0, 0.0,
                                  "ACKs are received in full");
    }
};

// ... and the PHY applies it: a node that overhears data spends less receive energy with N-A on.
class HeaderFirstReceiveSavesEnergyOnOverheardData : public TestCase
{
  public:
    HeaderFirstReceiveSavesEnergyOnOverheardData()
        : TestCase("D7: N-A lowers the receive energy of a node that overhears data")
    {
    }

  private:
    double OverheardRxEnergyJ(bool headerFirst)
    {
        std::string dir = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-na";
        RngSeedManager::SetSeed(12);
        TwoRelayNetwork net(dir, headerFirst);
        for (int k = 0; k < 30; ++k)
        {
            Simulator::Schedule(Seconds(200.0 + 8.0 * k), &ErgrEmhcRouting::InjectPacket, net.routing[3]);
        }
        Simulator::Schedule(Seconds(700.0), &MetricsCollector::Finalize, net.metrics);
        Simulator::Stop(Seconds(700.001));
        Simulator::Run();
        double rxJ = net.energy[2]->GetRxEnergyJ(); // relay 2 overhears what the source sends to relay 1
        Simulator::Destroy();
        std::filesystem::remove_all(dir);
        return rxJ;
    }

    void DoRun() override
    {
        double full = OverheardRxEnergyJ(false);
        double headerOnly = OverheardRxEnergyJ(true);
        NS_TEST_ASSERT_MSG_EQ((headerOnly < full), true, "less receive energy with N-A (" << headerOnly << " vs " << full << " J)");
        NS_TEST_ASSERT_MSG_EQ((headerOnly > 0.5 * full), true, "beacons still dominate, so the saving is partial");
    }
};

// N-C (D§4.2): with lean beacons the network sends far fewer beacons but still elects heads and delivers data.
class LeanBeaconsCutBeaconTrafficButKeepTheNetworkWorking : public TestCase
{
  public:
    LeanBeaconsCutBeaconTrafficButKeepTheNetworkWorking()
        : TestCase("D7: lean beacons reduce beacon frames and the network still works")
    {
    }

  private:
    struct Outcome
    {
        uint64_t beacons;
        ScenarioResult result;
        bool elected;
    };

    Outcome Run(bool lean)
    {
        std::string dir = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-lean";
        ScenarioSettings settings;
        settings.outDir = dir;
        settings.seed = 21;
        settings.numNodes = 50;
        settings.simTimeS = 1500;
        settings.warmupS = 300;
        settings.pktRateHz = 0.01;
        settings.initialEnergyJ = 10000;
        settings.cellSizeM = 1500.0; // 8 cells of ~6 nodes, so most nodes are members that can stay quiet
        settings.leanBeacons = lean;
        ScenarioConfig config;
        RegisterSettings(config, settings);
        Outcome o;
        o.result = RunScenario(settings, config);
        o.beacons = 0;
        for (const CsvRow& n : ReadCsv(dir + "/nodes.csv"))
        {
            o.beacons += std::stoull(n.at("frames_tx_beacon"));
        }
        o.elected = ReadFile(dir + "/events.csv").find("HEAD_ELECTED") != std::string::npos;
        std::filesystem::remove_all(dir);
        return o;
    }

    void DoRun() override
    {
        Outcome normal = Run(false);
        Outcome lean = Run(true);
        NS_TEST_ASSERT_MSG_EQ((lean.beacons < 0.6 * normal.beacons), true,
                              "beacons " << lean.beacons << " (lean) vs " << normal.beacons);
        NS_TEST_ASSERT_MSG_EQ(lean.elected, true, "heads are still elected");
        NS_TEST_ASSERT_MSG_EQ((lean.result.packetsDelivered > 0), true, "data still reaches a sink");
        NS_TEST_ASSERT_MSG_EQ((lean.result.pdr > 0.5 * normal.result.pdr), true,
                              "PDR " << lean.result.pdr << " vs " << normal.result.pdr);
    }
};


// ============================================================ D7 power =====

// Spec test 11 (§4.2, the gate for the power arm): two static nodes; per distance and power level, 2000 frames.
// Success must rise with the power level at 1400 m, match the link model, and the energy per transmission must
// equal P_tx[level] x airtime within 1%.
class PowerLevelsChangeSuccessAndEnergy : public TestCase
{
  public:
    PowerLevelsChangeSuccessAndEnergy()
        : TestCase("D7 test11: two nodes, 3 power levels, success monotone and energy = P_tx x airtime")
    {
    }

  private:
    static uint32_t s_received[3];
    static void OnPhyRx(Ptr<Packet> packet, double noise)
    {
        // The block (= power level) is identified by the time the frame arrives.
        int block = static_cast<int>((Simulator::Now().GetSeconds() - 10.0) / 6100.0);
        if (block >= 0 && block < 3)
        {
            s_received[block]++;
        }
    }

    void DoRun() override
    {
        const double distances[4] = {500.0, 1000.0, 1250.0, 1400.0};
        const int framesPerLevel = 2000;
        double successAt1250[3] = {0, 0, 0};
        double successAt1400[3] = {0, 0, 0};
        AcousticLinkModel link;

        for (double distanceM : distances)
        {
            std::string dir = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-power";
            RngSeedManager::SetSeed(31);
            Ptr<MetricsCollector> metrics = CreateObject<MetricsCollector>();
            MetricsCollector::Config mc;
            mc.outDir = dir;
            mc.simTimeS = 1.0e9;
            metrics->Configure(mc);
            NodeContainer nodes;
            nodes.Create(2);
            Ptr<CbErgrPropagation> propagation;
            Ptr<AquaSimChannel> channel = CbErgrHelper::CreateChannel(propagation, 0.0, 120.0); // no shadowing
            propagation->AssignStreams(10);
            AquaSimHelper asHelper = AquaSimHelper::Default();
            asHelper.SetChannel(channel);
            asHelper.SetMac("ns3::AquaSimBroadcastMac");
            asHelper.SetRouting("ns3::cbergr::ErgrEmhcRouting");
            CbErgrHelper::ConfigureHelper(asHelper, 0.1, 1e12, 2.0, 0.75, 0.008);
            Ptr<AquaSimNetDevice> devices[2];
            Ptr<ErgrEmhcRouting> routing[2];
            for (int i = 0; i < 2; ++i)
            {
                Ptr<MobilityModel> mobility = CreateObject<ConstantPositionMobilityModel>();
                nodes.Get(i)->AggregateObject(mobility);
                mobility->SetPosition(Vector(i == 0 ? 0.0 : distanceM, 0, 0));
                devices[i] = CreateObject<AquaSimNetDevice>();
                asHelper.Create(nodes.Get(i), devices[i]);
                CbErgrHelper::ConfigureDevice(devices[i], 5000.0);
                CbErgrHelper::EnablePowerLevels(devices[i], 2.0);
                devices[i]->GetMac()->AssignStreams(20 + i);
                routing[i] = DynamicCast<ErgrEmhcRouting>(devices[i]->GetRouting());
                routing[i]->SetAttribute("AutoElection", BooleanValue(false));
                routing[i]->SetAttribute("PktRatePerNodeHz", DoubleValue(0.0));
                routing[i]->SetAttribute("BeaconPeriodS", DoubleValue(1.0e7)); // no beacons: only the test frames
                routing[i]->AssignStreams(30 + 2 * i);
                routing[i]->SetNodeRole(false);
                routing[i]->SetMetrics(metrics);
                ObjectFactory factory;
                factory.SetTypeId("ns3::cbergr::SelectorRoundRobin");
                routing[i]->SetSelector(factory.Create<ForwarderSelector>());
                metrics->RegisterNode(i, false, devices[i]->GetPhy()->EM());
            }
            devices[1]->GetPhy()->TraceConnectWithoutContext("Rx", MakeCallback(&OnPhyRx));
            for (int i = 0; i < 2; ++i)
            {
                routing[i]->Start();
            }
            std::fill(s_received, s_received + 3, 0u);

            double txEnergyAtBlockStart[3] = {0, 0, 0}, txEnergyAtBlockEnd[3] = {0, 0, 0};
            Ptr<AquaSimEnergyModel> txEnergy = devices[0]->GetPhy()->EM();
            for (int level = 0; level < 3; ++level)
            {
                const double blockStart = 10.0 + level * 6100.0;
                for (int k = 0; k < framesPerLevel; ++k)
                {
                    Simulator::Schedule(Seconds(blockStart + 3.0 * k), &ErgrEmhcRouting::InjectRawFrame, routing[0],
                                        static_cast<uint16_t>(1), static_cast<uint8_t>(level));
                }
                Simulator::Schedule(Seconds(blockStart - 1.0), [&txEnergyAtBlockStart, txEnergy, level]() {
                    txEnergyAtBlockStart[level] = txEnergy->GetTxEnergyJ();
                });
                Simulator::Schedule(Seconds(blockStart + 6050.0), [&txEnergyAtBlockEnd, txEnergy, level]() {
                    txEnergyAtBlockEnd[level] = txEnergy->GetTxEnergyJ();
                });
            }
            Simulator::Stop(Seconds(10.0 + 3 * 6100.0));
            Simulator::Run();

            Ptr<AquaSimPhyCmn> phy = DynamicCast<AquaSimPhyCmn>(devices[0]->GetPhy());
            const double airtimeS = phy->CalcTxTime(17 + 150).GetSeconds();
            for (int level = 0; level < 3; ++level)
            {
                const double success = static_cast<double>(s_received[level]) / framesPerLevel;
                const double expectedPower = 2.0 * params::phy::kTxPowerLevelScale[level];
                const double energyPerTx = (txEnergyAtBlockEnd[level] - txEnergyAtBlockStart[level]) / framesPerLevel;
                NS_TEST_ASSERT_MSG_EQ_TOL(energyPerTx, expectedPower * airtimeS, 0.01 * expectedPower * airtimeS,
                                          "energy per transmission, level " << level << " at " << distanceM << " m");
                const double sl = link.FullSourceLevelDb() + params::phy::kSourceLevelOffsetDb[level];
                const double analytic = 1.0 - link.PerFromSnr(link.SnrDb(sl, distanceM, 0.0), 167);
                NS_TEST_ASSERT_MSG_EQ_TOL(success, analytic, 0.035, "success vs link model, level " << level << " at " << distanceM << " m");
                if (distanceM == 1250.0)
                {
                    successAt1250[level] = success;
                }
                if (distanceM == 1400.0)
                {
                    successAt1400[level] = success;
                }
            }
            Simulator::Destroy();
            std::filesystem::remove_all(dir);
        }
        // At 1400 m the -3 dB and -6 dB levels fall off the BFSK cliff (success 0), so the order is non-decreasing there;
        // at 1250 m all three levels differ.
        NS_TEST_ASSERT_MSG_EQ((successAt1400[0] <= successAt1400[1] && successAt1400[1] < successAt1400[2]), true,
                              "success does not fall with power at 1400 m: " << successAt1400[0] << " <= "
                                                                             << successAt1400[1] << " < " << successAt1400[2]);
        NS_TEST_ASSERT_MSG_EQ((successAt1250[0] < successAt1250[1] && successAt1250[1] < successAt1250[2]), true,
                              "success rises strictly with power at 1250 m: " << successAt1250[0] << " < " << successAt1250[1]
                                                                              << " < " << successAt1250[2]);
    }
};
uint32_t PowerLevelsChangeSuccessAndEnergy::s_received[3] = {0, 0, 0};

// ======================================================== the suite =========


// ===================================================== P0 (published) ======

// The paper's 32-neighbour relation and the grid-based Hmin (ERGR-EMHC §4.1).
class PublishedGeometryHas32NeighboursAndBfsHmin : public TestCase
{
  public:
    PublishedGeometryHas32NeighboursAndBfsHmin()
        : TestCase("P0: 32 neighbour cells and Hmin from a breadth-first search")
    {
    }

  private:
    void DoRun() override
    {
        GridGeometry g(3000.0, 433.0127); // 7 cells per axis
        NS_TEST_ASSERT_MSG_EQ(g.CellsPerAxis(), 7, "3000 / 433");
        uint16_t centre = g.CellIdOf(Vector(1500, 1500, 1500)); // cell (3,3,3)
        NS_TEST_ASSERT_MSG_EQ(g.NeighbourCellsPaper(centre).size(), 32u, "26 adjacent + 6 two steps along one axis");
        NS_TEST_ASSERT_MSG_EQ(g.AreNeighboursPaper(centre, g.CellIdOf(Vector(1500 + 2 * 433.0127, 1500, 1500))), true,
                              "two steps along x");
        NS_TEST_ASSERT_MSG_EQ(g.AreNeighboursPaper(centre, g.CellIdOf(Vector(1500 + 2 * 433.0127, 1500 + 433.0127, 1500))),
                              false, "two steps in x and one in y is not a neighbour");
        NS_TEST_ASSERT_MSG_EQ(g.AreNeighboursPaper(centre, centre), false, "not its own neighbour");
        // The relation is symmetric.
        for (uint16_t a = 0; a < g.NumCells(); a += 7)
        {
            for (uint16_t b : g.NeighbourCellsPaper(a))
            {
                NS_TEST_ASSERT_MSG_EQ(g.AreNeighboursPaper(b, a), true, "symmetric");
            }
        }
        // Hmin: a sink at the surface; its cell and all its neighbours are 1; every other reachable cell has a
        // neighbour that is one lower.
        std::vector<uint8_t> hmin = g.HminTable({Vector(1500, 1500, 0)});
        uint16_t sinkCell = g.CellIdOf(Vector(1500, 1500, 0));
        NS_TEST_ASSERT_MSG_EQ(static_cast<int>(hmin[sinkCell]), 1, "the sink's cell");
        for (uint16_t n : g.NeighbourCellsPaper(sinkCell))
        {
            NS_TEST_ASSERT_MSG_EQ(static_cast<int>(hmin[n]), 1, "neighbours of the sink cell");
        }
        int maxHmin = 0;
        for (uint16_t c = 0; c < g.NumCells(); ++c)
        {
            maxHmin = std::max<int>(maxHmin, hmin[c]);
            if (hmin[c] > 1)
            {
                bool lower = false;
                for (uint16_t n : g.NeighbourCellsPaper(c))
                {
                    lower = lower || hmin[n] == hmin[c] - 1;
                }
                NS_TEST_ASSERT_MSG_EQ(lower, true, "every cell with Hmin > 1 has a neighbour with Hmin - 1");
            }
        }
        // Farthest cell (0,0,6) from the sink cell (3,3,0): three diagonal steps, then a 2-step and a 1-step along z.
        NS_TEST_ASSERT_MSG_EQ(maxHmin, 5, "deepest cell is 5 hops from the sink");
    }
};

static ScenarioResult
RunTinyP0(const std::string& outDir, int seed, double speed)
{
    ScenarioSettings settings;
    settings.protocol = "P0";
    settings.outDir = outDir;
    settings.seed = seed;
    settings.numNodes = 120;
    settings.numSinks = 3;
    settings.numSources = 10;
    settings.cellSizeM = 433.0127;
    settings.simTimeS = 700;
    settings.warmupS = 300;
    settings.dataStartS = 100;
    settings.pktRateHz = 0.07;
    settings.currentSpeed = speed;
    settings.idealLinks = true;
    settings.shadowSigmaDb = 0.0;
    settings.initialEnergyJ = 300.0;
    ScenarioConfig config;
    RegisterSettings(config, settings);
    return RunScenario(settings, config);
}

// P0 runs end to end: heads are elected, packets reach the sinks over several hops, no beacon/ACK frames exist
// (the protocol has none), only sources create traffic, and the same seed gives the same files.
class PublishedProtocolRunsAndIsDeterministic : public TestCase
{
  public:
    PublishedProtocolRunsAndIsDeterministic()
        : TestCase("P0: elections, multi-hop delivery, no beacons or ACKs, determinism")
    {
    }

  private:
    void DoRun() override
    {
        std::string base = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-p0";
        ScenarioResult a = RunTinyP0(base + "-a", 5, 1.0);
        RunTinyP0(base + "-b", 5, 1.0);
        NS_TEST_ASSERT_MSG_EQ((a.packetsCreated > 50), true, "ten sources at 0.07 pkt/s create traffic");
        NS_TEST_ASSERT_MSG_EQ((a.packetsDelivered > 0), true, "something reaches a sink");
        NS_TEST_ASSERT_MSG_EQ((ReadFile(base + "-a/events.csv").find("HEAD_ELECTED") != std::string::npos), true, "heads");
        auto summary = ReadCsv(base + "-a/summary.csv");
        NS_TEST_ASSERT_MSG_EQ(summary.size(), 1u, "one summary row");
        NS_TEST_ASSERT_MSG_EQ(std::stod(summary[0]["ctrl_bytes_beacon_per_delivered"]), 0.0, "no beacons in P0");
        NS_TEST_ASSERT_MSG_EQ(std::stod(summary[0]["ack_frames_per_delivered"]), 0.0, "no ACKs in P0");
        // Only the 10 chosen sources originate packets.
        int originators = 0;
        for (auto& row : ReadCsv(base + "-a/nodes.csv"))
        {
            originators += (std::stod(row["data_originated"]) > 0) ? 1 : 0;
        }
        NS_TEST_ASSERT_MSG_EQ((originators >= 1 && originators <= 10), true, "at most NumSources nodes originate data");
        for (const char* file : {"summary.csv", "packets.csv", "nodes.csv", "events.csv"})
        {
            NS_TEST_ASSERT_MSG_EQ((ReadFile(base + "-a/" + file) == ReadFile(base + "-b/" + file)), true,
                                  file << " differs between two runs with the same seed");
        }
        std::filesystem::remove_all(base + "-a");
        std::filesystem::remove_all(base + "-b");
    }
};

// Mac=Csma (listen-before-talk with backoff) and Mac=Aloha (Aqua-Sim broadcast MAC) both run the same scenario; CSMA counts
// its deferrals, and the run stays deterministic.
class CsmaAndAlohaMacsBothWork : public TestCase
{
  public:
    CsmaAndAlohaMacsBothWork()
        : TestCase("MAC: Csma defers on a busy channel and delivers; Aloha still works; same seed gives the same CSMA run")
    {
    }

  private:
    static ScenarioResult Run(const std::string& dir, const std::string& mac)
    {
        ScenarioSettings settings;
        settings.mac = mac;
        settings.outDir = dir;
        settings.seed = 9;
        settings.numNodes = 60;
        settings.simTimeS = 700;
        settings.warmupS = 300;
        settings.pktRateHz = 0.05; // enough load for the channel to be busy now and then
        settings.currentSpeed = 0.0;
        ScenarioConfig config;
        RegisterSettings(config, settings);
        return RunScenario(settings, config);
    }

    void DoRun() override
    {
        std::string base = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-mac";
        ScenarioResult csma = Run(base + "-c1", "Csma");
        Run(base + "-c2", "Csma");
        ScenarioResult aloha = Run(base + "-a", "Aloha");
        NS_TEST_ASSERT_MSG_EQ((csma.packetsDelivered > 0 && aloha.packetsDelivered > 0), true, "both MACs deliver");
        auto csmaRow = ReadCsv(base + "-c1/summary.csv")[0];
        auto alohaRow = ReadCsv(base + "-a/summary.csv")[0];
        NS_TEST_ASSERT_MSG_EQ((std::stod(csmaRow["mac_deferrals"]) > 0.0), true, "CSMA deferred at least once");
        NS_TEST_ASSERT_MSG_EQ(std::stod(alohaRow["mac_deferrals"]), 0.0, "Aloha reports no CSMA deferrals");
        NS_TEST_ASSERT_MSG_EQ((ReadFile(base + "-c1/packets.csv") == ReadFile(base + "-c2/packets.csv")), true,
                              "CSMA is deterministic");
        for (const char* s : {"-c1", "-c2", "-a"})
        {
            std::filesystem::remove_all(base + s);
        }
    }
};


// E34: a member must always know the head of its own cell. With lean beacons on, nobody may give up a packet for "no own head"
// while the cell has a live head (ground truth), the cells keep their heads, and the solicit path is used.
class MembersAlwaysFindTheirCellsHead : public TestCase
{
  public:
    MembersAlwaysFindTheirCellsHead()
        : TestCase("E34: no packet dies at the source for 'no own head' while the cell has a head; cells stay headed")
    {
    }

  private:
    void DoRun() override
    {
        std::string dir = std::filesystem::temp_directory_path().string() + "/cb-ergr-test-e34";
        ScenarioSettings settings;
        settings.outDir = dir;
        settings.seed = 5;
        settings.numNodes = 120;
        settings.cellSizeM = 750.0;
        settings.simTimeS = 1300;
        settings.warmupS = 300;
        settings.pktRateHz = 0.01;
        settings.currentSpeed = 1.0; // nodes cross cell borders, so heads leave and cells need new ones
        settings.leanBeacons = true;
        ScenarioConfig config;
        RegisterSettings(config, settings);
        ScenarioResult r = RunScenario(settings, config);
        auto row = ReadCsv(dir + "/summary.csv")[0];
        NS_TEST_ASSERT_MSG_EQ((r.packetsCreated > 100), true, "traffic");
        NS_TEST_ASSERT_MSG_EQ(std::stod(row["nohead_giveups_cell_had_head"]), 0.0,
                              "no give-up for 'no own head' while the cell had a head");
        NS_TEST_ASSERT_MSG_EQ((std::stod(row["cells_with_head_share"]) > 0.9), true,
                              "occupied cells have a head at least 90% of the time: " << row["cells_with_head_share"]);
        NS_TEST_ASSERT_MSG_EQ((std::stod(row["nodes_in_headless_cells_share"]) < 0.1), true, "few nodes without a head");
        std::filesystem::remove_all(dir);
    }
};


class CbErgrTestSuite : public TestSuite
{
  public:
    CbErgrTestSuite()
        : TestSuite("cb-ergr", UNIT)
    {
        AddTestCase(new UpstreamTypesAreRegistered, TestCase::QUICK);
        AddTestCase(new CholeskyAndSolvesMatchHandComputedValues, TestCase::QUICK);
        AddTestCase(new CholeskyFallbackJittersThenResets, TestCase::QUICK);
        AddTestCase(new SigmoidIsClamped, TestCase::QUICK);
        AddTestCase(new LogisticNewtonStepConvergesOnToyData, TestCase::QUICK);
        AddTestCase(new ThompsonSamplesHaveInverseCovariance<3>, TestCase::QUICK);
        AddTestCase(new ThompsonSamplesHaveInverseCovariance<9>, TestCase::QUICK);
        AddTestCase(new ReceiptWindowTracksGapsAndWrap, TestCase::QUICK);
        AddTestCase(new SerialNumberArithmeticFollowsRfc1982, TestCase::QUICK);
        AddTestCase(new ForgettingDecaysToThePrior, TestCase::QUICK);
        AddTestCase(new EnergyPriceMovesTheRightWayAndStaysNonNegative, TestCase::QUICK);
        AddTestCase(new SharedEvidenceIsNotDoubleCounted, TestCase::QUICK);
        AddTestCase(new HandoverScalesOnlyWhenUnplanned, TestCase::QUICK);
        AddTestCase(new CellPriorAveragesAcceptedPeers, TestCase::QUICK);
        AddTestCase(new AirtimeIncludesTheExactPreamble, TestCase::QUICK);
        AddTestCase(new LinkBudgetIsCalibratedAtRange, TestCase::QUICK);
        AddTestCase(new ShadowingIsGaussMarkovAndAccessOrderFree, TestCase::QUICK);
        AddTestCase(new HeadersRoundTripWithExactSizes, TestCase::QUICK);
        AddTestCase(new GridGeometryIndexesCellsAndNeighbours, TestCase::QUICK);
        AddTestCase(new NeighbourTableTracksHeadsAndExpires, TestCase::QUICK);
        AddTestCase(new RecentSetRemembersWithinCapacityAndTtl, TestCase::QUICK);
        AddTestCase(new SmallScenarioRunsAndIsConsistent, TestCase::QUICK);
        AddTestCase(new SameSeedGivesIdenticalOutput, TestCase::QUICK);
        AddTestCase(new ForcedLossGivesOneLabelPerAttempt, TestCase::QUICK);
        AddTestCase(new LostAckGivesTimeoutLabelAndDeduplicatedRetry, TestCase::QUICK);
        AddTestCase(new FeaturesMatchHandComputedValues, TestCase::QUICK);
        AddTestCase(new ScoreCombinesSuccessProgressAndEnergy, TestCase::QUICK);
        AddTestCase(new AckGateFollowsTheModeAndUncertainty, TestCase::QUICK);
        AddTestCase(new CbLearnsToPreferTheReliableLink, TestCase::QUICK);
        AddTestCase(new SharedModelRaisesTheCoHeadsConfidence, TestCase::QUICK);
        AddTestCase(new FixedWeightPrefersTheBetterLinkAndNeverLearns, TestCase::QUICK);
        AddTestCase(new CarpEwmaFollowsItsUpdateRule, TestCase::QUICK);
        AddTestCase(new QLearningUpdatesAndAdvertisesItsValue, TestCase::QUICK);
        AddTestCase(new LinearQMovesItsWeightsTowardTheTarget, TestCase::QUICK);
        AddTestCase(new OracleSuccessMatchesTheLinkModel, TestCase::QUICK);
        AddTestCase(new OracleSelectorPicksTheTrulyBestHop, TestCase::QUICK);
        AddTestCase(new FittedLogisticUsesItsOfflineWeights, TestCase::QUICK);
        AddTestCase(new HandoverGivesTheNewHeadTheOldModel, TestCase::QUICK);
        AddTestCase(new DeadRelayIsSuspectedAndAvoided, TestCase::QUICK);
        AddTestCase(new KilledHeadsAreReplacedByElections, TestCase::QUICK);
        AddTestCase(new QuorumDetectsAKilledHeadEarlyAndNeedsAQuorum, TestCase::QUICK);
        AddTestCase(new HeaderFirstReceiveChargesOnlyOverheardData, TestCase::QUICK);
        AddTestCase(new HeaderFirstReceiveSavesEnergyOnOverheardData, TestCase::QUICK);
        AddTestCase(new LeanBeaconsCutBeaconTrafficButKeepTheNetworkWorking, TestCase::QUICK);
        AddTestCase(new PowerLevelsChangeSuccessAndEnergy, TestCase::QUICK);
        AddTestCase(new CsmaAndAlohaMacsBothWork, TestCase::QUICK);
        AddTestCase(new MembersAlwaysFindTheirCellsHead, TestCase::QUICK);
        AddTestCase(new NewHeadStartsFromTheNeighbourCellsModel, TestCase::QUICK);
        AddTestCase(new PublishedGeometryHas32NeighboursAndBfsHmin, TestCase::QUICK);
        AddTestCase(new PublishedProtocolRunsAndIsDeterministic, TestCase::QUICK);
    }
};

static CbErgrTestSuite g_cbErgrTestSuite;
