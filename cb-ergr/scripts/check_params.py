#!/usr/bin/env python3
"""Rule 11 of the kickoff: all parameter defaults live in model/cb-ergr-params.h.

  check_params.py --write    regenerate scripts/params.json from the header (namespace path -> value)
  check_params.py            (default) fail if params.json is stale OR if an AddAttribute(...) default in model/,
                             helper/ or examples/ is a literal number/bool instead of a value from params::

Allowed literals: 0, 0.0 and false (neutral "off" defaults), and strings. Everything else must be
params::<group>::k<Name>. Python scripts read scripts/params.json, never their own copies.
"""
import json
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
HEADER = os.path.join(ROOT, "model", "cb-ergr-params.h")
JSON_PATH = os.path.join(ROOT, "scripts", "params.json")


def parse_header():
    """Collect 'constexpr <type> kName = value;' per namespace, e.g. {'phy': {'kTxRangeM': 1500.0}}."""
    params, group = {}, None
    for line in open(HEADER):
        line = line.split("//")[0].strip()
        m = re.match(r"namespace\s+(\w+)\s*$", line) or re.match(r"namespace\s+(\w+)\s*\{", line)
        if m and m.group(1) not in ("ns3", "cbergr", "params"):
            group = m.group(1)
            params.setdefault(group, {})
            continue
        m = re.match(r"constexpr\s+(?:std::array<\s*double\s*,\s*\d+\s*>|\w+)\s+(k\w+)\s*=\s*(.+);$", line)
        if m and group:
            name, expr = m.group(1), m.group(2).strip()
            if expr.startswith("{"):
                params[group][name] = [float(x) for x in expr.strip("{}").split(",")]
            elif expr in ("true", "false"):
                params[group][name] = (expr == "true")
            else:
                try:
                    params[group][name] = eval(expr.replace("1.0 /", "1.0 /"), {"__builtins__": {}})  # simple arithmetic only
                except Exception:
                    params[group][name] = expr
    return params


ATTR_VALUE = re.compile(r"\b(?:Double|Uinteger|Integer|Boolean)Value\s*\(\s*([^()]*?)\s*\)")


def attribute_violations():
    bad = []
    for sub in ("model", "helper", "examples"):
        for dirpath, _, files in os.walk(os.path.join(ROOT, sub)):
            for name in files:
                if not name.endswith(".cc") or name == "cb-ergr-params.h":
                    continue
                path = os.path.join(dirpath, name)
                text = open(path).read()
                for m in re.finditer(r"\.AddAttribute\(", text):
                    # take the statement up to the checker call that ends this attribute
                    chunk = text[m.start(): text.find("Make", text.find("Accessor", m.start()) - 30)]
                    for v in ATTR_VALUE.finditer(chunk):
                        literal = v.group(1)
                        if literal in ("0", "0.0", "false") or literal.startswith("params::") or "(" in literal:
                            continue
                        if re.fullmatch(r"-?[\d.eE+-]+|true", literal):
                            line = text.count("\n", 0, m.start() + v.start()) + 1
                            bad.append(f"{os.path.relpath(path, ROOT)}:{line}: attribute default '{literal}' is not from cb-ergr-params.h")
    return bad


def main():
    params = parse_header()
    text = json.dumps(params, indent=1, sort_keys=True) + "\n"
    if "--write" in sys.argv:
        open(JSON_PATH, "w").write(text)
        print(f"wrote {JSON_PATH} ({sum(len(v) for v in params.values())} values)")
        return
    problems = []
    if not os.path.exists(JSON_PATH) or open(JSON_PATH).read() != text:
        problems.append("scripts/params.json is out of date: run  scripts/check_params.py --write")
    problems += attribute_violations()
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    print(f"ok: params.json matches the header; no literal attribute defaults ({sum(len(v) for v in params.values())} parameters)")


if __name__ == "__main__":
    main()
