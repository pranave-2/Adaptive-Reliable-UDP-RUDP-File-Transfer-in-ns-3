#!/bin/bash
# Runs the full experiment grid (210 simulations) and writes results.csv
# loss sweep, delay sweep, rate sweep x {tcp, fixed, adaptive} x 5 seeds
# Usage:  NS3_DIR=~/ns-3.49 ./scripts/run_all.sh     (default NS3_DIR = ~/ns-3.49)
NS3_DIR="${NS3_DIR:-$HOME/ns-3.49}"
cd "$NS3_DIR" || { echo "NS3_DIR not found: $NS3_DIR"; exit 1; }

FILE=1000000
RUNS=5
OUT=results.csv
echo "exp,mode,loss,delay_ms,rate_mbps,run,time_s,goodput_mbps,data_sent,retx,timeouts,intact" > $OUT

run() { # exp mode loss delay rate run
  line=$(./ns3 run --no-build "rudp-exp --mode=$2 --loss=$3 --delay=$4 --rate=$5 --run=$6 --fileSize=$FILE" 2>/dev/null | grep '^RESULT,' | sed 's/^RESULT,//')
  echo "$1,$line" >> $OUT
}

for m in tcp fixed adaptive; do
  for r in $(seq 1 $RUNS); do
    for loss in 0 0.01 0.02 0.03 0.05; do run loss  $m $loss 10 5 $r; done
    for d in 5 20 50 100 200;          do run delay $m 0.01 $d 5 $r; done
    for rate in 1 5 10 20;             do run rate  $m 0.01 20 $rate $r; done
  done
  echo "finished $m"
done
echo "done -> $NS3_DIR/$OUT"
