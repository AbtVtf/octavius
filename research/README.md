# Evo 2 Genomic Foundation Model - Briefing for Autonomous Research Agent

## What is this?

Evo 2 is a 7-billion parameter genomic foundation model from the Arc Institute, trained on 8.8 trillion nucleotides from all domains of life. It understands DNA at single-nucleotide resolution and can:

- **Score sequences**: Rate how "natural" a DNA sequence looks (log-likelihood scoring)
- **Generate sequences**: Autocomplete or create novel DNA from a prompt
- **Predict mutation effects**: Compare wild-type vs mutant scores to predict if a mutation is beneficial, neutral, or damaging
- **Design regulatory elements**: Generate synthetic promoters, codon-optimize genes for target organisms

## Installation (already done)

- **Virtual environment**: `/home/mafuu/evo2-env`
- **Activate**: `source ~/evo2-env/bin/activate`
- **Package**: evo2 v0.5.5 with flash-attn 2.8.0.post2
- **Model**: `evo2_7b` (7B parameters, 1M base pair context window)
- **Hardware**: RTX 3090 24GB -- fits the 7B model when no other GPU processes are running
- **IMPORTANT**: Before loading the model, ensure GPU memory is free. Check with `nvidia-smi`. Kill other GPU processes if needed. The model uses ~14GB VRAM.

## How to use

### Basic usage

```python
source ~/evo2-env/bin/activate
python3 << 'EOF'
from evo2 import Evo2

model = Evo2('evo2_7b')

# Generate DNA
output = model.generate(prompt_seqs=["ATGCGATCGATCG"], n_tokens=200, temperature=1.0, top_k=4)
print(output.sequences[0])
EOF
```

### Score a sequence (log-likelihood)

```python
import torch
import torch.nn.functional as F
from evo2 import Evo2

model = Evo2('evo2_7b')

def score_seq(model, seq):
    input_ids = torch.tensor(model.tokenizer.tokenize(seq), dtype=torch.int).unsqueeze(0).to('cuda:0')
    with torch.no_grad():
        out, _ = model(input_ids)
        logits = out[0] if isinstance(out, tuple) else out
    while logits.dim() > 2:
        logits = logits[0]
    log_probs = F.log_softmax(logits[:-1, :].float(), dim=-1)
    target_ids = input_ids[0, 1:].long()
    token_log_probs = log_probs[torch.arange(len(target_ids)), target_ids]
    return token_log_probs.mean().item()

# Higher score (closer to 0) = more natural-looking DNA
score = score_seq(model, "ATGGCACCTCTCACCACCATG")
```

### Predict mutation effects

```python
wt_score = score_seq(model, wild_type_sequence)
mut_score = score_seq(model, mutant_sequence)
delta = mut_score - wt_score
# delta > 0 = potentially beneficial
# delta ~ 0 = neutral
# delta < 0 = potentially damaging
```

## Key technical notes

- The model output from `model(input_ids)` returns a tuple: `(outputs, embeddings)`. `outputs` is itself a tuple where `outputs[0]` is the logits tensor with shape `[batch, seq_len, vocab_size=512]`.
- Use `while logits.dim() > 2: logits = logits[0]` to safely unwrap to `[seq_len, vocab]`.
- Always cast target_ids to `.long()` for gather operations.
- The model loads from HuggingFace cache at `~/.cache/huggingface/hub/models--arcinstitute--evo2_7b/` (~14GB).
- Each forward pass on a ~1800bp sequence takes a few seconds on the 3090.
- For generation, use `model.generate(prompt_seqs=[seq], n_tokens=N, temperature=T, top_k=K)`.

## Files on disk

| File | Description |
|---|---|
| `/home/mafuu/psilocybin_cluster.fasta` | Full 74,622 bp psilocybin biosynthetic gene cluster from *Psilocybe cyanescens* (NCBI ID: 1347545313) |
| `/home/mafuu/super_psih.fasta` | AI-engineered PsiH variant (9 mutations) + wild-type PsiH for comparison |
| `/home/mafuu/psih_protein_sequence.txt` | PsiH protein sequence (494 amino acids) for structure prediction tools |
| `/home/mafuu/alphafold3_psih_job_v2.json` | AlphaFold3 Server job file (protein + heme) |
| `/home/mafuu/alphafold3_psih_protein_only.json` | AlphaFold3 Server job file (protein only -- this one was submitted) |
| `/home/mafuu/psih_structure.pdb` | ESMFold structure prediction (FAILED -- low confidence, do not trust) |

## What we discovered

### The psilocybin gene cluster contains more than the 4 core genes

The full cluster has 15+ genes including:
- **Core pathway**: PsiD (decarboxylase), PsiH (hydroxylase, bottleneck), PsiK (kinase), PsiM (methyltransferase)
- **Duplicated genes**: TWO copies of PsiH (33% sequence similarity) and TWO copies of PsiT (transporter)
- **Mystery protein**: A 351bp hypothetical protein (positions 57991-58342) sitting between the two PsiH copies. BLAST against all of GenBank returned ZERO significant hits. Unknown function. Evo mutation scan showed only 3% position sensitivity, suggesting possible pseudogene or regulatory element.
- **Support genes**: Transcriptional regulator, P450 monooxygenase (not PsiH), kinase, casein kinase

### Gene scores (Evo 2 log-likelihood, higher = more natural)

| Gene | Score |
|---|---|
| PsiH copy 1 | -1.0516 (best in cluster) |
| Transcriptional regulator | -1.0623 |
| PsiH copy 2 | -1.0658 |
| PsiD | -1.0961 |
| PsiM | -1.1861 |
| PsiK | -1.2211 |
| Mystery protein | -1.3815 (lowest) |

### Super-PsiH engineered variant

9 mutations that Evo predicts improve PsiH:
- Positions: 0(A>T), 175(T>C), 550(C>T), 850(A>C), 1000(A>G), 1025(T>C), 1050(A>G), 1350(T>C), 1750(T>G)
- Wild-type score: -1.051567
- Super-PsiH score: -1.045714
- Improvement: +0.005853
- Identity to wild-type: 99.5%
- **UNVALIDATED** -- this is a hypothesis, not a confirmed improvement. Structure validation pending (AlphaFold3 job submitted).

### PsiH duplication analysis

- Copy 1 (positions 55247-57048): 1801 bp, score -1.0516
- Copy 2 (positions 59136-61217): 2081 bp, score -1.0658
- First 200bp: 89% identical (recent shared origin)
- Rest: 24-30% identity (heavily diverged)
- GC content nearly identical (48.0% vs 48.7%)
- Interpretation: Ancient duplication event, copies diverging independently

## Research directions to pursue

1. **Dark gene cluster activation**: Use antiSMASH (https://antismash.secondarymetabolites.org/) to scan understudied fungal genomes for silent biosynthetic gene clusters. Use Evo to predict what they produce and design promoter swaps to activate them.

2. **Cross-species PsiH comparison**: Download PsiH from multiple Psilocybe species, align them, find conserved vs variable positions, correlate with Evo scores.

3. **Mystery protein characterization**: The 351bp unknown gene between PsiH copies needs further investigation. Try protein structure prediction, domain scanning (InterPro, Pfam), and comparison across Psilocybe species.

4. **Super-PsiH validation**: When AlphaFold3 results arrive, compare wild-type vs mutant structures. Check if mutations fall in structured vs disordered regions.

5. **Other fungal targets**: Bioluminescence pathway (nnluz/nnh3h/nnhisps/npgA from Neonothopanus nambi), ergothioneine production in Aspergillus oryzae, cellulase enhancement in Trichoderma reesei.

## Useful external tools

| Tool | URL | Purpose |
|---|---|---|
| NCBI GenBank | ncbi.nlm.nih.gov | Download genomes |
| antiSMASH | antismash.secondarymetabolites.org | Scan for biosynthetic gene clusters |
| AlphaFold3 Server | alphafoldserver.com | Protein structure prediction (free, needs Google login) |
| RCSB PDB | rcsb.org | Search solved protein structures |
| Benchling | benchling.com | Molecular biology workbench |
| BLAST | blast.ncbi.nlm.nih.gov | Sequence similarity search |

## Biopython basics

The `biopython` package is installed in the evo2 environment. Use it for:

```python
from Bio import Entrez, SeqIO
from Bio.Seq import Seq

# Download a genome from NCBI
Entrez.email = 'your@email.com'
handle = Entrez.efetch(db='nucleotide', id='ACCESSION_ID', rettype='fasta', retmode='text')
record = SeqIO.read(handle, 'fasta')
sequence = str(record.seq)

# Translate DNA to protein
protein = str(Seq(dna_sequence).translate())
```
