/*
 * run-summary.cc: see run-summary.h.
 */

#include "run-summary.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace ns3
{
namespace cbergr
{

namespace
{
using Row = std::map<std::string, std::string>;

std::vector<std::string>
SplitCommas(const std::string& line, size_t maxFields)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (out.size() + 1 < maxFields)
    {
        size_t comma = line.find(',', start);
        if (comma == std::string::npos)
        {
            break;
        }
        out.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    out.push_back(line.substr(start)); // the last field keeps any commas (summary.csv: params)
    return out;
}

Row
ReadOneRowCsv(const std::string& path)
{
    Row row;
    std::ifstream in(path);
    std::string header, values;
    if (!std::getline(in, header) || !std::getline(in, values))
    {
        return row;
    }
    auto names = SplitCommas(header, 1000);
    auto vals = SplitCommas(values, names.size());
    for (size_t i = 0; i < names.size() && i < vals.size(); ++i)
    {
        row[names[i]] = vals[i];
    }
    return row;
}

/// key,value files; with group,key,value (loss_budget.csv) the key is "group/key".
std::map<std::string, double>
ReadKeyValue(const std::string& path)
{
    std::map<std::string, double> m;
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    const bool grouped = line.rfind("group,", 0) == 0;
    while (std::getline(in, line))
    {
        auto f = SplitCommas(line, grouped ? 3 : 2);
        if (f.size() < (grouped ? 3u : 2u))
        {
            continue;
        }
        try
        {
            m[grouped ? f[0] + "/" + f[1] : f[0]] = std::stod(f.back());
        }
        catch (...)
        {
        }
    }
    return m;
}

double
Num(const Row& r, const std::string& k, double dflt = -1.0)
{
    auto it = r.find(k);
    if (it == r.end() || it->second.empty())
    {
        return dflt;
    }
    try
    {
        return std::stod(it->second);
    }
    catch (...)
    {
        return dflt;
    }
}

double
Get(const std::map<std::string, double>& m, const std::string& k)
{
    auto it = m.find(k);
    return it == m.end() ? 0.0 : it->second;
}

std::string
Fix(double v, int digits)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(digits) << v;
    return os.str();
}

std::string
Time(double s)
{
    return s < 0 ? "none (no node died)" : Fix(s, 0) + " s";
}

void
Line(std::ostream& os, const std::string& key, const std::string& value, const std::string& unit = "")
{
    os << "  " << std::left << std::setw(46) << key << value << (unit.empty() ? "" : " " + unit) << '\n';
}
} // namespace

std::string
NextRunDir(const std::string& base, int& number)
{
    namespace fs = std::filesystem;
    std::string b = base;
    while (b.size() > 1 && (b.back() == '/' || b.back() == '\\'))
    {
        b.pop_back();
    }
    const fs::path basePath(b);
    const fs::path parent = basePath.has_parent_path() ? basePath.parent_path() : fs::path(".");
    const std::string prefix = basePath.filename().string() + "_run";
    int highest = 0;
    std::error_code ec;
    if (fs::exists(parent, ec))
    {
        for (const auto& entry : fs::directory_iterator(parent, ec))
        {
            const std::string name = entry.path().filename().string();
            if (name.rfind(prefix, 0) != 0)
            {
                continue;
            }
            const std::string tail = name.substr(prefix.size());
            if (!tail.empty() && std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; }))
            {
                highest = std::max(highest, std::stoi(tail));
            }
        }
    }
    // create_directories returns false if the folder already exists (two runs started together): take the next number
    for (int n = highest + 1;; ++n)
    {
        const std::string dir = b + "_run" + std::to_string(n);
        if (fs::create_directories(dir, ec))
        {
            number = n;
            return dir;
        }
    }
}

void
WriteRunSummary(const ScenarioSettings& s, const ScenarioConfig& config, const ScenarioResult& result)
{
    const Row r = ReadOneRowCsv(s.outDir + "/summary.csv");
    if (r.empty())
    {
        return;
    }
    const auto lb = ReadKeyValue(s.outDir + "/loss_budget.csv");
    const auto heads = ReadKeyValue(s.outDir + "/heads.csv");
    std::ofstream os(s.outDir + "/run_summary.txt");
    os << "CB-ERGR RUN SUMMARY\n===================\n";
    std::time_t now = std::time(nullptr);
    char stamp[64];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&now));
    os << "\nRUN\n";
    if (s.runNumber > 0)
    {
        Line(os, "run number", std::to_string(s.runNumber), "(folder " + s.outDir + ")");
    }
    else
    {
        Line(os, "folder", s.outDir);
    }
    Line(os, "finished", stamp);
    Line(os, "wall-clock time", Fix(result.wallSeconds, 1), "s");
    Line(os, "random seed / run", std::to_string(s.seed) + " / " + std::to_string(s.run));

    const bool p0 = s.protocol == "P0";
    os << "\nMETHOD\n";
    Line(os, "protocol", p0 ? "ERGR-EMHC as published (P0)" : "ERGR-EMHC-g");
    Line(os, "forwarder selection", s.selector + (s.selectorAttributes.empty() ? "" : " (" + s.selectorAttributes + ")"));
    if (!s.flags.empty())
    {
        Line(os, "flags", s.flags);
    }
    Line(os, "MAC", s.mac);
    if (!p0)
    {
        Line(os, "heads per cell (k)", std::to_string(s.headsPerCell));
        Line(os, "neighbour rule", s.neighbourRule + (s.neighbourRule == "Adjacent" ? (s.faceOnly ? " (6 face cells)" : " (26 adjacent cells)") : " (32 cells)"));
        Line(os, "lean beacons / channel-aware beacons", std::string(s.leanBeacons ? "on" : "off") + " / " + (s.channelAwareBeacons ? "on" : "off"));
        Line(os, "label rule", s.labelRule);
        Line(os, "quorum dead-head detection", s.quorumDetection ? "on" : "off");
    }

    os << "\nCONDITIONS\n";
    Line(os, "nodes / sinks", std::to_string(s.numNodes) + " / " + std::to_string(s.numSinks));
    Line(os, "traffic sources", s.numSources > 0 ? std::to_string(s.numSources) : "all nodes");
    Line(os, "packet rate per source", Fix(s.pktRateHz, 4), "packets/s");
    Line(os, "payload", std::to_string(s.payloadBytes), "bytes");
    Line(os, "box / cell side", Fix(s.boxSizeM, 0) + " m / " + Fix(s.cellSizeM, 1) + " m");
    Line(os, "radio range / bit rate / preamble", Fix(s.rangeM, 0) + " m / " + Fix(s.bitRateBps, 0) + " bit/s / " + Fix(s.preambleS, 2) + " s");
    Line(os, "channel", s.idealLinks ? "ideal links (every frame inside the range decodes; collisions still apply)"
                                     : "calibrated (Thorp/Urick, shadowing sigma " + Fix(s.shadowSigmaDb, 1) + " dB, tau " + Fix(s.shadowTauS, 0) + " s)");
    if (s.noiseBandOffsetDb != 0.0)
    {
        Line(os, "extra noise band", Fix(s.noiseBandOffsetDb, 1) + " dB, x " + Fix(s.noiseBandXMinM, 0) + ".." + Fix(s.noiseBandXMaxM, 0) +
                                         " m, from " + Fix(s.noiseBandStartS, 0) + " s" + (s.noiseBandEndS >= 0 ? " to " + Fix(s.noiseBandEndS, 0) + " s" : ""));
    }
    Line(os, "ocean current speed", Fix(s.currentSpeed, 2), "m/s");
    Line(os, "position error / velocity error", Fix(s.locErrM, 1) + " m / " + Fix(s.velErrMps, 2) + " m/s");
    Line(os, "battery per node", Fix(s.initialEnergyJ, 0), "J");
    Line(os, "power Tx (full) / Rx / idle", Fix(s.txPowerFullW, 2) + " / " + Fix(s.rxPowerW, 2) + " / " + Fix(s.idlePowerW, 3), "W");
    Line(os, "simulated time / warm-up / data start", Fix(s.simTimeS, 0) + " s / " + Fix(s.warmupS, 0) + " s / " + Fix(s.dataStartS, 0) + " s");
    if (s.killHeadsAtS >= 0)
    {
        Line(os, "head failures injected", std::to_string(s.killHeadsCells) + " cells at " + Fix(s.killHeadsAtS, 0) + " s");
    }
    os << "  (all results below count only the time after the warm-up)\n";

    const double created = Num(r, "packets_created", 0), delivered = Num(r, "packets_delivered", 0);
    const double window = std::max(1.0, Num(r, "sim_time_s", s.simTimeS) - Num(r, "warmup_s", s.warmupS));
    const double eTx = Num(r, "energy_tx_j", 0), eRx = Num(r, "energy_rx_j", 0), eIdle = Num(r, "energy_idle_j", 0);
    const double eTot = eTx + eRx + eIdle;

    os << "\nRESULTS: DELIVERY\n";
    Line(os, "packets created", Fix(created, 0));
    Line(os, "packets delivered", Fix(delivered, 0));
    Line(os, "packet delivery ratio (PDR)", Fix(Num(r, "pdr", 0), 4));
    Line(os, "packets dropped / still in flight", Fix(Num(r, "packets_dropped", 0), 0) + " / " + Fix(Num(r, "packets_in_flight", 0), 0));
    Line(os, "end-to-end delay mean / 95th pct", Fix(Num(r, "delay_mean_s", 0), 2) + " / " + Fix(Num(r, "delay_p95_s", 0), 2), "s");
    Line(os, "data transmissions per delivered pkt", Fix(Num(r, "data_tx_per_delivered", 0), 2), "(retransmissions " + Fix(Num(r, "retx_per_delivered", 0), 2) + ")");
    Line(os, "hop success (ground truth)", Fix(Num(r, "hop_success_truth", 0), 3), "of " + Fix(Get(lb, "hop/attempts"), 0) + " data hop attempts");

    os << "\nRESULTS: ENERGY AND POWER\n";
    Line(os, "energy per delivered packet", Fix(Num(r, "energy_per_delivered_j", 0), 2), "J");
    Line(os, "  of which transmit / receive / idle", Fix(delivered > 0 ? eTx / delivered : 0, 2) + " / " + Fix(delivered > 0 ? eRx / delivered : 0, 2) + " / " +
                                                      Fix(delivered > 0 ? eIdle / delivered : 0, 2), "J");
    Line(os, "total energy used (all nodes)", Fix(eTot, 0), "J");
    Line(os, "average power per node", Fix(eTot / (std::max(1, s.numNodes) * window), 4), "W");
    Line(os, "receive share of the energy", Fix(Num(r, "rx_energy_share", 0), 3));
    Line(os, "first node dead", Time(Num(r, "fnd_s")));
    Line(os, "half of the nodes dead", Time(Num(r, "hnd_s")));

    os << "\nRESULTS: CONTROL TRAFFIC\n";
    Line(os, "ACK frames per delivered packet", Fix(Num(r, "ack_frames_per_delivered", 0), 2));
    Line(os, "control bytes per delivered packet", Fix(Num(r, "ctrl_bytes_total_per_delivered", 0), 0), "B");
    Line(os, "  beacon / ACK / election / handover", Fix(Num(r, "ctrl_bytes_beacon_per_delivered", 0), 0) + " / " + Fix(Num(r, "ctrl_bytes_ack_per_delivered", 0), 0) + " / " +
                                                      Fix(Num(r, "ctrl_bytes_election_per_delivered", 0), 0) + " / " + Fix(Num(r, "ctrl_bytes_handover_per_delivered", 0), 0), "B");
    Line(os, "airtime share data / ACK / beacon / election", Fix(100 * Get(lb, "airtime_share/data"), 1) + " / " + Fix(100 * Get(lb, "airtime_share/ack"), 1) + " / " +
                                                              Fix(100 * Get(lb, "airtime_share/beacon"), 1) + " / " + Fix(100 * Get(lb, "airtime_share/election"), 1), "%");
    Line(os, "channel load (Erlang)", Fix(Get(lb, "airtime_erlang/total"), 3));

    os << "\nRESULTS: WHERE PACKETS AND HOPS ARE LOST\n";
    const char* drops[][2] = {{"drop_max_tx_reached", "dropped: MAX_TX reached"},      {"drop_no_eligible_forwarder", "dropped: no eligible forwarder / void"},
                              {"drop_hop_budget_exhausted", "dropped: hop budget exhausted"}, {"drop_ttl_expired", "dropped: TTL expired (loss unseen)"},
                              {"drop_queue_full", "dropped: queue full"},              {"drop_node_died_holding", "dropped: node died holding it"}};
    for (const auto& d : drops)
    {
        Line(os, d[1], Fix(Num(r, d[0], 0), 0), "packets");
    }
    const double attempts = Get(lb, "hop/attempts"), failed = attempts - Get(lb, "hop/delivered");
    if (failed > 0)
    {
        const char* causes[][2] = {{"hop/per_loss", "failed hops: bad link (PER)"},         {"hop/collision_with_data", "failed hops: DATA-DATA collision"},
                                   {"hop/collision_with_beacon_hello", "failed hops: beacon/Hello collision"}, {"hop/collision_with_ack_nack", "failed hops: ACK collision"},
                                   {"hop/half_duplex", "failed hops: half-duplex"},         {"hop/receiver_dead", "failed hops: receiver dead"},
                                   {"hop/out_of_range", "failed hops: out of range"},       {"hop/unexplained_phy_busy", "failed hops: unexplained"},
                                   {"hop/never_on_air", "failed hops: never on air"}};
        for (const auto& c : causes)
        {
            Line(os, c[1], Fix(100.0 * Get(lb, c[0]) / failed, 1), "% of failed hops");
        }
    }

    if (!p0 && !heads.empty())
    {
        os << "\nRESULTS: HEADS\n";
        Line(os, "occupied cells with a head (time avg)", Fix(100 * Get(heads, "occupied_with_head_share"), 1), "%");
        Line(os, "heads per occupied cell", Fix(Get(heads, "heads_per_occupied_cell"), 2));
        Line(os, "elections after warm-up", Fix(Get(heads, "elections_after_warmup"), 0));
        Line(os, "eligible heads per decision (mean)", Fix(Get(heads, "candidates_mean"), 2), "(void decisions " + Fix(100 * Get(heads, "share_void"), 1) + " %)");
    }
    if (Num(r, "regret_mean") >= 0 && Num(r, "decisions_scored", 0) > 0)
    {
        Line(os, "mean regret vs per-hop oracle", Fix(Num(r, "regret_mean", 0), 4));
    }

    os << "\nOTHER FILES IN THIS FOLDER\n";
    os << "  summary.csv (all headline numbers, one row), params_used.txt (every setting), loss_budget.csv, heads.csv, packets.csv,\n"
          "  regret_by_time.csv, cost.csv, resources.csv, and the optional logs you switched on (nodes, links, events, decisions, attempts).\n";
    (void)config;
}

} // namespace cbergr
} // namespace ns3
