/*
 * acoustic-link-model.h: underwater link budget and packet error rate (spec §4.4).
 * Plain C++, no ns-3 dependency, so it can be unit-tested and reused by the oracles.
 *
 *   SNR = SL - TL(d) - NL + shadow          (all in dB)
 *   TL(d)  = 10 k log10(d) + alpha(f) d/1000  (k = 1.5 spreading, alpha = Thorp, dB/km)
 *   NL     = Stojanovic noise (turbulence, shipping, wind, thermal) + 10 log10(B)
 *   BFSK   : Eb/N0 = SNR * B / Rb ; BER = 0.5 exp(-Eb/N0 / 2) ; PER = 1 - (1-BER)^(8 L)
 *
 * The full-power source level SL is *calibrated* so that a 150 B frame has
 * PER = 0.5 at R = 1500 m with mean shadowing (the pilot's calibrate_sl).
 */

#ifndef CB_ERGR_ACOUSTIC_LINK_MODEL_H
#define CB_ERGR_ACOUSTIC_LINK_MODEL_H

#include "cb-ergr-params.h"

namespace ns3
{
namespace cbergr
{

class AcousticLinkModel
{
  public:
    struct Config
    {
        double carrierKHz = params::phy::kCarrierKHz;
        double bandwidthHz = params::phy::kBandwidthHz;
        double bitRateBps = params::phy::kBitRateBps;
        double spreadingK = params::phy::kSpreadingK;
        double shippingActivity = params::phy::kShippingActivity; // s in [0, 1]
        double windSpeedMps = params::phy::kWindSpeedMps;
        double rangeM = params::phy::kTxRangeM;                   // R used for calibration
        double calibPer = params::phy::kCalibPer;
        double calibFrameBytes = params::phy::kCalibFrameBytes;
        double snrReqPer = params::phy::kSnrReqPer;               // PER defining SnrReqDb
        double referencePtW = params::phy::kReferenceAcousticPtW; // Pt of the full level
    };

    AcousticLinkModel();                          // all defaults from cb-ergr-params.h
    explicit AcousticLinkModel(const Config& cfg);

    /// Thorp absorption in dB/km at carrier frequency f (kHz).
    static double ThorpDbPerKm(double fKHz);

    /// Stojanovic ambient noise power spectral density in dB re 1 uPa^2/Hz at f (kHz).
    static double NoisePsdDb(double fKHz, double shippingActivity, double windSpeedMps);

    /// Transmission loss in dB at distance dM (metres, clamped to >= 1 m).
    double TransmissionLossDb(double dM) const;

    /// Noise level over the receiver bandwidth in dB: NoisePsd + 10 log10(B).
    double NoiseLevelDb() const { return m_noiseLevelDb; }

    /// Calibrated full-power source level in dB.
    double FullSourceLevelDb() const { return m_slFullDb; }

    /// Source level for a frame sent with acoustic power ptW (scales the full level in dB).
    double SourceLevelDb(double ptW) const;

    /// SNR in dB for a given source level, distance and shadowing offset (dB).
    double SnrDb(double slDb, double dM, double shadowDb) const;

    /// Packet error rate of a frame of 'bytes' bytes at SNR 'snrDb' (non-coherent BFSK).
    double PerFromSnr(double snrDb, double bytes) const;

    /// SNR (dB) at which a frame of 'bytes' bytes has the given PER.
    double SnrForPer(double per, double bytes) const;

    /// SNR needed for data PER = snrReqPer (spec §8.2 SnrReqDb) for a frame of 'bytes' bytes.
    double SnrReqDb(double bytes) const { return SnrForPer(m_cfg.snrReqPer, bytes); }

    /// Predicted margin (spec §8.2): SL - TL(d) - NL - SnrReqDb, without shadowing.
    double MarginDb(double slDb, double dM, double bytes) const;

    const Config& GetConfig() const { return m_cfg; }

  private:
    Config m_cfg;
    double m_noiseLevelDb;
    double m_absorptionDbPerKm;
    double m_slFullDb;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_ACOUSTIC_LINK_MODEL_H */
