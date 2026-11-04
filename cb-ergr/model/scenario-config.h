/*
 * scenario-config.h: layered scenario settings (kickoff rule 11).
 *
 *   cb-ergr-params.h defaults  <  --ConfigFile=configs/x.txt  <  command line
 *
 * Each setting is registered once with its name, a pointer to the variable and a help text. The same
 * list is used to read a config file, to add command-line options and to write params_used.txt, so a
 * value can never be present in one place and missing in another.
 * Config file format: one "Name = value" per line, '#' starts a comment.
 */

#ifndef CB_ERGR_SCENARIO_CONFIG_H
#define CB_ERGR_SCENARIO_CONFIG_H

#include "ns3/command-line.h"

#include <ostream>
#include <string>
#include <vector>

namespace ns3
{
namespace cbergr
{

class ScenarioConfig
{
  public:
    void Add(const std::string& name, double* value, const std::string& help);
    void Add(const std::string& name, int* value, const std::string& help);
    void Add(const std::string& name, bool* value, const std::string& help);
    void Add(const std::string& name, std::string* value, const std::string& help);

    /// Apply a config file. Returns false (and fills 'error') on an unknown name or bad value.
    bool LoadFile(const std::string& path, std::string& error);

    /// Make every setting available as --Name=value on the command line.
    void RegisterOnCommandLine(CommandLine& cmd);

    /// Write "Name = value" for every setting (params_used.txt).
    void Dump(std::ostream& os) const;

    /// "Name=value;Name=value" for the summary.csv params column; names in 'skip' are left out.
    std::string OneLine(const std::vector<std::string>& skip = {}) const;

  private:
    enum class Kind
    {
        Double,
        Int,
        Bool,
        String
    };
    struct Entry
    {
        std::string name;
        Kind kind;
        void* ptr;
        std::string help;
    };
    bool Set(Entry& e, const std::string& text, std::string& error);
    std::string ValueOf(const Entry& e) const;

    std::vector<Entry> m_entries;
};

} // namespace cbergr
} // namespace ns3

#endif /* CB_ERGR_SCENARIO_CONFIG_H */
