#!/usr/bin/env python3
"""Download bioluminescence pathway CDS genes from Neonothopanus nambi via NCBI.

Correct accessions from Kotlobay et al. 2018 (PNAS) - LC4353xx series.
"""

from Bio import Entrez, SeqIO
import os
import sys
import time

Entrez.email = "research@octocorp.dev"

GENES = {
    "nnHispS": {"accession": "LC435355", "desc": "hispidin synthase"},
    "nnH3H":   {"accession": "LC435367", "desc": "hispidin-3-hydroxylase"},
    "nnLuz":   {"accession": "LC435379", "desc": "luciferase"},
    "nnCPH":   {"accession": "LC435389", "desc": "caffeylpyruvate hydrolase"},
}

OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def download_gene(name, accession, desc):
    """Download CDS from NCBI by accession."""
    print(f"Fetching {name} ({accession} - {desc})...")
    handle = Entrez.efetch(db="nucleotide", id=accession, rettype="gb", retmode="text")
    record = SeqIO.read(handle, "genbank")
    handle.close()

    # Try to extract CDS feature first
    cds_seq = None
    for feature in record.features:
        if feature.type == "CDS":
            cds_seq = str(feature.extract(record.seq))
            break

    if cds_seq is None:
        # These are mRNA records so full sequence is the CDS
        print(f"  No CDS annotation, using full sequence")
        cds_seq = str(record.seq)

    return cds_seq, record.description


def main():
    all_records = []

    for name, info in GENES.items():
        try:
            seq, desc = download_gene(name, info["accession"], info["desc"])
            # Save individual FASTA
            fasta_path = os.path.join(OUT_DIR, f"{name.lower()}.fasta")
            with open(fasta_path, "w") as f:
                f.write(f">{name} {info['accession']} {info['desc']}\n")
                for i in range(0, len(seq), 80):
                    f.write(seq[i:i+80] + "\n")
            all_records.append((name, info["accession"], info["desc"], seq))
            print(f"  Saved {fasta_path} ({len(seq)} bp)")
            time.sleep(1)
        except Exception as e:
            print(f"  ERROR downloading {name}: {e}", file=sys.stderr)
            sys.exit(1)

    # Write combined FASTA
    combined_path = os.path.join(OUT_DIR, "all_biolum_genes.fasta")
    with open(combined_path, "w") as f:
        for name, acc, desc, seq in all_records:
            f.write(f">{name} {acc} {desc}\n")
            for i in range(0, len(seq), 80):
                f.write(seq[i:i+80] + "\n")
    print(f"\nCombined FASTA: {combined_path} ({len(all_records)} genes)")

    print("\n=== Download Summary ===")
    for name, acc, desc, seq in all_records:
        print(f"  {name}: {len(seq)} bp ({desc})")


if __name__ == "__main__":
    main()
