#!/bin/bash
# Short live demo (about 2 minutes). Needs the project already built (./ns3 build).
NS3_DIR="${NS3_DIR:-$HOME/ns-3.49}"
cd "$NS3_DIR" || exit 1
R="./ns3 run --no-build"
echo "RESULT,mode,loss,delay_ms,rate_mbps,run,time_s,goodput_mbps,data_sent,retx,timeouts,intact"

echo; echo "== 1. Same file, same link, 2% loss: TCP vs fixed vs adaptive"
for m in tcp fixed adaptive; do $R "rudp-exp --mode=$m --loss=0.02" 2>/dev/null; done

echo; echo "== 2. Adaptive with 2% payload corruption (checksum must catch it)"
$R "rudp-exp --mode=adaptive --loss=0.02 --corrupt=0.02"

echo; echo "== 3. High delay (100 ms one-way), 1% loss: fixed 200 ms timeout is too short"
for m in fixed adaptive tcp; do $R "rudp-exp --mode=$m --loss=0.01 --delay=100" 2>/dev/null; done

echo; echo "== 4. Write window/RTO trace for plotting (rudp-trace-adaptive.csv)"
$R "rudp-exp --mode=adaptive --loss=0.02 --trace=true" >/dev/null 2>&1 && echo "trace written"
