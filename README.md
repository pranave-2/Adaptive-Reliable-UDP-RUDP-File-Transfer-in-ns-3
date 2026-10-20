# Adaptive Reliable UDP (RUDP) File Transfer in ns-3

A reliable file-transfer protocol built **on top of UDP** and evaluated in the
**ns-3.49** network simulator. It compares three ways of moving the same 1 MB file
over the same simulated link:

| Mode | What it is |
|---|---|
| `tcp` | ns-3's built-in TCP (`BulkSend` + `PacketSink`): the standard to compare against |
| `fixed` | Our RUDP with a **fixed** window (16 packets) and a **fixed** timeout (200 ms) |
| `adaptive` | Our RUDP that **measures the network** and adapts its timeout and window |

The research question: **does adapting the retransmission timeout and the sending
window to measured network conditions make a UDP-based file transfer faster and
less wasteful than fixed parameters, and how close does it get to TCP?**

> **Status:** Result 1 (the untuned baseline) is complete. Result 2 (larger initial
> window and faster window growth) is next. Result 1 is kept so that Result 2 can be
> compared against it. See [Results](#results) and [Roadmap](#roadmap).

---

## Contents

1. [Why this project](#why-this-project)
2. [How the protocol works](#how-the-protocol-works)
3. [Repository layout](#repository-layout)
4. [Requirements](#requirements)
5. [Quick start](#quick-start)
6. [Command-line options](#command-line-options)
7. [Reading the output](#reading-the-output)
8. [The experiments](#the-experiments)
9. [Results](#results)
10. [Known limitations](#known-limitations)
11. [Roadmap](#roadmap)
12. [Working as a team](#working-as-a-team)
13. [References](#references)

---

## Why this project

UDP is a bare datagram service: packets can be lost, duplicated, reordered or
corrupted and nothing recovers them. TCP fixes this, but a program that wants its own
reliability on UDP (as UDT and QUIC do) has to build it. Two decisions dominate how
well such a protocol performs:

- **How long to wait** before deciding a packet is lost (the retransmission timeout).
  Too short wastes bandwidth on needless retransmissions. Too long makes recovery slow.
- **How many packets may be outstanding** (the window). Too small leaves the link idle.
  Too large causes loss and retransmission.

A fixed choice suits only one kind of network. This project makes both choices
**adaptive** and measures the effect.

## How the protocol works

### Packet format (13-byte header + payload)

```
 +------+-------------+-------------+--------+----------+------------------+
 | Type | Transfer ID | Sequence no | Length | Checksum | Payload (<=1000B)|
 | 1 B  |    4 B      |    4 B      |  2 B   |   2 B    |                  |
 +------+-------------+-------------+--------+----------+------------------+
```

- `Type`: 1 = DATA, 2 = ACK. All multi-byte fields are big-endian.
- `Checksum`: 16-bit ones'-complement sum over the whole datagram (checksum field
  set to zero while computing). A datagram with a wrong checksum, length or transfer
  ID is silently discarded.
- For an ACK, the sequence field is the **next sequence number the receiver expects**
  (a cumulative ACK).

### Receiver

Accepts only the next expected packet, counts duplicates and out-of-order packets (the
out-of-order ones are discarded, not buffered), and replies to every valid DATA packet
with a cumulative ACK.

### Sender: Go-Back-N

Keeps up to *W* packets in flight with **one timer on the oldest unacknowledged
packet**. When the timer fires, it goes back and resends from that packet onward.

### What "adaptive" adds

| Mechanism | Rule |
|---|---|
| RTT estimate | `SRTT = 7/8 SRTT + 1/8 R`, `RTTVAR = 3/4 RTTVAR + 1/4 abs(SRTT - R)` |
| Timeout | `RTO = SRTT + max(1 ms, 4 RTTVAR)`, limited to 20 ms .. 2 s; starts at 300 ms |
| Karn's rule | no RTT sample is taken from a retransmitted packet |
| Back-off | RTO doubles (up to 2 s) on each timeout |
| Window growth | start at 4; +1 per ACK below `ssthresh` (slow start, `ssthresh` starts at 128); +1/cwnd per ACK above it |
| Loss reaction | timeout: `ssthresh = max(cwnd/2, 2)`, `cwnd = 1`. 3 duplicate ACKs: `cwnd = ssthresh`, resend immediately (fast retransmit) |
| Window cap | 128 packets |

`fixed` mode uses window 16 and RTO 200 ms, with no RTT estimation, no window change
and no fast retransmit.

### Integrity check

The "file" is a deterministic pseudo-random byte array. At the end, the receiver's
buffer is compared with the original using a **64-bit FNV-1a hash**. The last field of
each result line (`intact`) is `1` only if they match. (FNV-1a is not a cryptographic
hash: it detects accidental differences only.)

### Network model

Two nodes joined by a point-to-point link (`10.1.1.1` sender, `10.1.1.2` receiver, UDP
port 9000). Default 5 Mbps and 10 ms one-way delay. Loss is a `RateErrorModel` on the
receive side of **both** devices, so data packets **and ACKs** are lost independently.

## Repository layout

```
adaptive-rudp-ns3/
├── README.md
├── src/
│   └── rudp-exp.cc          # the whole simulator: header, sender, receiver, TCP baseline
├── scripts/
│   ├── install.sh           # copy rudp-exp.cc into ns-3 and build
│   ├── run_all.sh           # Result 1: full 210-run experiment -> results.csv
│   ├── run_result2.sh       # Result 2: 4 adaptive variants x 14 conditions x 5 seeds -> results2.csv
│   ├── compare_result2.py   # Result 2 vs Result 1: tables + graphs
│   ├── plot.py              # results.csv -> graphs + summary
│   ├── summarize.py         # per-condition tables + sanity checks
│   └── demo.sh              # 2-minute live demo
└── results/
    └── result1/             # Result 1 data, graphs and report (add your files here)
```

## Requirements

- Linux or WSL (tested on Ubuntu under WSL)
- **ns-3.49** already built (`./ns3 configure && ./ns3 build` works). Install guide:
  <https://www.nsnam.org/docs/installation/html/>
- Python 3 with `pandas` and `matplotlib` (`pip install pandas matplotlib`)

## Quick start

All scripts find ns-3 through the `NS3_DIR` variable (default `~/ns-3.49`).

```bash
# 1. get the code
git clone <this-repo-url> adaptive-rudp-ns3
cd adaptive-rudp-ns3
export NS3_DIR=~/ns-3.49          # change if ns-3 is elsewhere

# 2. copy the simulator into ns-3 and build it
./scripts/install.sh

# 3. try one run of each mode at 2% loss
cd $NS3_DIR
./ns3 run --no-build "rudp-exp --mode=tcp      --loss=0.02"
./ns3 run --no-build "rudp-exp --mode=fixed    --loss=0.02"
./ns3 run --no-build "rudp-exp --mode=adaptive --loss=0.02"
```

Full experiment (about 5-10 minutes) and graphs:

```bash
cd adaptive-rudp-ns3
./scripts/run_all.sh                              # writes $NS3_DIR/results.csv
cd $NS3_DIR
python3 ~/adaptive-rudp-ns3/scripts/plot.py       # graphs appear next to results.csv
python3 ~/adaptive-rudp-ns3/scripts/summarize.py  # per-condition tables + checks
```

Live demo: `./scripts/demo.sh`

**Troubleshooting**

- *ns-3 builds every program in `scratch/`.* If any other file there does not
  compile, the build fails. Move it out of `scratch/`.
- *`ModuleNotFoundError: pandas`* means `python3` is not the Python where pandas is
  installed (for example it was installed for `python3.11`). Run the script with that
  interpreter, e.g. `python3.11 plot.py`.
- *Scripts show `\r` errors* if they were edited on Windows. Run
  `sed -i 's/\r$//' scripts/*.sh scripts/*.py src/rudp-exp.cc`.

## Command-line options

Pass them to `rudp-exp`, for example `./ns3 run --no-build "rudp-exp --mode=adaptive --loss=0.03 --delay=50"`.

| Option | Default | Meaning |
|---|---|---|
| `--mode` | `adaptive` | `tcp`, `fixed` or `adaptive` |
| `--loss` | `0` | per-packet loss probability, applied in both directions |
| `--delay` | `10` | one-way link delay in ms |
| `--rate` | `5` | link rate in Mbps |
| `--fileSize` | `1000000` | file size in bytes |
| `--payload` | `1000` | payload bytes per packet |
| `--run` | `1` | random-number run (seed index) |
| `--fixedWin` | `16` | window for `fixed` mode |
| `--fixedRto` | `0.2` | timeout in seconds for `fixed` mode |
| `--maxWin` | `128` | window cap for `adaptive` mode |
| `--corrupt` | `0` | probability that a received packet has one byte altered (tests the checksum) |
| `--trace` | `false` | write `rudp-trace-<mode>.csv` with window, RTO, SRTT over time |
| `--initCwnd` | `4` | adaptive mode: initial window in packets (Result 1 value) |
| `--ai` | `1` | adaptive mode: additive increase in packets per round trip (Result 1 value) |

## Reading the output

Each run prints one line on stdout:

```
RESULT,mode,loss,delay_ms,rate_mbps,run,time_s,goodput_mbps,data_sent,retx,timeouts,intact
```

| Field | Meaning |
|---|---|
| `time_s` | completion time. RUDP: until the final ACK reaches the sender. TCP: until the sink has all bytes. `-1` means the run did not finish |
| `goodput_mbps` | `8 x file size / time` (useful bits per second) |
| `data_sent` | DATA packets sent, including retransmissions (RUDP only) |
| `retx` | packets sent more than once (RUDP only; `-1` for TCP, not collected) |
| `timeouts` | retransmission-timer expiries (RUDP only; `-1` for TCP) |
| `intact` | `1` if the received file matches the original (for TCP: finished) |

RUDP runs also print `[info] bad(checksum)=.. dup=.. out-of-order=..` on stderr.

## The experiments

`scripts/run_all.sh` runs **3 modes x 14 conditions x 5 seeds = 210 runs**
(RNG seed 12345, run numbers 1 to 5), each transferring 1,000,000 bytes.

| Sweep | Varied | Values | Fixed at |
|---|---|---|---|
| Loss | packet loss | 0, 1, 2, 3, 5 % | 10 ms delay, 5 Mbps |
| Delay | one-way delay | 5, 20, 50, 100, 200 ms | 1 % loss, 5 Mbps |
| Rate | link rate | 1, 5, 10, 20 Mbps | 1 % loss, 20 ms delay |

## Results

### Result 1: baseline, before any tuning

Parameters as in [How the protocol works](#how-the-protocol-works): initial window 4,
`ssthresh` 128, +1 per ACK in slow start, +1/cwnd afterwards. All 210 runs completed
and every RUDP file matched the original.

Mean over each sweep (five seeds per condition; mean of per-condition means):

| Sweep | Mode | Time (s) | Goodput (Mbps) | Retransmissions | Timeouts |
|---|---|---:|---:|---:|---:|
| Loss | TCP | 3.398 | 3.195 | n/a | n/a |
| Loss | Fixed | 6.311 | 2.038 | 335.0 | 21.08 |
| Loss | Adaptive | 3.714 | 2.568 | 259.4 | 3.40 |
| Delay | TCP | 6.042 | 2.743 | n/a | n/a |
| Delay | Fixed | 11.792 | 1.169 | 630.0 | 39.64 |
| Delay | Adaptive | 13.747 | 1.571 | 211.6 | 0.40 |
| Rate | TCP | 3.753 | 4.483 | n/a | n/a |
| Rate | Fixed | 6.022 | 1.532 | 134.6 | 8.60 |
| Rate | Adaptive | 5.222 | 2.095 | 231.8 | 0.30 |

Single-run smoke test (2 % loss, 10 ms, 5 Mbps, seed 1):

| Mode | Time (s) | Retransmissions | Timeouts |
|---|---:|---:|---:|
| Adaptive | 3.469 | 223 | 0 |
| Fixed | 7.831 | 448 | 28 |
| TCP | 3.136 | n/a | n/a |
| Adaptive + 2 % corruption | 5.158 | 313 | 3 (19 checksum failures, file intact) |

**What it shows**

- **Reliability holds.** Every RUDP transfer was intact, including with loss and with
  injected corruption.
- **Adaptive timeouts work.** Timeouts per run fell from 21.1 to 3.4 (loss sweep) and
  from 39.6 to 0.4 (delay sweep).
- **Under loss** adaptive RUDP is 41 % faster than fixed and within about 9 % of TCP.
- **On long-delay paths** adaptive RUDP is *slower* on average than fixed (13.7 s vs
  11.8 s) and much slower than TCP (6.0 s), even though it wastes far fewer packets.
- **On the rate sweep** adaptive is 13 % faster than fixed but slower than TCP, and it
  retransmits more.

**Hypotheses (not yet tested; Result 2 tests the first)**

1. After each loss the window is halved or reset, then regrows by one packet per round
   trip. At a 400 ms round trip that takes many seconds, which would explain the slow
   delay-sweep result.
2. At 200 ms delay the bandwidth-delay product is about 251 packets, above the
   128-packet window cap.
3. On slow links the window may overshoot the queue and cause extra retransmissions
   (queue drops were not measured).

**Model check.** A sender limited by its window cannot exceed `8 x payload x W / RTT`.
For fixed RUDP (W = 16) over the five delays this bound is 5.00, 3.07, 1.26, 0.63 and
0.32 Mbps (mean 2.06). The measured mean is 1.17 Mbps, below the bound as expected
with 1 % loss and timeouts. A fixed 200 ms timeout also fires spuriously once the RTT
exceeds 200 ms (one-way delay above about 99 ms at 5 Mbps).

### Result 2: larger initial window and faster window growth

**Question.** Result 1 suggested that adaptive RUDP is slow on long-delay paths because
its window regrows by only one packet per round trip after a loss. Does a larger initial
window and/or a larger additive increase fix that?

**What changes.** Only two adaptive-sender parameters, now command-line options
(`--initCwnd`, `--ai`). Their defaults are the Result 1 values, so Result 1 is still
reproducible with the same code. Everything else (timeout rules, back-off, fast
retransmit, 128-packet cap, loss reaction) is unchanged.

| Variant | `--initCwnd` | `--ai` | Purpose |
|---|---:|---:|---|
| `r1_control` | 4 | 1 | Result 1 settings; must reproduce Result 1 exactly (sanity check) |
| `r2a_ai4` | 4 | 4 | faster growth only |
| `r2b_iw10` | 10 | 1 | larger initial window only (10 is the value in RFC 6928) |
| `r2c_iw10_ai4` | 10 | 4 | both |

`tcp` and `fixed` are not rerun; their Result 1 numbers are reused. 4 variants x 14
conditions x 5 seeds = 280 runs.

**Expectation (to be confirmed by the data).** The initial window only affects the first
few round trips, so its effect should be small on a 1,000-packet transfer. The additive
increase acts after every loss, so it is the more likely fix for the long-delay case.
A 128-packet cap may still limit the 200 ms case (bandwidth-delay product about 251 packets).

**How to run.**

```bash
git checkout -b result-2
./scripts/install.sh                       # rebuild with the new options
cd $NS3_DIR
# sanity check: defaults must still give the Result 1 line below
./ns3 run --no-build "rudp-exp --mode=adaptive --loss=0.02"
#   RESULT,adaptive,0.02,10,5,1,3.4687,2.3063,1223,223,0,1
cd -
./scripts/run_result2.sh                   # about 10 minutes, writes $NS3_DIR/results2.csv
cd $NS3_DIR && python3 ~/adaptive-rudp-ns3/scripts/compare_result2.py
mkdir -p ~/adaptive-rudp-ns3/results/result2
cp results2.csv r2_*.png ~/adaptive-rudp-ns3/results/result2/
```

`compare_result2.py` prints a control check (max time difference between `r1_control`
and Result 1 adaptive, which must be 0), the mean per sweep, per-condition completion
times, and writes `r2_time_vs_delay.png`, `r2_time_vs_loss.png`,
`r2_goodput_vs_rate.png` and `r2_summary.png`.

**Results.** 280 runs, all completed, all files intact. The control variant reproduced
Result 1 exactly (maximum time difference 0 s over 70 matched runs).

Mean over each sweep:

| Sweep | Variant | Time (s) | Goodput (Mbps) | Retransmissions | Timeouts |
|---|---|---:|---:|---:|---:|
| Loss | TCP | 3.398 | 3.195 | n/a | n/a |
| Loss | `r1_control` (Result 1) | 3.714 | 2.568 | 259.4 | 3.40 |
| Loss | `r2a_ai4` | **2.874** | 2.963 | 445.4 | 1.16 |
| Loss | `r2b_iw10` | 3.672 | 2.597 | 266.6 | 3.44 |
| Loss | `r2c_iw10_ai4` | 2.882 | 2.953 | 489.1 | 1.08 |
| Delay | TCP | 6.042 | 2.743 | n/a | n/a |
| Delay | `r1_control` (Result 1) | 13.747 | 1.571 | 211.6 | 0.40 |
| Delay | `r2a_ai4` | **11.501** | 1.793 | 440.8 | 0.44 |
| Delay | `r2b_iw10` | 14.553 | 1.588 | 227.0 | 0.40 |
| Delay | `r2c_iw10_ai4` | 11.660 | 1.730 | 501.3 | 0.44 |
| Rate | TCP | 3.753 | 4.483 | n/a | n/a |
| Rate | `r1_control` (Result 1) | 5.222 | 2.095 | 231.8 | 0.30 |
| Rate | `r2a_ai4` | **4.956** | 2.944 | 392.0 | 0.40 |
| Rate | `r2b_iw10` | 5.137 | 2.155 | 237.0 | 0.30 |
| Rate | `r2c_iw10_ai4` | 5.270 | 2.806 | 480.9 | 0.30 |

**Findings**

- **The initial window does not matter.** `r2b_iw10` is within about 1-6 % of the control
  everywhere (median change -1.5 % over the 70 paired runs), and adding it to `ai=4`
  (`r2c`) gives no gain over `r2a`. It only affects the first few round trips.
- **A larger additive increase helps a lot.** `r2a_ai4` was faster than the control in 47 of
  70 paired runs and slower in 15 (median change -22 %). Loss sweep: 23 % faster, and
  faster than TCP on average (2.87 s vs 3.40 s), because it beats TCP at 3 % and 5 % loss
  (3.07 s vs 4.26 s, 4.13 s vs 6.79 s) while TCP is still faster at 0-2 % loss.
- **The delay result was partly a window-growth problem.** `ai=4` cut completion time at
  50 ms from 8.13 s to 5.00 s and at 100 ms from 15.77 s to 9.52 s (TCP: 4.80 s and
  8.36 s). It did not fix 200 ms (39.0 s to 37.7 s; five-seed range 24.7-49.1 s, so
  the change is within noise). TCP takes 12.8 s there.
- **Fast links gain, slow links lose.** At 10 and 20 Mbps `ai=4` is close to TCP
  (2.07 s vs 2.07 s, 1.93 s vs 1.82 s). At 1 Mbps it is slower (13.0 s vs 10.7 s) and
  retransmits more (547 vs 240).
- **The price is retransmissions.** `ai=4` retransmits 69-108 % more packets
  (loss sweep 259 to 445) while timeouts fall (3.4 to 1.2 per run).
- **Occasional retransmission storms.** In 4 of 70 `r2a` runs, nearly the whole file was
  sent twice (about 1,040 retransmissions); the control never did this. Those runs are
  also slower (for example 3.41 s vs 2.08 s at 5 ms delay).

**Interpretation (not yet proven by a trace).** At 200 ms and 1 % loss the transfer looks
loss-recovery-limited, not growth-limited: a window-controlled flow settles near
1.22/sqrt(p) = about 12 packets per round trip, which at a 0.4 s round trip is roughly
80 round trips, or about 33 s, close to the observed 39 s. Go-Back-N then resends whole
windows after every loss, and TCP wins because it retransmits only what was lost.
The retransmission storms and the 1 Mbps penalty are consistent with the window
overshooting the link queue, but queue drops were not measured.

**Conclusion for Result 2.** The best setting is `initCwnd=4, ai=4`. It is a real improvement
under loss, moderate delay and fast links, but it does not close the gap to TCP at long
delay and it increases wasted retransmissions. The next change should therefore target
loss recovery itself (selective retransmission) rather than window growth.

## Known limitations

- Go-Back-N, not Selective Repeat: after a loss, packets that already arrived are
  resent. This is the main reason TCP wins at higher loss.
- No FIN/FIN_ACK handshake: completion is the cumulative ACK of the last packet, and
  the receiver knows the file size in advance.
- FNV-1a is a non-cryptographic hash.
- TCP retransmissions and timeouts are not collected.
- Completion time is measured at different points: TCP at the sink (and includes its
  connection set-up), RUDP at the sender (includes the final ACK's return trip).
- Loss is uniform and independent; real losses are bursty. One flow, one link, five
  seeds per condition.
- Averages over a sweep hide differences between conditions. Use
  `scripts/summarize.py` for per-condition numbers.

## Roadmap

- [x] ns-3 setup, header, one-packet exchange, single-file simulator
- [x] Fixed and adaptive RUDP, TCP baseline, integrity check
- [x] **Result 1**: 210-run baseline
- [ ] **Result 2**: larger initial window and larger window increase per round trip
      (only these parameters change; same 14 conditions and 5 seeds; compare with Result 1).
      Code and scripts are in place; see [Result 2](#result-2-larger-initial-window-and-faster-window-growth)
- [ ] Selective Repeat with a SACK bitmap in the ACK
- [ ] Raise the 128-packet cap for high bandwidth-delay paths
- [ ] FIN/FIN_ACK handshake and a file-size field
- [ ] Collect TCP retransmission counts; add bursty loss and competing traffic

## Working as a team

- **Do not edit `src/rudp-exp.cc` for Result 1.** Result 1 is tagged so it can always
  be reproduced:
  ```bash
  git tag -a result-1 -m "Result 1: baseline adaptive RUDP, 210 runs"
  git push origin result-1
  ```
- Do Result 2 work on a branch (`git checkout -b result-2`) and save its data under
  `results/result2/`.
- When changing protocol parameters, keep a note of which values changed so the
  comparison with Result 1 stays fair.
- Check any change with `./scripts/demo.sh` before pushing: every `intact` field must be `1`.

## References

1. J. Postel, "User Datagram Protocol," RFC 768, 1980.
2. V. Jacobson, "Congestion avoidance and control," ACM SIGCOMM, 1988.
3. P. Karn and C. Partridge, "Improving round-trip time estimates in reliable transport protocols," ACM SIGCOMM, 1987.
4. V. Paxson et al., "Computing TCP's Retransmission Timer," RFC 6298, 2011.
5. M. Allman, V. Paxson, E. Blanton, "TCP Congestion Control," RFC 5681, 2009.
6. D.-M. Chiu and R. Jain, "Analysis of the increase and decrease algorithms for congestion avoidance in computer networks," 1989.
7. M. Mathis et al., "The macroscopic behavior of the TCP congestion avoidance algorithm," ACM CCR, 1997.
8. J. Chu et al., "Increasing TCP's Initial Window," RFC 6928, 2013.
9. M. Mathis et al., "TCP Selective Acknowledgment Options," RFC 2018, 1996.
10. Y. Gu and R. L. Grossman, "UDT: UDP-based data transfer for high-speed wide area networks," Computer Networks, 2007.
11. A. Langley et al., "The QUIC transport protocol: design and Internet-scale deployment," ACM SIGCOMM, 2017.
12. G. F. Riley and T. R. Henderson, "The ns-3 network simulator," 2010.
