#!/bin/bash
# Copies the simulator into an existing ns-3.49 tree and builds it.
NS3_DIR="${NS3_DIR:-$HOME/ns-3.49}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cp "$HERE/src/rudp-exp.cc" "$NS3_DIR/scratch/rudp-exp.cc" || exit 1
cd "$NS3_DIR" && ./ns3 configure && ./ns3 build
