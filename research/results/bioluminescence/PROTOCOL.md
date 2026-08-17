# Bioluminescence Gene Transfer to Yeast — Research Protocol

## Hypothesis

The 4-gene fungal bioluminescence pathway from *Neonothopanus nambi* can be
computationally optimized for heterologous expression in *Saccharomyces cerevisiae*
(baker's yeast) using Evo2 mutation scanning and codon optimization, followed by
Boltz-1 structural validation of the key luciferase enzyme.

## Background

In 2018, Kotlobay et al. (PNAS) discovered the complete fungal bioluminescence
pathway requires only 4 genes to produce light from caffeic acid:

1. **hispidin synthase (hisps)** — converts caffeic acid + malonyl-CoA to hispidin
2. **hispidin-3-hydroxylase (h3h)** — converts hispidin to 3-hydroxyhispidin (the luciferin)
3. **luciferase (luz)** — oxidizes 3-hydroxyhispidin to produce light (green, ~520 nm)
4. **caffeylpyruvate hydrolase (cph)** — recycling enzyme, converts oxidized luciferin back to caffeic acid

Additionally, **npgA** (4'-phosphopantetheinyl transferase) may be needed for
activating hispidin synthase in yeast.

Mitikas et al. (2023) demonstrated successful transfer to yeast, but light output
was dim. Optimizing gene sequences for yeast codon usage and identifying beneficial
mutations could significantly improve brightness.

## Target Organism

**Saccharomyces cerevisiae** (baker's yeast)
- Codon bias: strong preference for A/T at wobble positions
- GC content: ~38% (lower than fungi like N. nambi)
- Well-established transformation protocols

## Gene Sources

| Gene | Organism | NCBI Accession | Function |
|------|----------|---------------|----------|
| nnLuz | *N. nambi* | QGA85050.1 (protein) / MK484702 | Luciferase |
| nnH3H | *N. nambi* | QGA85048.1 (protein) / MK484700 | Hispidin-3-hydroxylase |
| nnHispS | *N. nambi* | QGA85047.1 (protein) / MK484699 | Hispidin synthase |
| nnCPH | *N. nambi* | QGA85049.1 (protein) / MK484701 | Caffeylpyruvate hydrolase |

Note: If exact accessions are not available, search NCBI for "Neonothopanus nambi
bioluminescence" or use the sequences from Kotlobay et al. 2018 supplementary data.

## Experimental Plan

### Phase 1: Gene Download & Evo2 Baseline Scoring

Download CDS for all 4 pathway genes. Score with Evo2 to establish baseline fitness.

### Phase 2: Luciferase (nnLuz) Mutation Scan

nnLuz is the key enzyme — it directly produces light. Run full Evo2 mutation scan
to identify positions where mutations improve the DNA sequence score. Focus on:
- Start codon context (first 100 bp)
- Active site region
- Full-length scan if computationally feasible

### Phase 3: Codon Optimization for Yeast

Design yeast-codon-optimized versions of all 4 genes:
- Replace codons with S. cerevisiae preferred codons
- Avoid rare yeast codons (CGA for Arg, etc.)
- Score optimized vs native sequences with Evo2

### Phase 4: Luciferase Structure Prediction

Run Boltz-1 on nnLuz protein to:
- Understand the 3D structure
- Identify active site residues
- Verify non-synonymous mutations don't disrupt folding

## Expected Outcomes

- Evo2 baseline scores for all 4 bioluminescence genes
- Mutation catalog for nnLuz with beneficial/neutral/damaging positions
- Yeast-codon-optimized gene sequences
- Boltz-1 structure of nnLuz for active site analysis
