#!/usr/bin/env python3
"""
Generate synthetic DNA using Evo2 7B.

Usage:
    python evo2_generate.py --prompt "ATGCGATCG" --tokens 500
    python evo2_generate.py --prompt "ATGCGATCG" --tokens 500 --num 5 --output generated.fasta
"""

import argparse
import sys


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate DNA with Evo2")
    parser.add_argument("--prompt", required=True, help="DNA prompt sequence")
    parser.add_argument("--tokens", type=int, default=200, help="Tokens to generate")
    parser.add_argument("--num", type=int, default=1, help="Number of sequences to generate")
    parser.add_argument("--temperature", type=float, default=1.0, help="Sampling temperature")
    parser.add_argument("--top-k", type=int, default=4, help="Top-k sampling")
    parser.add_argument("--output", help="Output FASTA file")
    args = parser.parse_args()

    import torch
    from evo2 import Evo2

    print("Loading Evo2 7B...", file=sys.stderr)
    model = Evo2('evo2_7b')

    results = []
    for i in range(args.num):
        print(f"Generating sequence {i+1}/{args.num}...", file=sys.stderr)
        output = model.generate(
            prompt_seqs=[args.prompt],
            n_tokens=args.tokens,
            temperature=args.temperature,
            top_k=args.top_k,
        )
        seq = output.sequences[0]
        results.append(seq)

    del model
    torch.cuda.empty_cache()

    if args.output:
        with open(args.output, "w") as f:
            for i, seq in enumerate(results):
                f.write(f">generated_{i+1} prompt={args.prompt[:20]}... tokens={args.tokens}\n")
                for j in range(0, len(seq), 80):
                    f.write(seq[j:j+80] + "\n")
        print(f"Saved {len(results)} sequences to {args.output}", file=sys.stderr)
    else:
        for i, seq in enumerate(results):
            print(f">generated_{i+1}")
            print(seq)
