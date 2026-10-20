#!/bin/bash
# Result 2: adaptive RUDP with a larger initial window and/or a larger window increase per round trip.
# Same 14 conditions x 5 seeds as Result 1, for 4 adaptive variants -> results2.csv (280 runs).
# tcp and fixed are NOT rerun: Result 1 numbers are reused (they are unchanged and deterministic).
# Usage:  NS3_DIR=~/ns-3.49 ./scripts/run_result2.sh
NS3_DIR="${NS3_DIR:-$HOME/ns-3.49}"
cd "$NS3_DIR" || { echo "NS3_DIR not found: $NS3_DIR"; exit 1; }

FILE=1000000
RUNS=5
OUT=results2.csv
echo "exp,variant,loss,delay_ms,rate_mbps,run,time_s,goodput_mbps,data_sent,retx,timeouts,intact" > $OUT

# variant name : initial window : additive increase (packets per round trip)
VARIANTS=("r1_control:4:1" "r2a_ai4:4:4" "r2b_iw10:10:1" "r2c_iw10_ai4:10:4")

run() { # exp variant initCwnd ai loss delay rate run
  line=$(./ns3 run --no-build "rudp-exp --mode=adaptive --initCwnd=$3 --ai=$4 --loss=$5 --delay=$6 --rate=$7 --run=$8 --fileSize=$FILE" 2>/dev/null | grep '^RESULT,' | sed 's/^RESULT,adaptive,//')
  echo "$1,$2,$line" >> $OUT
}

for v in "${VARIANTS[@]}"; do
  IFS=: read -r name iw ai <<< "$v"
  for r in $(seq 1 $RUNS); do
    for loss in 0 0.01 0.02 0.03 0.05; do run loss  $name $iw $ai $loss 10 5 $r; done
    for d in 5 20 50 100 200;          do run delay $name $iw $ai 0.01 $d 5 $r; done
    for rate in 1 5 10 20;             do run rate  $name $iw $ai 0.01 20 $rate $r; done
  done
  echo "finished $name"
done
echo "done -> $NS3_DIR/$OUT"
