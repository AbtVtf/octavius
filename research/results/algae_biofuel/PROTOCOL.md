# Algae Genetic Enhancement Protocol — Lipid Overproduction

## Hypothesis

Key lipid biosynthesis genes in *Chlamydomonas reinhardtii* (model green alga) can be
optimized at the DNA level using Evo2 mutation scanning to identify beneficial
single-nucleotide variants that improve gene fitness. Combined with CRISPR-based
overexpression of lipid pathway genes and downregulation of competing starch synthesis,
we can design a genetic modification strategy that increases triacylglycerol (TAG)
accumulation.

## Background

Microalgae are promising biofuel feedstocks because they:
- Grow rapidly with high photosynthetic efficiency
- Accumulate 20-50% dry weight as lipids under stress
- Do not compete with food crops for arable land

The bottleneck: wild-type strains partition carbon toward starch under normal conditions.
Genetic engineering can redirect flux toward lipid biosynthesis.

## Target Organism

**Chlamydomonas reinhardtii** (NCBI Taxonomy ID: 3055)
- Genome: ~120 Mb, 17 chromosomes, well-annotated
- Extensive genetic toolkit (CRISPR/Cas9, RNAi, nuclear/chloroplast transformation)
- Reference genome: NCBI Assembly GCA_000002595.3

## Target Genes

### Overexpression Targets (lipid biosynthesis)

| Gene | Full Name | Function | NCBI Gene ID | Strategy |
|------|-----------|----------|-------------|----------|
| DGAT1 | Diacylglycerol acyltransferase type 1 | Final committed step of TAG biosynthesis | Cre12.g557750 | Overexpress; Evo2-optimize coding sequence |
| DGTT1-5 | Type-2 DGAT family | Alternative TAG synthesis enzymes | Multiple loci | Score all paralogs; overexpress best-scoring |
| PDAT1 | Phospholipid:DAG acyltransferase | Converts membrane lipids to TAG | Cre02.g106400 | Overexpress |
| ACCase | Acetyl-CoA carboxylase | Rate-limiting step, fatty acid initiation | Cre12.g519100 | Optimize regulatory regions |

### Knockdown Targets (competing pathways)

| Gene | Full Name | Function | Strategy |
|------|-----------|----------|----------|
| STA6 | Isoamylase (starch debranching) | Required for starch granule formation | CRISPRi knockdown to redirect carbon to lipids |
| STA1 | ADP-glucose pyrophosphorylase | Starch synthesis initiation | CRISPRi knockdown |

### Regulatory Targets

| Element | Function | Strategy |
|---------|----------|----------|
| DGAT1 promoter | Controls DGAT1 transcription | Evo2 generate synthetic promoter variants |
| Nitrogen-responsive elements | Activate lipid accumulation under N-deprivation | Identify with Evo2 scoring; engineer constitutive versions |

## Experimental Plan

### Phase 1: Gene Acquisition & Baseline Scoring

**Objective**: Download target gene sequences and establish Evo2 baseline scores.

1. Download coding sequences for DGAT1, DGTT1-5, PDAT1, ACCase, STA6, STA1 from NCBI/Phytozome
2. Save as FASTA files in `research/data/algae_genes/`
3. Score all sequences with Evo2 to establish baseline fitness
4. Compare paralog scores (especially DGTT1-5 family) to identify the most "natural" variant

**Tools**:
```bash
source ~/evo2-env/bin/activate
python research/tools/evo2_score.py --fasta research/data/algae_genes/dgat_family.fasta \
    --output research/results/algae_biofuel/dgat_baseline_scores.json
```

### Phase 2: Mutation Scanning of DGAT1

**Objective**: Identify beneficial mutations in the primary TAG synthesis gene.

1. Run Evo2 mutation scan on DGAT1 coding sequence (focus on first 500 bp including start codon context and N-terminal domain)
2. Run scan on catalytic domain region (~bp 600-1200, containing MBOAT domain)
3. Catalog all beneficial mutations (delta > 0.001)
4. Filter for synonymous vs non-synonymous changes
5. Prioritize synonymous beneficial mutations (codon optimization without protein change)

**Tools**:
```bash
source ~/evo2-env/bin/activate
python research/tools/evo2_mutate.py --fasta research/data/algae_genes/dgat1.fasta \
    --start 0 --end 500 --output research/results/algae_biofuel/dgat1_mutations_nterm.json

python research/tools/evo2_mutate.py --fasta research/data/algae_genes/dgat1.fasta \
    --start 600 --end 1200 --output research/results/algae_biofuel/dgat1_mutations_catalytic.json
```

### Phase 3: Synthetic Promoter Design

**Objective**: Generate Evo2-designed promoter variants for DGAT1 overexpression.

1. Extract native DGAT1 promoter region (500 bp upstream of start codon)
2. Use Evo2 to generate 10 synthetic variants seeded from native promoter
3. Score all variants to find highest-fitness promoter designs
4. Identify candidates for constitutive high expression

**Tools**:
```bash
source ~/evo2-env/bin/activate
python research/tools/evo2_generate.py --prompt "<native_promoter_last_50bp>" \
    --tokens 500 --num 10 --temperature 0.8 --top-k 4 \
    --output research/results/algae_biofuel/synthetic_promoters.fasta

python research/tools/evo2_score.py --fasta research/results/algae_biofuel/synthetic_promoters.fasta \
    --output research/results/algae_biofuel/synthetic_promoter_scores.json
```

### Phase 4: Protein Structure Validation

**Objective**: Verify that proposed DGAT1 mutations (non-synonymous) do not disrupt protein folding.

1. Translate wild-type and top mutant DGAT1 sequences to protein
2. Run Boltz-1 folding on both
3. Compare predicted structures and confidence scores
4. Reject mutations that destabilize key structural elements

**Tools**:
```bash
python research/tools/boltz_fold.py --seq "<wt_protein_seq>" --name dgat1_wt \
    --output-dir research/results/algae_biofuel/structures/

python research/tools/boltz_fold.py --seq "<mutant_protein_seq>" --name dgat1_mutant \
    --output-dir research/results/algae_biofuel/structures/
```

### Phase 5: Results Synthesis & CRISPR Design

**Objective**: Compile all findings into a modification blueprint.

1. Rank all DGAT1 mutations by Evo2 delta score
2. Select top 5-10 synonymous mutations for codon-optimized DGAT1
3. Select best synthetic promoter
4. Design CRISPR guide RNAs for STA6 knockdown
5. Produce final protocol document with:
   - Optimized DGAT1 construct (promoter + codon-optimized CDS)
   - STA6 CRISPRi guide sequences
   - Predicted improvement metrics
   - Suggested validation experiments (lipid quantification via Nile Red staining, GC-MS fatty acid profiling)

## Expected Outcomes

- Baseline Evo2 fitness scores for all target genes
- Catalog of beneficial mutations in DGAT1 (expecting 5-20 positions with delta > 0.001)
- 2-3 synthetic promoter candidates scoring higher than native
- Structural validation of top DGAT1 variants
- Complete genetic modification blueprint ready for wet-lab implementation

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Evo2 trained primarily on prokaryotic DNA; eukaryotic algal genes may score differently | Compare scores across C. reinhardtii genes to establish species-specific baseline |
| Synonymous mutations may affect mRNA folding/stability | Cross-reference with codon usage tables for C. reinhardtii |
| Synthetic promoters may not function in vivo | Design multiple candidates; validate expression in silico before wet-lab |
| DGAT1 is membrane-bound; Boltz-1 may struggle with transmembrane regions | Focus structural analysis on soluble domains; flag low-confidence TM regions |
