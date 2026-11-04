/*
 * small-matrix.h: tiny dense vector/matrix maths for the bandit (spec §8.12).
 *
 * The learner works with D x D symmetric positive-definite matrices
 * (D = 9, or 10 with the optional onward feature). That is small enough to
 * write by hand, so we avoid an external linear-algebra library.
 * Everything here is plain C++ with no ns-3 dependency.
 *
 * Conventions: matrices are stored row-major in a flat array; element
 * (row, col) is a[row * D + col]. "L" always means a lower-triangular
 * Cholesky factor with A = L * L^T.
 */

#ifndef CB_ERGR_SMALL_MATRIX_H
#define CB_ERGR_SMALL_MATRIX_H

#include "cb-ergr-params.h"

#include <array>
#include <cmath>

namespace ns3
{
namespace cbergr
{

/// Fixed-size vector of D doubles.
template <int D>
struct Vec
{
    std::array<double, D> v{}; // zero-initialised

    double& operator[](int i) { return v[i]; }
    double operator[](int i) const { return v[i]; }

    /// Inner product, a . b.
    double Dot(const Vec& other) const
    {
        double sum = 0.0;
        for (int i = 0; i < D; ++i)
        {
            sum += v[i] * other.v[i];
        }
        return sum;
    }

    /// Euclidean length.
    double Norm() const { return std::sqrt(Dot(*this)); }

    /// this += scale * other.
    void AddScaled(const Vec& other, double scale)
    {
        for (int i = 0; i < D; ++i)
        {
            v[i] += scale * other.v[i];
        }
    }
};

/// Fixed-size D x D matrix, row-major.
template <int D>
struct Mat
{
    std::array<double, D * D> a{}; // zero-initialised

    double& At(int row, int col) { return a[row * D + col]; }
    double At(int row, int col) const { return a[row * D + col]; }

    /// scale * I.
    static Mat ScaledIdentity(double scale)
    {
        Mat m;
        for (int i = 0; i < D; ++i)
        {
            m.At(i, i) = scale;
        }
        return m;
    }

    /// this = this * keep + other * add (element-wise blend).
    void Blend(double keep, const Mat& other, double add)
    {
        for (int i = 0; i < D * D; ++i)
        {
            a[i] = a[i] * keep + other.a[i] * add;
        }
    }

    /// Sum of the diagonal. Used as a "how much has been learned" measure (spec §8.10).
    double Trace() const
    {
        double sum = 0.0;
        for (int i = 0; i < D; ++i)
        {
            sum += At(i, i);
        }
        return sum;
    }

    /// Add c to every diagonal entry (used for the Cholesky jitter).
    void AddToDiagonal(double c)
    {
        for (int i = 0; i < D; ++i)
        {
            At(i, i) += c;
        }
    }
};

/// A += c * x x^T (rank-one update; spec §8.7 uses c = p(1-p)).
template <int D>
void
AddOuter(Mat<D>& A, const Vec<D>& x, double c)
{
    for (int i = 0; i < D; ++i)
    {
        for (int j = 0; j < D; ++j)
        {
            A.At(i, j) += c * x[i] * x[j];
        }
    }
}

/// Matrix-vector product, A x.
template <int D>
Vec<D>
MatVec(const Mat<D>& A, const Vec<D>& x)
{
    Vec<D> out;
    for (int i = 0; i < D; ++i)
    {
        double sum = 0.0;
        for (int j = 0; j < D; ++j)
        {
            sum += A.At(i, j) * x[j];
        }
        out[i] = sum;
    }
    return out;
}

/**
 * Plain Cholesky factorisation A = L L^T.
 * \return false if a pivot is <= 0 (A is not positive definite). L is then undefined.
 */
template <int D>
bool
Cholesky(const Mat<D>& A, Mat<D>& L)
{
    L = Mat<D>();
    for (int i = 0; i < D; ++i)
    {
        for (int j = 0; j <= i; ++j)
        {
            double sum = A.At(i, j);
            for (int k = 0; k < j; ++k)
            {
                sum -= L.At(i, k) * L.At(j, k);
            }
            if (i == j)
            {
                if (sum <= 0.0)
                {
                    return false;
                }
                L.At(i, i) = std::sqrt(sum);
            }
            else
            {
                L.At(i, j) = sum / L.At(j, j);
            }
        }
    }
    return true;
}

/// Result of CholeskyWithFallback, so callers can count and log the rare resets.
enum class CholeskyStatus
{
    Ok,        ///< factorised on the first try
    Jittered,  ///< needed a small diagonal jitter
    Reset      ///< failed 3 times: A was reset to lambda0 * I
};

/**
 * Spec §8.12: Cholesky that never fails.
 * If a pivot is <= 0, add jitter (1e-9 * I) to A and retry; after
 * kCholeskyMaxRetries failures reset A = lambda0 * I and factorise that.
 * A is modified in place when jitter or a reset is applied.
 */
template <int D>
CholeskyStatus
CholeskyWithFallback(Mat<D>& A, Mat<D>& L, double lambda0)
{
    for (int attempt = 0; attempt < params::learner::kCholeskyMaxRetries; ++attempt)
    {
        if (Cholesky(A, L))
        {
            return attempt == 0 ? CholeskyStatus::Ok : CholeskyStatus::Jittered;
        }
        A.AddToDiagonal(params::learner::kCholeskyJitter);
    }
    A = Mat<D>::ScaledIdentity(lambda0);
    Cholesky(A, L); // lambda0 * I is always positive definite for lambda0 > 0
    return CholeskyStatus::Reset;
}

/// Solve L y = b for y (L lower triangular).
template <int D>
Vec<D>
ForwardSolve(const Mat<D>& L, const Vec<D>& b)
{
    Vec<D> y;
    for (int i = 0; i < D; ++i)
    {
        double sum = b[i];
        for (int k = 0; k < i; ++k)
        {
            sum -= L.At(i, k) * y[k];
        }
        y[i] = sum / L.At(i, i);
    }
    return y;
}

/// Solve L^T x = y for x (L lower triangular, so L^T is upper triangular).
template <int D>
Vec<D>
BackSolve(const Mat<D>& L, const Vec<D>& y)
{
    Vec<D> x;
    for (int i = D - 1; i >= 0; --i)
    {
        double sum = y[i];
        for (int k = i + 1; k < D; ++k)
        {
            sum -= L.At(k, i) * x[k];
        }
        x[i] = sum / L.At(i, i);
    }
    return x;
}

/// Solve A x = b given the Cholesky factor L of A.
template <int D>
Vec<D>
SolveSPD(const Mat<D>& L, const Vec<D>& b)
{
    return BackSolve(L, ForwardSolve(L, b));
}

/// log det(A) = 2 * sum(log L_ii), given the Cholesky factor L of A.
template <int D>
double
LogDetFromChol(const Mat<D>& L)
{
    double sum = 0.0;
    for (int i = 0; i < D; ++i)
    {
        sum += std::log(L.At(i, i));
    }
    return 2.0 * sum;
}

/// Spec §8.13: logistic function with the argument clamped to [-30, 30].
inline double
Sigmoid(double z)
{
    const double c = params::learner::kSigmoidClamp;
    if (z > c)
    {
        z = c;
    }
    if (z < -c)
    {
        z = -c;
    }
    return 1.0 / (1.0 + std::exp(-z));
}

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SMALL_MATRIX_H */
