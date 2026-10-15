#!/usr/bin/env bash
# Reproduce the CB-ERGR build environment (spec section 2):
#   ns-3.41 + Aqua-Sim NG (this repository) in src/aqua-sim-ng
#   + the cb-ergr module (this repository's cb-ergr/) in contrib/cb-ergr.
#
# Usage: scripts/setup_ns3.sh [WORKSPACE_DIR]   (default: ../ns3-workspace)
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WS="${1:-${REPO_DIR}/../ns3-workspace}"
NS3_DIR="${WS}/ns-3-dev"

mkdir -p "${WS}"
if [ ! -d "${NS3_DIR}" ]; then
    git clone --depth 1 -b ns-3.41 https://gitlab.com/nsnam/ns-3-dev.git "${NS3_DIR}"
fi

ln -sfn "${REPO_DIR}" "${NS3_DIR}/src/aqua-sim-ng"
ln -sfn "${REPO_DIR}/cb-ergr" "${NS3_DIR}/contrib/cb-ergr"

cd "${NS3_DIR}"
./ns3 configure --build-profile=debug --enable-examples --enable-tests \
    --disable-python --disable-werror \
    --enable-modules="aqua-sim-ng;cb-ergr;applications;point-to-point-layout"
# point-to-point-layout is needed only because netanim (pulled in by one
# upstream aqua-sim-ng example) has a test that includes it.
./ns3 build

echo "ns-3 ready in ${NS3_DIR}"
echo "  ./ns3 run broadcastMAC_example     # upstream example (M0 acceptance)"
echo "  ./ns3 run cb-ergr-smoke            # cb-ergr links against aqua-sim-ng"
echo "  ./test.py -s cb-ergr               # cb-ergr test suite"
