# Fungal Bioluminescence Pathway — Literature Background

## Overview

The fungal bioluminescence system of *Neonothopanus nambi* is a self-contained,
genetically encodable light-emitting pathway that converts caffeic acid to green
photons (~520 nm) through a four-enzyme cycle. First fully characterized by
Kotlobay et al. (2018, PNAS), it has since been expressed heterologously in yeast,
plants, and mammalian cells, making it a leading candidate for autonomous
bioluminescence engineering.

## The Core Pathway

### Biochemical Cycle

```
Caffeic acid + 2 Malonyl-CoA
       │ (nnHispS — hispidin synthase, type I PKS)
       ▼
    Hispidin
       │ (nnH3H — hispidin-3-hydroxylase, FAD monooxygenase)
       ▼
 3-Hydroxyhispidin (luciferin)
       │ (nnLuz — luciferase) + O₂
       ▼
 Caffeylpyruvate (oxyluciferin) + LIGHT (~520 nm green)
       │ (nnCPH — caffeylpyruvate hydrolase)
       ▼
 Caffeic acid + Pyruvate  ← cycle closes
```

### The Four Core Genes

| Gene | Accession (CDS) | Protein | Function | Size |
|------|-----------------|---------|----------|------|
| nnHispS | LC435355 | Hispidin synthase | Condenses caffeic acid + malonyl-CoA → hispidin | ~1700 aa; ~5.1 kbp |
| nnH3H | LC435367 | Hispidin-3-hydroxylase | Hydroxylates hispidin → 3-hydroxyhispidin (luciferin) | ~400–450 aa |
| nnLuz | LC435379 | Luciferase | Oxidizes luciferin → light + caffeylpyruvate | 267 aa (~28–31 kDa) |
| nnCPH | LC435389 | Caffeylpyruvate hydrolase | Cleaves oxyluciferin → caffeic acid + pyruvate | ~300 aa |

### The Fifth Gene: npgA

**npgA** encodes a 4'-phosphopantetheinyl transferase (PPTase) from *Aspergillus
nidulans*. It is **essential** in all heterologous hosts tested because nnHispS is a
type I polyketide synthase (PKS) that requires post-translational
phosphopantetheinylation of its acyl carrier protein (ACP) domain to become active.

*S. cerevisiae* has an endogenous PPTase (Lys5/Ppt2), but it is substrate-selective
and does not efficiently activate fungal type I PKS enzymes. Without npgA, no
hispidin is produced and the pathway stalls completely (Shakhova et al. 2024:
"expression of phosphopantetheinyl transferase was absolutely required for light
emission in animal and yeast hosts").

**Workaround:** Plant type III PKSs (PpASCL, PzPKS2, HmS) can replace nnHispS and
do not require phosphopantetheinylation, eliminating the need for npgA entirely
(Palkina et al. 2024, Science Advances).

## Key Literature

### Discovery: Kotlobay et al. 2018

**Citation:** Kotlobay AA, Sarkisyan KS, Mokrushina YA, et al. "Genetically
encodable bioluminescent system from fungi." *PNAS* 115(50):12728–12732.
DOI: 10.1073/pnas.1803615115. PMID: 30478037.

- Identified the complete 4-gene pathway from *N. nambi*
- Demonstrated autonomous light emission in *Pichia pastoris* with caffeic acid supplementation
- Light visible to the naked eye (photographed with 8-second exposure at ISO 1600)
- Also achieved substrate-free luminescence by adding tyrosine → caffeic acid biosynthesis genes
- Genomic data deposited under NCBI BioProject PRJNA476325

### Chemical Mechanism: Purtov et al. 2015

**Citation:** Purtov KV, et al. "The Chemical Basis of Fungal Bioluminescence."
*Angew. Chem. Int. Ed.* 54(28):8124–8128. DOI: 10.1002/anie.201501779.

- Established that 3-hydroxyhispidin is the universal fungal luciferin
- Reaction proceeds via O₂ cycloaddition → α-pyrone endoperoxide → decarboxylation → light
- Enzymatic hydrolysis of oxyluciferin regenerates caffeic acid

### Enzyme Characterization: Gorokhovatsky et al. 2021

**Citation:** Gorokhovatsky AY, et al. "The Recombinant Luciferase of the Fungus
Neonothopanus nambi." *Doklady Biochem. Biophys.* 496(1):52–55.
DOI: 10.1134/S1607672921010051.

Key parameters for nnLuz:
- Km = 1.09 ± 0.06 µM for 3-hydroxyhispidin
- pH optimum: 8.0
- **Temperature sensitivity: essentially inactive above 30°C** (critical for yeast culture)
- Best expression yield in *P. pastoris* (10 mg/L)

### Improved Pathway: Shakhova, Markina et al. 2024

**Citation:** Shakhova ES, Karataeva TA, Markina NM, et al. "An improved pathway
for autonomous bioluminescence imaging in eukaryotes." *Nature Methods*
21(3):406–410. DOI: 10.1038/s41592-023-02152-y.

Major advances:
- **nnLuz_v3** (T99P, T192S, A199P): increased thermostability and brightness
- **nnLuz_v4** (adds I3S, N4T, F11L, I63T): best-performing variant
- **nnH3H_v2** (D37E, V181I, S323M, M385K): improved hydroxylase activity
- **mcitHispS** from *Mycena citricolor*: outperformed nnHispS in all hosts
- FBP3 pathway (mcitHispS + nnH3H_v2 + nnLuz_v4 + nnCPH + NpgA): **10–100× brighter** than original
- Genes codon-optimized for each host species

### Hybrid Pathway: Palkina et al. 2024

**Citation:** Palkina KA, Karataeva TA, Perfilov MM, et al. "A hybrid pathway for
self-sustained luminescence." *Science Advances* 10(10):eadk1992.
DOI: 10.1126/sciadv.adk1992.

- Replaced nnHispS (~5.1 kbp type I PKS) with compact plant type III PKSs (~1.2 kbp)
- **No npgA required** — reduces total gene count from 5 to 4
- Plant PKS options: PpASCL (*P. patens*), PzPKS2 (*P. zeylanica*), HmS (*H. macrophylla*)
- In *P. pastoris*: HmS hybrid emitted 40× more light than firefly luciferase
- PzPKS2 pathway: 1–2 orders of magnitude brighter than nnHispS + NpgA

### Glowing Plants: Mitiouchkina et al. 2020

**Citation:** Mitiouchkina T, et al. "Plants with genetically encoded
autoluminescence." *Nature Biotechnology* 38(8):944–946.
DOI: 10.1038/s41587-020-0500-9.

- Stably autoluminescent tobacco plants with all 4 pathway genes
- Light emission from flowers reached 10^10 photons/min, visible to the naked eye

### H3H Biochemistry: Tong et al. 2020

**Citation:** Tong Y, et al. "Substrate binding tunes the reactivity of hispidin
3-hydroxylase." *J. Biol. Chem.* 295(50):17210–17220.
DOI: 10.1074/jbc.RA120.014996.

- Km ~5 µM for hispidin, Km ~69 µM for NADPH, kcat ~6.0 s⁻¹
- Substrate binding accelerates FAD reduction by ~100-fold

## Challenges for S. cerevisiae Expression

1. **nnLuz thermolability**: Standard yeast culture at 30°C is at the thermal
   inactivation boundary. Must culture at 25–28°C or use thermostable variants
   (nnLuz_v3/v4).

2. **nnHispS size and complexity**: At ~5.1 kbp, it is challenging to express
   efficiently. Subject to transcriptional attenuation and protein misfolding.

3. **Mandatory npgA co-expression**: S. cerevisiae Lys5 does not activate fungal
   type I PKS. npgA is required unless using plant PKS alternatives.

4. **Codon mismatch**: N. nambi genes have different codon usage than S. cerevisiae
   (which prefers A/T at wobble positions, ~38% GC). Codon optimization is essential.

5. **Caffeic acid supply**: Endogenous yeast production is insufficient. Either
   supplement media or engineer the tyrosine → caffeic acid pathway.

6. **Dim baseline output**: The original FBP1 pathway produces modest light in
   heterologous hosts. Improved variants (FBP2/FBP3) are 10–100× brighter.

## Implications for This Project

For maximum brightness in *S. cerevisiae* with minimal gene count:

- **Recommended pathway**: Plant PKS (e.g., HmS or PpASCL) + nnH3H_v2 + nnLuz_v4 + nnCPH
  - Only 4 genes, no npgA needed
  - Uses the brightest known enzyme variants

- **If using native N. nambi genes**: nnHispS + nnH3H + nnLuz + nnCPH + npgA
  - 5 genes required
  - Codon-optimize all sequences for S. cerevisiae
  - Culture at 25–28°C
  - Supplement caffeic acid or add TAL/HpaB/HpaC for autonomous production

- **Evo2 mutation scanning** (as planned in PROTOCOL.md) can identify additional
  beneficial mutations in the native gene sequences, complementing the published
  variants.

## References Summary

| Paper | Year | Journal | DOI |
|-------|------|---------|-----|
| Purtov et al. | 2015 | Angew. Chem. Int. Ed. | 10.1002/anie.201501779 |
| Kotlobay et al. | 2018 | PNAS | 10.1073/pnas.1803615115 |
| Mitiouchkina et al. | 2020 | Nat. Biotechnol. | 10.1038/s41587-020-0500-9 |
| Tong et al. | 2020 | J. Biol. Chem. | 10.1074/jbc.RA120.014996 |
| Beregovaya et al. | 2021 | Doklady Biochem. Biophys. | 10.1134/S1607672921010026 |
| Gorokhovatsky et al. | 2021 | Doklady Biochem. Biophys. | 10.1134/S1607672921010051 |
| Palkina et al. | 2023 | Int. J. Mol. Sci. | 10.3390/ijms24021317 |
| Shakhova et al. | 2024 | Nat. Methods | 10.1038/s41592-023-02152-y |
| Palkina et al. | 2024 | Sci. Adv. | 10.1126/sciadv.adk1992 |

---

*Note on the task description: The 4 "key genes" listed (nnluz, nnh3h, nnhisps, npgA) mix core
pathway genes with the auxiliary PPTase. The 4 core pathway genes are nnLuz, nnH3H, nnHispS, and
nnCPH. npgA is a required 5th gene for activation in yeast, unless plant PKS alternatives are used.*
