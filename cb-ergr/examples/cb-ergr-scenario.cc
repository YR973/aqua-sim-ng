/*
 * cb-ergr-scenario.cc: command-line front end of the experiment driver (kickoff §4, CODE-GUIDE §5).
 *   ./ns3 run "cb-ergr-scenario --Selector=RoundRobin --N=100 --SimTimeS=2000 --OutDir=results/smoke"
 * Settings: header defaults (cb-ergr-params.h) < --ConfigFile=configs/x.txt < command line.
 * The scenario itself is built in helper/scenario-runner.cc so that tests can run it too.
 */

#include "ns3/core-module.h"
#include "ns3/scenario-runner.h"

#include <iostream>

using namespace ns3;
using namespace ns3::cbergr;

namespace
{
// Parse --ConfigFile=... from argv before the real parse, so config values sit below command-line values.
std::string
FindConfigFile(int argc, char* argv[])
{
    const std::string key = "--ConfigFile=";
    for (int i = 1; i < argc; ++i)
    {
        std::string arg(argv[i]);
        if (arg.rfind(key, 0) == 0)
        {
            return arg.substr(key.size());
        }
    }
    return "";
}
} // namespace

int
main(int argc, char* argv[])
{
    ScenarioSettings settings;
    ScenarioConfig config;
    RegisterSettings(config, settings);

    std::string configFile = FindConfigFile(argc, argv);
    if (!configFile.empty())
    {
        std::string error;
        if (!config.LoadFile(configFile, error))
        {
            std::cerr << "config error: " << error << std::endl;
            return 1;
        }
    }
    CommandLine cmd(__FILE__);
    std::string ignoredConfig;
    cmd.AddValue("ConfigFile", "Settings file (key = value lines), below the command line", ignoredConfig);
    config.RegisterOnCommandLine(cmd);
    cmd.Parse(argc, argv);

    ScenarioResult r = RunScenario(settings, config);
    std::cout << "run finished: selector=" << settings.selector << " N=" << settings.numNodes
              << " sim=" << settings.simTimeS << " s, wall=" << r.wallSeconds << " s, PDR=" << r.pdr
              << " created=" << r.packetsCreated << " delivered=" << r.packetsDelivered
              << " rxEnergyShare=" << r.rxEnergyShare << " FND=" << r.firstNodeDeathS << std::endl;
    return 0;
}
