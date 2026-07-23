#!/usr/bin/env python3
"""Entrypoint: run all 5 experiments and print summary tables to stdout."""
import os
import sys

import pandas as pd

from experiments import run_all

HERE = os.path.dirname(os.path.abspath(__file__))
OUTDIR = os.path.join(HERE, "results")


def main():
    pd.set_option("display.width", 160)
    pd.set_option("display.max_columns", 20)
    pd.set_option("display.float_format", lambda x: f"{x:.4g}")

    results = run_all(OUTDIR)

    print("\n" + "=" * 78)
    print("EXPERIMENT 1: healthy vs. degenerate regime")
    print("=" * 78)
    print(results["exp1"].to_string(index=False))

    print("\n" + "=" * 78)
    print("EXPERIMENT 2: zero-hits bound tightness (summary over x'=0 reps)")
    print("=" * 78)
    print(results["exp2_summary"].to_string())
    print(f"(n={len(results['exp2'])} zero-hit repetitions pooled across instances with true k>0)")

    print("\n" + "=" * 78)
    print("EXPERIMENT 3: low-count regime spread")
    print("=" * 78)
    print(results["exp3"].to_string(index=False))

    print("\n" + "=" * 78)
    print("EXPERIMENT 4: local-enum vs. direct baseline")
    print("=" * 78)
    print(results["exp4"].to_string(index=False))

    print("\n" + "=" * 78)
    print("EXPERIMENT 5: density x correlation sweep")
    print("=" * 78)
    print(results["exp5"].to_string(index=False))

    print(f"\nAll CSVs and PNGs written to: {OUTDIR}")


if __name__ == "__main__":
    sys.exit(main())
