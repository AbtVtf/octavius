#!/usr/bin/env python3
"""
Predict protein structure using Boltz-1.

Usage:
    python boltz_fold.py --fasta protein.fasta
    python boltz_fold.py --fasta protein.fasta --output-dir results/structures/
    python boltz_fold.py --seq "MKTLLLTLVVVTIVCLDLGYT..." --name my_protein
"""

import argparse
import subprocess
import sys
import os
import tempfile


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Protein folding with Boltz-1")
    parser.add_argument("--fasta", help="Input FASTA file")
    parser.add_argument("--seq", help="Protein sequence (single letter amino acids)")
    parser.add_argument("--name", default="protein", help="Name for the sequence")
    parser.add_argument("--output-dir", default=".", help="Output directory for results")
    args = parser.parse_args()

    if args.seq:
        # Write to temp FASTA
        fasta_path = os.path.join(args.output_dir, f"{args.name}.fasta")
        with open(fasta_path, "w") as f:
            f.write(f">{args.name}\n{args.seq}\n")
    elif args.fasta:
        fasta_path = args.fasta
    else:
        parser.error("Provide --fasta or --seq")

    os.makedirs(args.output_dir, exist_ok=True)

    print(f"Running Boltz-1 on {fasta_path}...", file=sys.stderr)
    print(f"Output directory: {args.output_dir}", file=sys.stderr)

    result = subprocess.run(
        ["boltz", "predict", fasta_path, "--use-msa-server", "--out_dir", args.output_dir],
        capture_output=True,
        text=True,
    )

    if result.returncode == 0:
        print("Boltz-1 completed successfully!", file=sys.stderr)
        print(result.stdout)
    else:
        print(f"Boltz-1 failed: {result.stderr[:500]}", file=sys.stderr)
        sys.exit(1)
