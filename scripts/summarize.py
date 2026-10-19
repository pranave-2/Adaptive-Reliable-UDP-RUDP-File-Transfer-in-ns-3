"""Per-condition summary of results.csv (mean over the 5 seeds) plus sanity checks.
Usage:  python3 scripts/summarize.py [path/to/results.csv]
"""
import sys
import pandas as pd

path = sys.argv[1] if len(sys.argv) > 1 else "results.csv"
df = pd.read_csv(path)

print(f"rows: {len(df)}")
print("incomplete runs (time_s == -1):", int((df.time_s == -1).sum()))
print("integrity failures (intact != 1):", int((df.intact != 1).sum()))

d = df[df.time_s > 0]
for exp, col in [("loss", "loss"), ("delay", "delay_ms"), ("rate", "rate_mbps")]:
    s = d[d.exp == exp]
    for metric in ["time_s", "goodput_mbps", "retx", "timeouts"]:
        print(f"\n== {exp} sweep: mean {metric}")
        print(s.pivot_table(index=col, columns="mode", values=metric, aggfunc="mean").round(3))
