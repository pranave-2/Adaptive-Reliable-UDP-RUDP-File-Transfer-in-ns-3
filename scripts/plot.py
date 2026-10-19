import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

df = pd.read_csv("results.csv")
df = df[df.time_s > 0]
colors = {"tcp": "tab:green", "fixed": "tab:red", "adaptive": "tab:blue"}


def fig(exp, xcol, ycol, xl, yl, fname, logx=False):
    d = df[df.exp == exp]
    plt.figure(figsize=(6, 4))
    for m in ["tcp", "fixed", "adaptive"]:
        s = d[d["mode"] == m]
        if m == "tcp" and ycol == "retx":
            continue
        g = s.groupby(xcol)[ycol].agg(["mean", "std"]).reset_index()
        plt.errorbar(g[xcol], g["mean"], yerr=g["std"].fillna(0), marker="o",
                     capsize=3, label=m, color=colors[m])
    plt.xlabel(xl)
    plt.ylabel(yl)
    plt.grid(alpha=.3)
    plt.legend()
    if logx:
        plt.xscale("log")
    plt.tight_layout()
    plt.savefig(fname, dpi=150)
    plt.close()


fig("loss", "loss", "goodput_mbps", "Packet loss rate", "Goodput (Mbps)", "goodput_vs_loss.png")
fig("loss", "loss", "time_s", "Packet loss rate", "Completion time (s)", "time_vs_loss.png")
fig("loss", "loss", "retx", "Packet loss rate", "Retransmissions", "retx_vs_loss.png")
fig("delay", "delay_ms", "time_s", "One-way delay (ms)", "Completion time (s)", "time_vs_delay.png")
fig("delay", "delay_ms", "goodput_mbps", "One-way delay (ms)", "Goodput (Mbps)", "goodput_vs_delay.png")
fig("rate", "rate_mbps", "goodput_mbps", "Link rate (Mbps)", "Goodput (Mbps)", "goodput_vs_rate.png")

print(df.groupby(["exp", "mode"])[["time_s", "goodput_mbps", "retx", "timeouts"]].mean().round(3))
print("Integrity failures (RUDP):", len(df[(df["mode"] != "tcp") & (df.intact != 1)]))

try:
    d = pd.read_csv("rudp-trace-adaptive.csv")
    f, a = plt.subplots(2, 1, figsize=(7, 5), sharex=True)
    a[0].plot(d.time, d.cwnd)
    a[0].set_ylabel("Window (pkts)")
    a[1].plot(d.time, d.rto * 1000, color="tab:red", label="RTO")
    a[1].plot(d.time, d.srtt * 1000, color="gray", label="SRTT")
    a[1].set_ylabel("ms")
    a[1].set_xlabel("Time (s)")
    a[1].legend()
    plt.tight_layout()
    plt.savefig("adaptation_trace.png", dpi=150)
except FileNotFoundError:
    print("No rudp-trace-adaptive.csv found (run with --trace=true to get it)")
