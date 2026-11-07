/*
 * cb-ergr-scenario.cc: command-line front end of the experiment driver (kickoff §4, CODE-GUIDE §5).
 *   ./ns3 run "cb-ergr-scenario --Selector=RoundRobin --N=100 --SimTimeS=2000 --OutDir=results/smoke"
 * Every run writes to a NEW folder <OutDir>_run<N> (results/smoke_run1, _run2, ...; --NumberedRuns=false or CB_ERGR_NUMBERED_RUNS=0 for a fixed path)
 * and starts with run_summary.txt, the short readable file (method, conditions, PDR, energy per packet, ...).
 * Settings: header defaults (cb-ergr-params.h) < --ConfigFile=configs/x.txt < command line.
 * The scenario itself is built in helper/scenario-runner.cc so that tests can run it too.
 */

#include "ns3/core-module.h"
#include "ns3/run-summary.h"
#include "ns3/scenario-runner.h"

#include <cstdlib>
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
    if (const char* env = std::getenv("CB_ERGR_NUMBERED_RUNS")) // sweep scripts need fixed paths: they set this to 0
    {
        settings.numberedRuns = (std::string(env) != "0");
    }

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

    if (settings.numberedRuns)
    {
        // never overwrite an earlier run: results go to <OutDir>_run<N>
        settings.outDir = NextRunDir(settings.outDir, settings.runNumber);
    }
    ScenarioResult r = RunScenario(settings, config);
    std::cout << "run finished: selector=" << settings.selector << " N=" << settings.numNodes
              << " sim=" << settings.simTimeS << " s, wall=" << r.wallSeconds << " s, PDR=" << r.pdr
              << " created=" << r.packetsCreated << " delivered=" << r.packetsDelivered
              << " rxEnergyShare=" << r.rxEnergyShare << " FND=" << r.firstNodeDeathS << std::endl;
    std::cout << "results: " << settings.outDir << "  (read " << settings.outDir << "/run_summary.txt first)" << std::endl;
    return 0;
}
