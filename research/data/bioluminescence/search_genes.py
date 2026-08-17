#!/usr/bin/env python3
"""Search NCBI for the correct bioluminescence CDS accessions from Neonothopanus nambi."""

from Bio import Entrez
import time

Entrez.email = "research@octocorp.dev"

# Search for each gene
searches = [
    "Neonothopanus nambi luciferase",
    "Neonothopanus nambi hispidin synthase",
    "Neonothopanus nambi hispidin hydroxylase",
    "Neonothopanus nambi caffeylpyruvate hydrolase",
    "nnLuz luciferase fungal bioluminescence",
    "Kotlobay bioluminescence luciferase 2018",
]

for query in searches:
    print(f"\n=== Search: {query} ===")
    handle = Entrez.esearch(db="nucleotide", term=query, retmax=10)
    result = Entrez.read(handle)
    handle.close()

    ids = result["IdList"]
    print(f"  Found {result['Count']} results, showing {len(ids)}")

    if ids:
        handle = Entrez.esummary(db="nucleotide", id=",".join(ids))
        summaries = Entrez.read(handle)
        handle.close()
        for s in summaries:
            print(f"  {s['AccessionVersion']}: {s['Title'][:100]} ({s['Length']} bp)")

    time.sleep(1)
