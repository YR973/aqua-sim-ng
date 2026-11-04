/*
 * energy-price.h: online dual price for energy (spec §8.9).
 *
 * lambdaE rises when the node spends energy faster than its budget rate
 * rho = E0 / TargetLifetime and falls (never below 0) when it spends slower.
 * The score (§8.5) multiplies lambdaE by the transmit energy of an option,
 * so a high price steers the bandit toward cheaper (lower-power) choices.
 * Plain C++, no ns-3 dependency.
 */

#ifndef CB_ERGR_ENERGY_PRICE_H
#define CB_ERGR_ENERGY_PRICE_H

#include "cb-ergr-params.h"

#include <algorithm>

namespace ns3
{
namespace cbergr
{

class EnergyPrice
{
  public:
    struct Config
    {
        double lambdaInit = params::price::kLambdaInit;
        double eta = params::price::kEta;                     // step size
        double initialEnergyJ = params::energy::kInitialEnergyJ;
        double targetLifetimeS = params::price::kTargetLifetimeS;
        double tickS = params::timers::kPriceTickS;
    };

    explicit EnergyPrice(const Config& cfg, double energyNowJ)
        : m_cfg(cfg),
          m_lambda(cfg.lambdaInit),
          m_energyAtLastTickJ(energyNowJ)
    {
    }

    /// Budget spend rate rho in W.
    double BudgetRateW() const { return m_cfg.initialEnergyJ / m_cfg.targetLifetimeS; }

    /// Call every tickS seconds with the current residual energy. Returns the new lambdaE.
    double Tick(double energyNowJ)
    {
        double spentJ = m_energyAtLastTickJ - energyNowJ;
        double spendRateW = spentJ / m_cfg.tickS;
        double step = m_cfg.eta * (spendRateW - BudgetRateW());
        m_lambda = std::max(0.0, m_lambda + step);
        m_energyAtLastTickJ = energyNowJ;
        return m_lambda;
    }

    double Lambda() const { return m_lambda; }

  private:
    Config m_cfg;
    double m_lambda;
    double m_energyAtLastTickJ;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_ENERGY_PRICE_H */
