#!/usr/bin/env python3
"""
Codon-optimize bioluminescence genes for S. cerevisiae expression.

Uses yeast-preferred codons while maintaining GC content 35-45%.
Avoids rare yeast codons: CGA, CGG, CTC, AGG.
"""

import sys
from pathlib import Path

OUT_DIR = Path(__file__).parent / "yeast_optimized"

# S. cerevisiae preferred codon table
# Primary preferred codon for each amino acid
YEAST_PREFERRED = {
    "F": ["TTC", "TTT"],       # Phe: TTC preferred, TTT as low-GC alt
    "L": ["CTG", "TTG", "TTA"],# Leu: CTG preferred, TTG/TTA for low-GC
    "I": ["ATC", "ATT"],       # Ile: ATC preferred, ATT as low-GC alt
    "M": ["ATG"],              # Met: only codon
    "V": ["GTG", "GTT"],       # Val: GTG preferred, GTT alt
    "S": ["TCT", "TCA", "AGT"],# Ser: TCT preferred, TCA/AGT alts
    "P": ["CCA", "CCT"],       # Pro: CCA preferred
    "T": ["ACC", "ACT", "ACA"],# Thr: ACC preferred
    "A": ["GCT", "GCA"],       # Ala: GCT preferred
    "Y": ["TAC", "TAT"],       # Tyr: TAC preferred, TAT low-GC alt
    "H": ["CAC", "CAT"],       # His: CAC preferred
    "Q": ["CAA", "CAG"],       # Gln: CAA preferred
    "N": ["AAC", "AAT"],       # Asn: AAC preferred
    "K": ["AAG", "AAA"],       # Lys: AAG preferred
    "D": ["GAC", "GAT"],       # Asp: GAC preferred
    "E": ["GAA", "GAG"],       # Glu: GAA preferred
    "C": ["TGT", "TGC"],       # Cys: TGT preferred
    "W": ["TGG"],              # Trp: only codon
    "R": ["AGA", "CGT"],       # Arg: AGA preferred (avoid CGA, CGG, AGG)
    "G": ["GGT", "GGA"],       # Gly: GGT preferred
    "*": ["TAA", "TGA", "TAG"],# Stop
}


def translate(dna):
    """Translate DNA to protein."""
    codon_table = {
        "TTT": "F", "TTC": "F", "TTA": "L", "TTG": "L",
        "CTT": "L", "CTC": "L", "CTA": "L", "CTG": "L",
        "ATT": "I", "ATC": "I", "ATA": "I", "ATG": "M",
        "GTT": "V", "GTC": "V", "GTA": "V", "GTG": "V",
        "TCT": "S", "TCC": "S", "TCA": "S", "TCG": "S",
        "CCT": "P", "CCC": "P", "CCA": "P", "CCG": "P",
        "ACT": "T", "ACC": "T", "ACA": "T", "ACG": "T",
        "GCT": "A", "GCC": "A", "GCA": "A", "GCG": "A",
        "TAT": "Y", "TAC": "Y", "TAA": "*", "TAG": "*",
        "CAT": "H", "CAC": "H", "CAA": "Q", "CAG": "Q",
        "AAT": "N", "AAC": "N", "AAA": "K", "AAG": "K",
        "GAT": "D", "GAC": "D", "GAA": "E", "GAG": "E",
        "TGT": "C", "TGC": "C", "TGA": "*", "TGG": "W",
        "CGT": "R", "CGC": "R", "CGA": "R", "CGG": "R",
        "AGT": "S", "AGC": "S", "AGA": "R", "AGG": "R",
        "GGT": "G", "GGC": "G", "GGA": "G", "GGG": "G",
    }
    protein = []
    for i in range(0, len(dna) - 2, 3):
        codon = dna[i:i+3].upper()
        if len(codon) == 3 and codon in codon_table:
            protein.append(codon_table[codon])
    return "".join(protein)


def gc_content(seq):
    """Calculate GC content of a DNA sequence."""
    gc = sum(1 for c in seq.upper() if c in "GC")
    return gc / len(seq) if seq else 0


def optimize_for_yeast(dna):
    """Codon-optimize a CDS for S. cerevisiae."""
    protein = translate(dna)
    if protein.endswith("*"):
        protein_core = protein[:-1]
        has_stop = True
    else:
        protein_core = protein
        has_stop = False

    # First pass: use primary preferred codons
    optimized_codons = ["ATG"]  # Keep start codon
    for aa in protein_core[1:]:
        if aa in YEAST_PREFERRED:
            optimized_codons.append(YEAST_PREFERRED[aa][0])

    if has_stop:
        optimized_codons.append("TAA")  # Preferred yeast stop

    optimized = "".join(optimized_codons)
    gc = gc_content(optimized)

    # If GC too high (>45%), swap some codons to lower-GC alternatives
    if gc > 0.45:
        for i in range(1, len(optimized_codons) - (1 if has_stop else 0)):
            if gc_content("".join(optimized_codons)) <= 0.45:
                break
            aa = protein_core[i] if i < len(protein_core) else None
            if aa and aa in YEAST_PREFERRED and len(YEAST_PREFERRED[aa]) > 1:
                # Try lower-GC alternative
                current = optimized_codons[i]
                for alt in YEAST_PREFERRED[aa][1:]:
                    if gc_content(alt) < gc_content(current):
                        optimized_codons[i] = alt
                        break

    # If GC too low (<35%), swap some codons to higher-GC alternatives
    optimized = "".join(optimized_codons)
    gc = gc_content(optimized)
    if gc < 0.35:
        for i in range(1, len(optimized_codons) - (1 if has_stop else 0)):
            if gc_content("".join(optimized_codons)) >= 0.35:
                break
            aa = protein_core[i] if i < len(protein_core) else None
            if aa and aa in YEAST_PREFERRED and len(YEAST_PREFERRED[aa]) > 1:
                current = optimized_codons[i]
                for alt in YEAST_PREFERRED[aa]:
                    if gc_content(alt) > gc_content(current):
                        optimized_codons[i] = alt
                        break

    optimized = "".join(optimized_codons)
    return optimized


def parse_fasta(path):
    """Parse a FASTA file into (names, descriptions, sequences)."""
    names, descs, seqs = [], [], []
    current_name, current_desc, current_seq = None, None, []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith(">"):
                if current_name:
                    names.append(current_name)
                    descs.append(current_desc)
                    seqs.append("".join(current_seq))
                parts = line[1:].split(None, 1)
                current_name = parts[0]
                current_desc = parts[1] if len(parts) > 1 else ""
                current_seq = []
            elif line:
                current_seq.append(line.upper())
    if current_name:
        names.append(current_name)
        descs.append(current_desc)
        seqs.append("".join(current_seq))
    return names, descs, seqs


def write_fasta(path, name, desc, seq):
    """Write a single FASTA entry."""
    with open(path, "w") as f:
        f.write(f">{name} {desc}\n")
        for i in range(0, len(seq), 80):
            f.write(seq[i:i+80] + "\n")


def main():
    input_fasta = Path(__file__).parent / "all_biolum_genes.fasta"
    OUT_DIR.mkdir(exist_ok=True)

    names, descs, seqs = parse_fasta(input_fasta)

    all_optimized = []
    for name, desc, seq in zip(names, descs, seqs):
        print(f"Optimizing {name} ({len(seq)} bp)...", file=sys.stderr)

        native_gc = gc_content(seq)
        optimized = optimize_for_yeast(seq)
        opt_gc = gc_content(optimized)

        # Verify translation matches
        native_protein = translate(seq)
        opt_protein = translate(optimized)
        assert native_protein == opt_protein, f"Protein mismatch for {name}!"

        print(f"  Native:    {len(seq)} bp, GC={native_gc:.1%}", file=sys.stderr)
        print(f"  Optimized: {len(optimized)} bp, GC={opt_gc:.1%}", file=sys.stderr)

        # Save individual optimized FASTA
        opt_name = f"{name}_yeast_opt"
        opt_desc = f"{desc} (yeast codon-optimized)"
        write_fasta(OUT_DIR / f"{name.lower()}_optimized.fasta", opt_name, opt_desc, optimized)
        all_optimized.append((opt_name, opt_desc, optimized))

    # Write combined optimized FASTA
    combined_path = OUT_DIR / "all_optimized.fasta"
    with open(combined_path, "w") as f:
        for name, desc, seq in all_optimized:
            f.write(f">{name} {desc}\n")
            for i in range(0, len(seq), 80):
                f.write(seq[i:i+80] + "\n")
    print(f"\nCombined optimized FASTA: {combined_path} ({len(all_optimized)} genes)", file=sys.stderr)


if __name__ == "__main__":
    main()
