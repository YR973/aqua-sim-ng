/*
 * scenario-config.cc: config-file parsing, command-line registration and dumping (kickoff rule 11).
 */

#include "scenario-config.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace ns3
{
namespace cbergr
{

namespace
{
std::string
Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
} // namespace

void
ScenarioConfig::Add(const std::string& name, double* value, const std::string& help)
{
    m_entries.push_back({name, Kind::Double, value, help});
}

void
ScenarioConfig::Add(const std::string& name, int* value, const std::string& help)
{
    m_entries.push_back({name, Kind::Int, value, help});
}

void
ScenarioConfig::Add(const std::string& name, bool* value, const std::string& help)
{
    m_entries.push_back({name, Kind::Bool, value, help});
}

void
ScenarioConfig::Add(const std::string& name, std::string* value, const std::string& help)
{
    m_entries.push_back({name, Kind::String, value, help});
}

bool
ScenarioConfig::Set(Entry& e, const std::string& text, std::string& error)
{
    try
    {
        switch (e.kind)
        {
        case Kind::Double:
            *static_cast<double*>(e.ptr) = std::stod(text);
            break;
        case Kind::Int:
            *static_cast<int*>(e.ptr) = std::stoi(text);
            break;
        case Kind::Bool:
            *static_cast<bool*>(e.ptr) = (text == "1" || text == "true" || text == "True");
            break;
        case Kind::String:
            *static_cast<std::string*>(e.ptr) = text;
            break;
        }
    }
    catch (const std::exception&)
    {
        error = "bad value '" + text + "' for " + e.name;
        return false;
    }
    return true;
}

bool
ScenarioConfig::LoadFile(const std::string& path, std::string& error)
{
    std::ifstream in(path);
    if (!in)
    {
        error = "cannot open config file " + path;
        return false;
    }
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line))
    {
        lineNo++;
        size_t hash = line.find('#');
        if (hash != std::string::npos)
        {
            line = line.substr(0, hash);
        }
        line = Trim(line);
        if (line.empty())
        {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            error = path + ":" + std::to_string(lineNo) + ": expected 'Name = value'";
            return false;
        }
        std::string name = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));
        bool found = false;
        for (Entry& e : m_entries)
        {
            if (e.name == name)
            {
                found = true;
                if (!Set(e, value, error))
                {
                    return false;
                }
            }
        }
        if (!found)
        {
            error = path + ":" + std::to_string(lineNo) + ": unknown setting '" + name + "'";
            return false;
        }
    }
    return true;
}

void
ScenarioConfig::RegisterOnCommandLine(CommandLine& cmd)
{
    for (Entry& e : m_entries)
    {
        switch (e.kind)
        {
        case Kind::Double:
            cmd.AddValue(e.name, e.help, *static_cast<double*>(e.ptr));
            break;
        case Kind::Int:
            cmd.AddValue(e.name, e.help, *static_cast<int*>(e.ptr));
            break;
        case Kind::Bool:
            cmd.AddValue(e.name, e.help, *static_cast<bool*>(e.ptr));
            break;
        case Kind::String:
            cmd.AddValue(e.name, e.help, *static_cast<std::string*>(e.ptr));
            break;
        }
    }
}

std::string
ScenarioConfig::ValueOf(const Entry& e) const
{
    std::ostringstream os;
    os.precision(12);
    switch (e.kind)
    {
    case Kind::Double:
        os << *static_cast<double*>(e.ptr);
        break;
    case Kind::Int:
        os << *static_cast<int*>(e.ptr);
        break;
    case Kind::Bool:
        os << (*static_cast<bool*>(e.ptr) ? "true" : "false");
        break;
    case Kind::String:
        os << *static_cast<std::string*>(e.ptr);
        break;
    }
    return os.str();
}

void
ScenarioConfig::Dump(std::ostream& os) const
{
    for (const Entry& e : m_entries)
    {
        os << e.name << " = " << ValueOf(e) << "    # " << e.help << "\n";
    }
}

std::string
ScenarioConfig::OneLine(const std::vector<std::string>& skip) const
{
    std::ostringstream os;
    bool first = true;
    for (const Entry& e : m_entries)
    {
        if (std::find(skip.begin(), skip.end(), e.name) != skip.end())
        {
            continue;
        }
        os << (first ? "" : ";") << e.name << "=" << ValueOf(e);
        first = false;
    }
    return os.str();
}

} // namespace cbergr
} // namespace ns3
