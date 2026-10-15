/*
 * CB-ERGR v5, milestone M0 smoke test.
 *
 * Builds a 2-node static Aqua-Sim NG network (broadcast MAC, dummy routing)
 * from inside the cb-ergr module and runs it for a few seconds. It only
 * checks that the module links against Aqua-Sim NG; it is not an experiment.
 */

#include "ns3/applications-module.h"
#include "ns3/aqua-sim-ng-module.h"
#include "ns3/cb-ergr-config.h"
#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"

#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CbErgrSmoke");

int
main(int argc, char* argv[])
{
    double simStop = 20;
    CommandLine cmd(__FILE__);
    cmd.AddValue("simStop", "Simulated time (s)", simStop);
    cmd.Parse(argc, argv);

    NodeContainer nodes;
    nodes.Create(2);
    PacketSocketHelper socketHelper;
    socketHelper.Install(nodes);

    AquaSimChannelHelper channel = AquaSimChannelHelper::Default();
    AquaSimHelper asHelper = AquaSimHelper::Default();
    asHelper.SetChannel(channel.Create());
    asHelper.SetMac("ns3::AquaSimBroadcastMac");
    asHelper.SetRouting("ns3::AquaSimRoutingDummy");

    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    NetDeviceContainer devices;
    double x = 0;
    for (auto it = nodes.Begin(); it != nodes.End(); ++it, x += 500)
    {
        Ptr<AquaSimNetDevice> dev = CreateObject<AquaSimNetDevice>();
        pos->Add(Vector(x, 0, 0));
        devices.Add(asHelper.Create(*it, dev));
    }
    MobilityHelper mobility;
    mobility.SetPositionAllocator(pos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    PacketSocketAddress socket;
    socket.SetAllDevices();
    socket.SetPhysicalAddress(devices.Get(1)->GetAddress());
    socket.SetProtocol(0);

    OnOffHelper app("ns3::PacketSocketFactory", Address(socket));
    app.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    app.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    app.SetAttribute("DataRate", DataRateValue(DataRate(80)));
    app.SetAttribute("PacketSize", UintegerValue(40));
    ApplicationContainer apps = app.Install(nodes.Get(0));
    apps.Start(Seconds(0.5));
    apps.Stop(Seconds(simStop));

    Ptr<Socket> sink =
        Socket::CreateSocket(nodes.Get(1), TypeId::LookupByName("ns3::PacketSocketFactory"));
    sink->Bind(socket);

    Simulator::Stop(Seconds(simStop + 1));
    Simulator::Run();
    std::cout << cbergr::GetSpecVersion() << ": smoke run finished at t="
              << Simulator::Now().GetSeconds() << " s" << std::endl;
    Simulator::Destroy();
    return 0;
}
