#!/usr/bin/env python3
"""
Score DNA sequences using Evo2 7B.
Higher score (closer to 0) = more natural/functional DNA.

Usage:
    python evo2_score.py --fasta input.fasta
    python evo2_score.py --seq "ATGCGATCG..."
    python evo2_score.py --fasta input.fasta --output results.json
"""

import argparse
import json
import sys
import os

def score_sequences(sequences, names=None):
    """Score a list of DNA sequences with Evo2."""
    import torch
    import torch.nn.functional as F
    from evo2 import Evo2

    print("Loading Evo2 7B...", file=sys.stderr)
    model = Evo2('evo2_7b')
    print("Model loaded.", file=sys.stderr)

    results = []
    for i, seq in enumerate(sequences):
        name = names[i] if names else f"seq_{i}"
        print(f"Scoring {name} ({len(seq)} bp)...", file=sys.stderr)

        input_ids = torch.tensor(model.tokenizer.tokenize(seq), dtype=torch.int).unsqueeze(0).to('cuda:0')
        with torch.no_grad():
            out, _ = model(input_ids)
            logits = out[0] if isinstance(out, tuple) else out
        while logits.dim() > 2:
            logits = logits[0]
        log_probs = F.log_softmax(logits[:-1, :].float(), dim=-1)
        target_ids = input_ids[0, 1:].long()
        token_log_probs = log_probs[torch.arange(len(target_ids)), target_ids]
        score = token_log_probs.mean().item()

        results.append({
            "name": name,
            "length": len(seq),
            "score": round(score, 6),
        })
        print(f"  {name}: {score:.6f}", file=sys.stderr)

    del model
    torch.cuda.empty_cache()
    return results


def parse_fasta(path):
    """Parse a FASTA file into (names, sequences)."""
    names, seqs = [], []
    current_name, current_seq = None, []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith(">"):
                if current_name:
                    names.append(current_name)
                    seqs.append("".join(current_seq))
                current_name = line[1:].split()[0]
                current_seq = []
            elif line:
                current_seq.append(line.upper())
    if current_name:
        names.append(current_name)
        seqs.append("".join(current_seq))
    return names, seqs


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Score DNA with Evo2")
    parser.add_argument("--fasta", help="Input FASTA file")
    parser.add_argument("--seq", help="Single DNA sequence")
    parser.add_argument("--output", help="Output JSON file (default: stdout)")
    args = parser.parse_args()

    if args.fasta:
        names, seqs = parse_fasta(args.fasta)
    elif args.seq:
        names, seqs = ["input"], [args.seq]
    else:
        parser.error("Provide --fasta or --seq")

    results = score_sequences(seqs, names)

    output = json.dumps(results, indent=2)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
        print(f"Results saved to {args.output}", file=sys.stderr)
    else:
        print(output)
