/*
 * CB-ERGR test suite (spec §14, kickoff §4). Run: ./test.py -s cb-ergr
 *
 * Tests are named after the behaviour they protect, each with a one-line
 * comment naming the spec rule. Sections follow the build steps D0..D7.
 */

#include "ns3/acoustic-link-model.h"
#include "ns3/aqua-sim-phy-cmn.h"
#include "ns3/boolean.h"
#include "ns3/cb-learner-model.h"
#include "ns3/cb-ergr-config.h"
#include "ns3/double.h"
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
#include <random>

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

// ======================================================== the suite =========

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
    }
};

static CbErgrTestSuite g_cbErgrTestSuite;
