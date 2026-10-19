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
│   ├── run_all.sh           # run the full 210-run experiment -> results.csv
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
      (only these parameters change; rerun the same 210 runs and compare with Result 1)
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
