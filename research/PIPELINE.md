# Octo Corp Research Pipeline

## Tools Available

All tools are in `research/tools/`. Run from the project root.

### 1. Evo2 — DNA Analysis & Design
**Requires**: `source ~/evo2-env/bin/activate` before running

```bash
# Score DNA sequences (higher = more natural)
python research/tools/evo2_score.py --fasta research/data/gene.fasta --output research/results/scores.json

# Mutation scan — find beneficial/damaging mutations
python research/tools/evo2_mutate.py --fasta research/data/gene.fasta --start 0 --end 500 --output research/results/mutations.json

# Generate synthetic DNA from a prompt
python research/tools/evo2_generate.py --prompt "ATGCGATCG" --tokens 500 --num 5 --output research/results/generated.fasta
```

### 2. Boltz-1 — Protein Structure Prediction
```bash
# Fold a protein
python research/tools/boltz_fold.py --fasta research/data/protein.fasta --output-dir research/results/structures/

# Fold from sequence directly
python research/tools/boltz_fold.py --seq "MKTLLLT..." --name super_psih --output-dir research/results/structures/
```

## Data

- `research/data/` — input files (FASTA, sequences)
- `research/results/` — output files (JSON scores, PDB structures)
- Each project should create its own subdirectory:
  - `research/results/psilocybin/`
  - `research/results/algae_biofuel/`
  - etc.

## Project Structure

Each research project in Paperclip should have:
1. A vision document (created by Octo)
2. A research plan (created by Atlas)
3. Specific experiment tasks (executed by Nova)

Tasks should reference these tools by exact command.

## Hardware Notes

- GPU: RTX 3090 24GB
- Evo2 needs ~14GB VRAM — unload F5-TTS before running
- Boltz-1 needs ~8-12GB VRAM
- Always activate the right venv before running Evo2: `source ~/evo2-env/bin/activate`
