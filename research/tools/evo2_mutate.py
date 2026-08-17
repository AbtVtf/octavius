#!/usr/bin/env python3
"""
Mutation scanning with Evo2 — test every possible single mutation across a gene.

Usage:
    python evo2_mutate.py --fasta gene.fasta --start 0 --end 500
    python evo2_mutate.py --fasta gene.fasta --output mutations.json
"""

import argparse
import json
import sys

BASES = ['A', 'T', 'G', 'C']


def mutation_scan(model, seq, start=0, end=None):
    """Scan all single-nucleotide mutations in a region."""
    import torch
    import torch.nn.functional as F

    if end is None:
        end = len(seq)

    # Score wild-type first
    input_ids = torch.tensor(model.tokenizer.tokenize(seq), dtype=torch.int).unsqueeze(0).to('cuda:0')
    with torch.no_grad():
        out, _ = model(input_ids)
        logits = out[0] if isinstance(out, tuple) else out
    while logits.dim() > 2:
        logits = logits[0]
    log_probs = F.log_softmax(logits[:-1, :].float(), dim=-1)
    target_ids = input_ids[0, 1:].long()
    token_log_probs = log_probs[torch.arange(len(target_ids)), target_ids]
    wt_score = token_log_probs.mean().item()

    print(f"Wild-type score: {wt_score:.6f}", file=sys.stderr)

    results = []
    for pos in range(start, min(end, len(seq))):
        wt_base = seq[pos]
        for mut_base in BASES:
            if mut_base == wt_base:
                continue
            mut_seq = seq[:pos] + mut_base + seq[pos+1:]

            input_ids = torch.tensor(model.tokenizer.tokenize(mut_seq), dtype=torch.int).unsqueeze(0).to('cuda:0')
            with torch.no_grad():
                out, _ = model(input_ids)
                logits = out[0] if isinstance(out, tuple) else out
            while logits.dim() > 2:
                logits = logits[0]
            log_probs = F.log_softmax(logits[:-1, :].float(), dim=-1)
            target_ids = input_ids[0, 1:].long()
            token_log_probs = log_probs[torch.arange(len(target_ids)), target_ids]
            mut_score = token_log_probs.mean().item()

            delta = mut_score - wt_score
            results.append({
                "position": pos,
                "wild_type": wt_base,
                "mutant": mut_base,
                "wt_score": round(wt_score, 6),
                "mut_score": round(mut_score, 6),
                "delta": round(delta, 6),
                "effect": "beneficial" if delta > 0.001 else "damaging" if delta < -0.001 else "neutral",
            })

        if (pos - start) % 50 == 0:
            print(f"  Position {pos}/{end}...", file=sys.stderr)

    return {"wild_type_score": wt_score, "mutations": results}


def parse_fasta(path):
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
    parser = argparse.ArgumentParser(description="Mutation scan with Evo2")
    parser.add_argument("--fasta", required=True, help="Input FASTA file (first sequence used)")
    parser.add_argument("--start", type=int, default=0, help="Start position")
    parser.add_argument("--end", type=int, default=None, help="End position")
    parser.add_argument("--output", help="Output JSON file")
    args = parser.parse_args()

    names, seqs = parse_fasta(args.fasta)

    import torch
    from evo2 import Evo2
    print("Loading Evo2 7B...", file=sys.stderr)
    model = Evo2('evo2_7b')

    results = mutation_scan(model, seqs[0], args.start, args.end)
    results["gene_name"] = names[0]
    results["gene_length"] = len(seqs[0])

    del model
    torch.cuda.empty_cache()

    output = json.dumps(results, indent=2)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
        print(f"Results saved to {args.output}", file=sys.stderr)
    else:
        print(output)
