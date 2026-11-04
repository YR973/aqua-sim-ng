/*
 * link-per-curve.cc: PER-vs-distance check of the SNR-based channel (milestone D2 acceptance).
 *
 * For each distance, two static nodes exchange N 150 B frames through the real Aqua-Sim NG
 * stack (broadcast MAC, SNR propagation, collisions, half-duplex). The CSV compares the
 * measured frame error rate with the analytic value from AcousticLinkModel (averaged over
 * the shadowing distribution when sigma > 0). Frames are spaced far apart so they are
 * independent and never collide.
 *
 * Output columns: distance_m, sent, received, per_measured, per_analytic, snr_mean_db, margin_db
 */

#include "ns3/aqua-sim-ng-module.h"
#include "ns3/cb-ergr-helper.h"
#include "ns3/cb-ergr-params.h"
#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/packet-socket-helper.h"
#include "ns3/packet-socket-address.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace ns3;
using namespace ns3::cbergr;

namespace
{
uint32_t g_received = 0;

// PHY "Rx" trace: a frame survived the channel and collision checks and was handed to the MAC.
void
OnPhyRx(Ptr<Packet> packet, double noise)
{
    ++g_received;
}

void
SendOne(Ptr<Socket> socket, uint32_t bytes)
{
    socket->Send(Create<Packet>(bytes));
}

// Mean of (1 - PER) over shadowing ~ N(0, sigma), by a simple fixed quadrature.
double
AnalyticSuccess(const AcousticLinkModel& link, double slDb, double d, double sigmaDb, double bytes)
{
    if (sigmaDb <= 0.0)
    {
        return 1.0 - link.PerFromSnr(link.SnrDb(slDb, d, 0.0), bytes);
    }
    double sum = 0.0;
    double weightSum = 0.0;
    for (double z = -5.0; z <= 5.0; z += 0.05)
    {
        double w = std::exp(-0.5 * z * z);
        sum += w * (1.0 - link.PerFromSnr(link.SnrDb(slDb, d, sigmaDb * z), bytes));
        weightSum += w;
    }
    return sum / weightSum;
}
} // namespace

int
main(int argc, char* argv[])
{
    uint32_t packets = 400;
    double spacingS = 600.0; // >> shadowing tau, so frames see independent shadowing
    double sigmaDb = params::channel::kShadowSigmaDb;
    double tauS = params::channel::kShadowTauS;
    uint32_t frameBytes = static_cast<uint32_t>(params::phy::kCalibFrameBytes);
    std::string distances = "250,500,750,1000,1250,1500";
    std::string outFile = "link-per-curve.csv";
    uint32_t run = 1;

    CommandLine cmd(__FILE__);
    cmd.AddValue("packets", "Frames per distance", packets);
    cmd.AddValue("spacingS", "Seconds between frames", spacingS);
    cmd.AddValue("ShadowSigmaDb", "Shadowing std (dB)", sigmaDb);
    cmd.AddValue("ShadowTauS", "Shadowing correlation time (s)", tauS);
    cmd.AddValue("frameBytes", "Frame size (B)", frameBytes);
    cmd.AddValue("distances", "Comma-separated distances (m)", distances);
    cmd.AddValue("out", "Output CSV", outFile);
    cmd.AddValue("RngRun", "Run number", run);
    cmd.Parse(argc, argv);
    RngSeedManager::SetRun(run);

    std::vector<double> distList;
    std::stringstream ss(distances);
    for (std::string tok; std::getline(ss, tok, ',');)
    {
        distList.push_back(std::stod(tok));
    }

    std::ofstream csv(outFile);
    csv << "distance_m,sent,received,per_measured,per_analytic,snr_mean_db,margin_db\n";

    for (double distanceM : distList)
    {
        g_received = 0;
        NodeContainer nodes;
        nodes.Create(2);
        PacketSocketHelper socketHelper;
        socketHelper.Install(nodes);

        Ptr<CbErgrPropagation> propagation;
        Ptr<AquaSimChannel> channel = CbErgrHelper::CreateChannel(propagation, sigmaDb, tauS);
        propagation->AssignStreams(100);

        AquaSimHelper asHelper = AquaSimHelper::Default();
        asHelper.SetChannel(channel);
        asHelper.SetMac("ns3::AquaSimBroadcastMac");
        asHelper.SetRouting("ns3::AquaSimRoutingDummy");
        CbErgrHelper::ConfigureHelper(asHelper,
                                      params::phy::kPreambleS,
                                      1e9, // effectively unlimited energy for a link test
                                      params::energy::kTxPowerFullW,
                                      params::energy::kRxPowerW,
                                      params::energy::kIdlePowerW);

        Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
        pos->Add(Vector(0, 0, 0));
        pos->Add(Vector(distanceM, 0, 0));
        NetDeviceContainer devices;
        for (uint32_t i = 0; i < 2; ++i)
        {
            Ptr<AquaSimNetDevice> dev = CreateObject<AquaSimNetDevice>();
            devices.Add(asHelper.Create(nodes.Get(i), dev));
            CbErgrHelper::ConfigureDevice(dev, params::phy::kBitRateBps);
        }
        MobilityHelper mobility;
        mobility.SetPositionAllocator(pos);
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(nodes);

        PacketSocketAddress addr;
        addr.SetAllDevices();
        addr.SetPhysicalAddress(devices.Get(1)->GetAddress());
        addr.SetProtocol(0);

        Ptr<Socket> tx = Socket::CreateSocket(nodes.Get(0), TypeId::LookupByName("ns3::PacketSocketFactory"));
        tx->Bind();      // PacketSocket must be bound before it can connect
        tx->Connect(addr);
        Ptr<AquaSimNetDevice> rxDevice = DynamicCast<AquaSimNetDevice>(devices.Get(1));
        rxDevice->GetPhy()->TraceConnectWithoutContext("Rx", MakeCallback(&OnPhyRx));

        for (uint32_t k = 0; k < packets; ++k)
        {
            Simulator::Schedule(Seconds(1.0 + k * spacingS), &SendOne, tx, frameBytes);
        }
        Simulator::Stop(Seconds(2.0 + packets * spacingS));
        Simulator::Run();

        const AcousticLinkModel& link = propagation->GetLinkModel();
        double sl = link.FullSourceLevelDb();
        double analyticPer = 1.0 - AnalyticSuccess(link, sl, distanceM, sigmaDb, frameBytes);
        double measuredPer = 1.0 - static_cast<double>(g_received) / packets;
        csv << distanceM << ',' << packets << ',' << g_received << ',' << measuredPer << ','
            << analyticPer << ',' << link.SnrDb(sl, distanceM, 0.0) << ','
            << link.MarginDb(sl, distanceM, frameBytes) << '\n';
        std::cout << "d=" << distanceM << " m  measured PER=" << measuredPer
                  << "  analytic PER=" << analyticPer << std::endl;
        Simulator::Destroy();
    }
    return 0;
}
