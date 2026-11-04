/*
 * acoustic-link-model.cc: Thorp absorption, Stojanovic noise, BFSK packet error rate,
 * and source-level calibration. Implements spec §4.4 and the margin of §8.2.
 */

#include "acoustic-link-model.h"

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace cbergr
{

namespace
{
double
DbToLinear(double db)
{
    return std::pow(10.0, db / 10.0);
}
} // namespace

AcousticLinkModel::AcousticLinkModel()
    : AcousticLinkModel(Config{})
{
}

AcousticLinkModel::AcousticLinkModel(const Config& cfg)
    : m_cfg(cfg)
{
    double psdDb = NoisePsdDb(cfg.carrierKHz, cfg.shippingActivity, cfg.windSpeedMps);
    m_noiseLevelDb = psdDb + 10.0 * std::log10(cfg.bandwidthHz);
    m_absorptionDbPerKm = ThorpDbPerKm(cfg.carrierKHz);

    // Calibrate SL: the SNR that gives calibPer at range R, then add back the losses.
    double snrAtRangeDb = SnrForPer(cfg.calibPer, cfg.calibFrameBytes);
    m_slFullDb = snrAtRangeDb + TransmissionLossDb(cfg.rangeM) + m_noiseLevelDb;
}

double
AcousticLinkModel::ThorpDbPerKm(double f)
{
    double f2 = f * f;
    return 0.11 * f2 / (1.0 + f2) + 44.0 * f2 / (4100.0 + f2) + 2.75e-4 * f2 + 0.003;
}

double
AcousticLinkModel::NoisePsdDb(double f, double s, double w)
{
    double turbulence = 17.0 - 30.0 * std::log10(f);
    double shipping = 40.0 + 20.0 * (s - 0.5) + 26.0 * std::log10(f) - 60.0 * std::log10(f + 0.03);
    double wind = 50.0 + 7.5 * std::sqrt(w) + 20.0 * std::log10(f) - 40.0 * std::log10(f + 0.4);
    double thermal = -15.0 + 20.0 * std::log10(f);
    double totalLinear = DbToLinear(turbulence) + DbToLinear(shipping) + DbToLinear(wind) +
                         DbToLinear(thermal);
    return 10.0 * std::log10(totalLinear);
}

double
AcousticLinkModel::TransmissionLossDb(double dM) const
{
    double d = std::max(1.0, dM);
    double spreadingDb = 10.0 * m_cfg.spreadingK * std::log10(d);
    double absorptionDb = m_absorptionDbPerKm * d / 1000.0;
    return spreadingDb + absorptionDb;
}

double
AcousticLinkModel::SourceLevelDb(double ptW) const
{
    double ratio = std::max(ptW, 1e-12) / m_cfg.referencePtW;
    return m_slFullDb + 10.0 * std::log10(ratio);
}

double
AcousticLinkModel::SnrDb(double slDb, double dM, double shadowDb) const
{
    return slDb - TransmissionLossDb(dM) - m_noiseLevelDb + shadowDb;
}

double
AcousticLinkModel::PerFromSnr(double snrDb, double bytes) const
{
    double ebN0 = DbToLinear(snrDb) * m_cfg.bandwidthHz / m_cfg.bitRateBps;
    double ber = 0.5 * std::exp(-ebN0 / 2.0);
    // PER = 1 - (1-BER)^(8L), written to stay accurate for tiny BER.
    return -std::expm1(8.0 * bytes * std::log1p(-ber));
}

double
AcousticLinkModel::SnrForPer(double per, double bytes) const
{
    // Invert PER = 1 - (1-BER)^(8L), then BER = 0.5 exp(-g/2) with g = Eb/N0.
    double ber = -std::expm1(std::log1p(-per) / (8.0 * bytes));
    double ebN0 = -2.0 * std::log(2.0 * ber);
    double snrLinear = ebN0 * m_cfg.bitRateBps / m_cfg.bandwidthHz;
    return 10.0 * std::log10(snrLinear);
}

double
AcousticLinkModel::MarginDb(double slDb, double dM, double bytes) const
{
    return SnrDb(slDb, dM, 0.0) - SnrReqDb(bytes);
}

} // namespace cbergr
} // namespace ns3
