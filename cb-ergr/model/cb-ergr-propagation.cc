/*
 * cb-ergr-propagation.cc: per-receiver SNR/PER decision for every frame on the channel.
 * Implements spec §4.4 (reception succeeds with probability 1 - PER).
 */

#include "cb-ergr-propagation.h"

#include "cb-ergr-params.h"
#include "ergr-headers.h"
#include "tx-power-level-tag.h"

#include "ns3/aqua-sim-header-mac.h"
#include "ns3/aqua-sim-header.h"
#include "ns3/aqua-sim-net-device.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/mobility-model.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/trace-source-accessor.h"

namespace ns3
{
namespace cbergr
{

NS_LOG_COMPONENT_DEFINE("CbErgrPropagation");
NS_OBJECT_ENSURE_REGISTERED(CbErgrPropagation);

TypeId
CbErgrPropagation::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::cbergr::CbErgrPropagation")
            .SetParent<AquaSimPropagation>()
            .AddConstructor<CbErgrPropagation>()
            .AddAttribute("RangeM",
                          "Frames are not delivered beyond this distance (m).",
                          DoubleValue(params::phy::kTxRangeM),
                          MakeDoubleAccessor(&CbErgrPropagation::m_rangeM),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("CarrierKHz",
                          "Carrier frequency (kHz).",
                          DoubleValue(params::phy::kCarrierKHz),
                          MakeDoubleAccessor(&CbErgrPropagation::m_carrierKHz),
                          MakeDoubleChecker<double>(0.1))
            .AddAttribute("BandwidthHz",
                          "Receiver noise bandwidth (Hz).",
                          DoubleValue(params::phy::kBandwidthHz),
                          MakeDoubleAccessor(&CbErgrPropagation::m_bandwidthHz),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("BitRateBps",
                          "Bit rate used in Eb/N0 (bit/s). Must match the PHY modulation.",
                          DoubleValue(params::phy::kBitRateBps),
                          MakeDoubleAccessor(&CbErgrPropagation::m_bitRateBps),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("SpreadingK",
                          "Spreading factor k (1.5 = practical).",
                          DoubleValue(params::phy::kSpreadingK),
                          MakeDoubleAccessor(&CbErgrPropagation::m_spreadingK),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("ShippingActivity",
                          "Shipping activity factor s in [0, 1].",
                          DoubleValue(params::phy::kShippingActivity),
                          MakeDoubleAccessor(&CbErgrPropagation::m_shippingActivity),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("WindSpeedMps",
                          "Wind speed for the ambient-noise model (m/s).",
                          DoubleValue(params::phy::kWindSpeedMps),
                          MakeDoubleAccessor(&CbErgrPropagation::m_windSpeedMps),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("ReferencePtW",
                          "Acoustic Pt (W) of the full power level; other levels scale from it.",
                          DoubleValue(params::phy::kReferenceAcousticPtW),
                          MakeDoubleAccessor(&CbErgrPropagation::m_referencePtW),
                          MakeDoubleChecker<double>(1e-12))
            .AddAttribute("CalibFrameBytes",
                          "Frame length (B) used to calibrate the full-power source level.",
                          DoubleValue(params::phy::kCalibFrameBytes),
                          MakeDoubleAccessor(&CbErgrPropagation::m_calibFrameBytes),
                          MakeDoubleChecker<double>(1.0))
            .AddAttribute("ExtraLossProbability",
                          "Test only: every frame is additionally lost with this probability.",
                          DoubleValue(0.0),
                          MakeDoubleAccessor(&CbErgrPropagation::m_extraLossProbability),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("Shadowing",
                          "The shadowing process (created automatically).",
                          PointerValue(),
                          MakePointerAccessor(&CbErgrPropagation::m_shadowing),
                          MakePointerChecker<GmShadowing>())
            .AddTraceSource("LinkEval",
                            "Result of the link decision for one frame and one receiver.",
                            MakeTraceSourceAccessor(&CbErgrPropagation::m_linkEval),
                            "ns3::TracedCallback<uint32_t, uint32_t, double, double, double, bool, uint32_t, uint8_t>");
    return tid;
}

CbErgrPropagation::CbErgrPropagation()
    : m_rangeM(params::phy::kTxRangeM),
      m_carrierKHz(params::phy::kCarrierKHz),
      m_bandwidthHz(params::phy::kBandwidthHz),
      m_bitRateBps(params::phy::kBitRateBps),
      m_spreadingK(params::phy::kSpreadingK),
      m_shippingActivity(params::phy::kShippingActivity),
      m_windSpeedMps(params::phy::kWindSpeedMps),
      m_referencePtW(params::phy::kReferenceAcousticPtW),
      m_calibFrameBytes(params::phy::kCalibFrameBytes),
      m_extraLossProbability(0.0)
{
    m_extraLossDraw = CreateObject<UniformRandomVariable>();
    m_shadowing = CreateObject<GmShadowing>();
    m_lossDraw = CreateObject<UniformRandomVariable>();
}

bool
CbErgrPropagation::MeasuredBeaconSnrDb(uint32_t txNode, uint32_t rxNode, double& snrDb) const
{
    auto it = m_beaconSnr.find((static_cast<uint64_t>(txNode) << 32) | rxNode);
    if (it == m_beaconSnr.end())
    {
        return false;
    }
    snrDb = it->second;
    return true;
}

int64_t
CbErgrPropagation::AssignStreams(int64_t stream)
{
    m_lossDraw->SetStream(stream);
    m_extraLossDraw->SetStream(stream + 1000000); // separate stream: default runs never draw from it
    return 1 + m_shadowing->AssignStreams(stream + 1);
}

void
CbErgrPropagation::EnsureLinkModel()
{
    if (m_link)
    {
        return;
    }
    AcousticLinkModel::Config cfg;
    cfg.carrierKHz = m_carrierKHz;
    cfg.bandwidthHz = m_bandwidthHz;
    cfg.bitRateBps = m_bitRateBps;
    cfg.spreadingK = m_spreadingK;
    cfg.shippingActivity = m_shippingActivity;
    cfg.windSpeedMps = m_windSpeedMps;
    cfg.rangeM = m_rangeM;
    cfg.calibFrameBytes = m_calibFrameBytes;
    cfg.referencePtW = m_referencePtW;
    m_link = std::make_unique<AcousticLinkModel>(cfg);
}

const AcousticLinkModel&
CbErgrPropagation::GetLinkModel()
{
    EnsureLinkModel();
    return *m_link;
}

std::vector<PktRecvUnit>*
CbErgrPropagation::ReceivedCopies(Ptr<AquaSimNetDevice> s,
                                  Ptr<Packet> p,
                                  std::vector<Ptr<AquaSimNetDevice>> dList)
{
    NS_LOG_FUNCTION(this << dList.size());
    EnsureLinkModel();

    auto* result = new std::vector<PktRecvUnit>;

    // Read the stamp (Pt) and the simulated frame size without disturbing the packet.
    Ptr<Packet> peek = p->Copy();
    AquaSimPacketStamp stamp;
    AquaSimHeader header;
    peek->RemoveHeader(stamp);
    peek->PeekHeader(header);
    const uint32_t frameBytes = header.GetSize();
    // Frame type = first byte of our own header, which sits behind the Aqua-Sim and MAC headers.
    peek->RemoveHeader(header);
    MacHeader macHeader;
    peek->RemoveHeader(macHeader);
    const uint8_t frameType = PeekFrameType(peek);
    // Source level: the tagged power level if the routing layer set one (spec §4.2), otherwise the full level.
    double sourceLevelDb = m_link->SourceLevelDb(stamp.GetPt());
    TxPowerLevelTag levelTag;
    if (p->PeekPacketTag(levelTag))
    {
        sourceLevelDb = m_link->FullSourceLevelDb() + params::phy::kSourceLevelOffsetDb[levelTag.GetLevel()];
    }

    Ptr<MobilityModel> senderMobility = s->GetNode()->GetObject<MobilityModel>();
    const uint32_t senderId = s->GetNode()->GetId();
    const Time now = Simulator::Now();

    for (const Ptr<AquaSimNetDevice>& receiver : dList)
    {
        if (receiver == s)
        {
            continue;
        }
        Ptr<MobilityModel> receiverMobility = receiver->GetNode()->GetObject<MobilityModel>();
        const double distanceM = senderMobility->GetDistanceFrom(receiverMobility);
        if (distanceM > m_rangeM)
        {
            continue; // OUT_OF_RANGE: no copy, no energy, no interference
        }
        const uint32_t receiverId = receiver->GetNode()->GetId();

        const double shadowDb = m_shadowing->GetShadowDb(senderId, receiverId, now);
        const double snrDb = m_link->SnrDb(sourceLevelDb, distanceM, shadowDb);
        const double per = m_link->PerFromSnr(snrDb, frameBytes);
        bool decodable = m_lossDraw->GetValue() >= per;
        if (decodable && m_extraLossProbability > 0.0 && m_extraLossDraw->GetValue() < m_extraLossProbability)
        {
            decodable = false; // forced loss (tests)
        }
        if (decodable && m_filter && m_filter(senderId, receiverId, frameType))
        {
            decodable = false; // deterministic loss chosen by a test
        }
        if (decodable && frameType == static_cast<uint8_t>(FrameType::Beacon))
        {
            m_beaconSnr[(static_cast<uint64_t>(senderId) << 32) | receiverId] = snrDb;
        }
        m_linkEval(senderId, receiverId, distanceM, snrDb, per, decodable, frameBytes, frameType);

        PktRecvUnit unit;
        unit.recver = receiver;
        unit.pDelay = PDelay(senderMobility, receiverMobility);
        unit.pR = decodable ? 1.0 : 0.0; // see the file comment: PHY RXThresh = 0.5
        result->push_back(unit);
    }
    return result;
}

} // namespace cbergr
} // namespace ns3
