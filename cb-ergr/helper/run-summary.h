/*
 * run-summary.h: the short, human-readable file of one run (run_summary.txt) and the numbered output folder.
 *
 * run_summary.txt holds only what is needed to understand a run: which method, under which conditions, with which parameters, and the
 * headline results (PDR, packets, delay, energy per delivered packet, power, lifetime, drops, hop success, control traffic). Everything
 * else stays in the detailed CSV files of the same folder. Reporting only: nothing here is read by a protocol.
 */

#ifndef CB_ERGR_RUN_SUMMARY_H
#define CB_ERGR_RUN_SUMMARY_H

#include "scenario-runner.h"

#include <string>

namespace ns3
{
namespace cbergr
{

/**
 * Numbered output folders. Returns "<base>_run<N>" where N is one more than the highest existing "<base>_run<M>" (1 for the first
 * run) and creates it, so a repeated run never overwrites an earlier one. 'base' is the OutDir the user gave (a trailing '/' is
 * ignored). 'number' receives N.
 */
std::string NextRunDir(const std::string& base, int& number);

/// Write <outDir>/run_summary.txt from the settings and the files the run has written (summary.csv, loss_budget.csv, heads.csv, resources.csv).
void WriteRunSummary(const ScenarioSettings& settings, const ScenarioConfig& config, const ScenarioResult& result);

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_RUN_SUMMARY_H */
