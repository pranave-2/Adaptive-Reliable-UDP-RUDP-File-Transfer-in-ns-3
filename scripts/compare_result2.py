"""Compare Result 2 variants with Result 1 (tcp, fixed, adaptive).
Run from the folder that holds results.csv (Result 1) and results2.csv (Result 2):
    python3 /path/to/scripts/compare_result2.py
Outputs tables on screen and PNG graphs: r2_time_vs_delay.png, r2_time_vs_loss.png,
r2_goodput_vs_rate.png, r2_summary.png
"""
import sys
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

r1 = pd.read_csv(sys.argv[1] if len(sys.argv) > 1 else "results.csv")
r2 = pd.read_csv(sys.argv[2] if len(sys.argv) > 2 else "results2.csv")
r1["variant"] = r1["mode"]          # tcp / fixed / adaptive
df = pd.concat([r1.drop(columns="mode"), r2], ignore_index=True)

print("Result 2 rows:", len(r2), "| incomplete (time=-1):", int((r2.time_s == -1).sum()),
      "| integrity failures:", int((r2.intact != 1).sum()))

# 1) sanity: the control variant must reproduce Result 1 adaptive exactly
a = r1[r1["mode"] == "adaptive"].set_index(["exp", "loss", "delay_ms", "rate_mbps", "run"]).time_s
c = r2[r2.variant == "r1_control"].set_index(["exp", "loss", "delay_ms", "rate_mbps", "run"]).time_s
d = (a - c).abs().dropna()
print(f"\nControl check: {len(d)} matched runs, max |time difference| vs Result 1 adaptive = {d.max():.6f} s",
      "(should be 0)")

d = df[df.time_s > 0]
order = ["tcp", "fixed", "adaptive", "r1_control", "r2a_ai4", "r2b_iw10", "r2c_iw10_ai4"]
order = [v for v in order if v in set(d.variant)]
cols = {"loss": "loss", "delay": "delay_ms", "rate": "rate_mbps"}

print("\n=== Mean over each sweep ===")
summ = d.groupby(["exp", "variant"])[["time_s", "goodput_mbps", "retx", "timeouts"]].mean().round(3)
print(summ.reindex(pd.MultiIndex.from_product([["loss", "delay", "rate"], order]), fill_value=float("nan")).dropna(how="all"))

for exp, col in cols.items():
    print(f"\n=== {exp} sweep: mean completion time (s) per condition ===")
    print(d[d.exp == exp].pivot_table(index=col, columns="variant", values="time_s", aggfunc="mean")[order].round(2))

colour = {"tcp": "tab:green", "fixed": "tab:red", "adaptive": "lightgray", "r1_control": "tab:blue",
          "r2a_ai4": "tab:orange", "r2b_iw10": "tab:purple", "r2c_iw10_ai4": "tab:brown"}

def line(exp, ycol, ylabel, fname):
    s = d[d.exp == exp]; col = cols[exp]
    plt.figure(figsize=(6.6, 4.2))
    for v in order:
        if v == "adaptive":      # identical to r1_control; hide to avoid clutter
            continue
        g = s[s.variant == v].groupby(col)[ycol].agg(["mean", "std"]).reset_index()
        plt.errorbar(g[col], g["mean"], yerr=g["std"].fillna(0), marker="o", capsize=2, label=v, color=colour[v])
    plt.xlabel({"loss": "Packet loss rate", "delay": "One-way delay (ms)", "rate": "Link rate (Mbps)"}[exp])
    plt.ylabel(ylabel); plt.grid(alpha=.3); plt.legend(fontsize=8); plt.tight_layout()
    plt.savefig(fname, dpi=150); plt.close()

line("delay", "time_s", "Completion time (s)", "r2_time_vs_delay.png")
line("loss", "time_s", "Completion time (s)", "r2_time_vs_loss.png")
line("rate", "goodput_mbps", "Goodput (Mbps)", "r2_goodput_vs_rate.png")

m = d.groupby(["exp", "variant"]).time_s.mean().unstack("variant")[[v for v in order if v != "adaptive"]]
m = m.loc[["loss", "delay", "rate"]]
ax = m.plot(kind="bar", figsize=(7.5, 4), color=[colour[v] for v in m.columns], rot=0)
ax.set_ylabel("Mean completion time (s)"); ax.set_xlabel("Sweep"); ax.grid(axis="y", alpha=.3)
plt.tight_layout(); plt.savefig("r2_summary.png", dpi=150); plt.close()
print("\nSaved: r2_time_vs_delay.png r2_time_vs_loss.png r2_goodput_vs_rate.png r2_summary.png")
