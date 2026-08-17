#!/usr/bin/env python3
"""Search for remaining bioluminescence gene accessions."""

from Bio import Entrez
import time

Entrez.email = "research@octocorp.dev"

# Search for LC4353xx range - these are the Kotlobay et al. submissions
searches = [
    "Neonothopanus nambi h3h",
    "Neonothopanus nambi cph",
    "Neonothopanus nambi hispidin-3-hydroxylase",
    "Neonothopanus nambi caffeylpyruvate",
    "Neonothopanus nambi mRNA",
    "LC435355 OR LC435356 OR LC435357 OR LC435358 OR LC435359 OR LC435360 OR LC435370 OR LC435371 OR LC435372 OR LC435373 OR LC435374 OR LC435375 OR LC435376 OR LC435377 OR LC435378 OR LC435379 OR LC435380",
]

for query in searches:
    print(f"\n=== Search: {query} ===")
    handle = Entrez.esearch(db="nucleotide", term=query, retmax=20)
    result = Entrez.read(handle)
    handle.close()

    ids = result["IdList"]
    print(f"  Found {result['Count']} results, showing {len(ids)}")

    if ids:
        handle = Entrez.esummary(db="nucleotide", id=",".join(ids))
        summaries = Entrez.read(handle)
        handle.close()
        for s in summaries:
            acc = s.get('AccessionVersion', s.get('Caption', 'N/A'))
            title = s.get('Title', 'N/A')[:120]
            length = s.get('Length', 'N/A')
            print(f"  {acc}: {title} ({length} bp)")

    time.sleep(1)
