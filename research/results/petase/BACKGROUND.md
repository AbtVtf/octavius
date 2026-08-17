# PETase Engineering: Background and Literature Review

## 1. Discovery of Enzymatic PET Degradation

Poly(ethylene terephthalate) (PET) is one of the most widely produced synthetic polymers, used in beverage bottles, textiles, and packaging. Its resistance to biodegradation made it a persistent environmental pollutant until Yoshida et al. (2016) isolated *Ideonella sakaiensis* strain 201-F6 from a PET bottle recycling site in Sakai, Japan. This bacterium uses PET as a primary carbon and energy source by secreting two cooperating enzymes:

- **PETase** (ISF6_4831): hydrolyzes PET to MHET, BHET, and TPA
- **MHETase** (ISF6_0224): hydrolyzes MHET to TPA and ethylene glycol (EG)

Complete depolymerization to TPA and EG monomers enables closed-loop chemical recycling — recovered monomers can be repolymerized to virgin-quality PET.

> Yoshida S, Hiraga K, Takehana T, et al. "A bacterium that degrades and assimilates poly(ethylene terephthalate)." *Science* 351(6278):1196–1199 (2016). DOI: 10.1126/science.aad6359

## 2. Wild-Type IsPETase

### Sequence and Structure

| Property | Value |
|---|---|
| Organism | *Ideonella sakaiensis* 201-F6 |
| Gene | ISF6_4831 |
| UniProt | [A0A0K8P6T7](https://www.uniprot.org/uniprotkb/A0A0K8P6T7) |
| GenBank | GAP38373.1 |
| PDB | 5XJH (1.5 Å), 6QGC, 6ILW |
| Length | 290 aa (prepro); ~261 aa mature |
| Signal peptide | ~28–29 residues N-terminal |
| Catalytic triad | Ser160–His237–Asp206 |
| Fold | α/β-hydrolase superfamily |
| Molecular weight | ~32 kDa (mature) |

### Biochemical Properties

- **Tm**: ~41°C
- **Optimal temperature**: 30–40°C
- **pH optimum**: 7–8
- Active on amorphous PET (<30% crystallinity); greatly reduced activity on semicrystalline PET
- Structurally similar to cutinases but with a wider active-site cleft permitting PET binding

### Key Structural Features

- The **β6-β7 connecting loop** borders the substrate-binding groove and is unique to IsPETase relative to cutinases
- Two "wobble" residues flanking the catalytic serine (W185, S238) define substrate selectivity
- An **oxyanion hole** (two backbone NH groups) stabilizes the tetrahedral intermediate

> Joo S, et al. "Structural insight into molecular mechanism of PET degradation." *Nat Commun* 9:382 (2018). DOI: 10.1038/s41467-018-02881-1

## 3. Engineered IsPETase Variants

### 3.1 IsPETase W159H/S238F (Austin et al., 2018)

**Mutations**: W159H, S238F

Structural analysis revealed that narrowing the active-site cleft by introducing bulkier residues improves substrate binding. S238F enables π-stacking with the PET aromatic ring.

- **Activity**: 1.7× improvement over wild-type
- **PDB**: 7OSB
- **Addgene**: #112203

> Austin HP, et al. *PNAS* 115(19):E4350–E4357 (2018). DOI: 10.1073/pnas.1718804115

### 3.2 ThermoPETase (Son et al., 2019)

**Mutations**: S121E / D186H / R280A

Rational design targeting the flexible β6-β7 loop. Each mutation introduces stabilizing hydrogen bonds:

| Mutation | Effect |
|---|---|
| S121E | 2 intramolecular + 2 extramolecular H-bonds |
| D186H | 2 additional H-bonds with water molecules |
| R280A | Relieves steric strain, H-bond with Asn277 |

- **Tm**: ~50°C (+8.8°C vs WT)
- **Activity**: 14× improvement at 40°C
- **PDB**: 6IJ6

This three-mutation scaffold became the basis for FAST-PETase.

> Son HF, et al. *ACS Catal* 9(4):3519–3526 (2019). DOI: 10.1021/acscatal.9b00568

### 3.3 DuraPETase (Cui et al., 2021)

**Mutations** (10 total): L117F / Q119Y / T140D / W159H / G165A / I168R / A180I / S188Q / S214H / R280A

Designed using the **GRAPE** (Greedy Accumulated strategy for Protein Engineering) computational approach. Screened 253 predicted mutations → 21 confirmed stabilizing → combinatorial assembly.

Stabilization mechanisms:
- Hydrophobic core packing (L117F, Q119Y, A180I, S214H, R280A)
- New electrostatic interactions (I168R)
- Entropy reduction (G165A — restricts glycine flexibility)
- Additional hydrogen bonds (W159H, S188Q, T140D)

- **Tm**: ~72°C (+31°C vs WT)
- **Activity**: >300× on semicrystalline PET (30% crystallinity)
- **PDB**: 6KY5

> Cui Y, et al. *ACS Catal* 11(3):1340–1350 (2021). DOI: 10.1021/acscatal.0c05126

### 3.4 FAST-PETase (Lu et al., 2022)

**Full name**: Functional, Active, Stable, and Tolerant PETase

**Mutations** (5 total): S121E / D186H / R280A / N233K / R224Q

Three mutations from ThermoPETase scaffold (S121E, D186H, R280A) plus two from machine learning (N233K, R224Q). A structure-based ML algorithm trained on 19 single-point mutants predicted beneficial substitutions around the active site.

| Mutation | Source | Effect |
|---|---|---|
| S121E | ThermoPETase | Loop stabilization, H-bond network |
| D186H | ThermoPETase | H-bonds with water molecules |
| R280A | ThermoPETase | Steric strain relief |
| N233K | ML-predicted | Salt bridge with Glu204 |
| R224Q | ML-predicted | Restructures active-site environment |

- **Tm**: 63.3°C (+22°C vs WT)
- **Optimal temperature**: 50°C
- **Activity**: 38× vs ThermoPETase at 50°C
- **PDB**: 7SH6, 7SH7
- Complete depolymerization of 51 different postconsumer PET products in 1 week at 50°C
- Demonstrated closed-loop recycling: recovered monomers repolymerized to virgin PET

**Limitation**: Operates below PET glass transition temperature (Tg ~65–75°C), limiting effectiveness on semicrystalline PET without pretreatment.

> Lu H, et al. "Machine learning-aided engineering of hydrolases for PET depolymerization." *Nature* 604:662–667 (2022). DOI: 10.1038/s41586-022-04599-z

### 3.5 HotPETase (Bell et al., 2022)

**Mutations** (21 total, from 6 rounds of directed evolution):

S58A / S61V / R90T / K95N / Q119K / S121E / M154G / P181V / Q182M / D186H / S207R / N212K / S213E / S214Y / R224L / N233C / N241C / K252M / T270Q / R280A / S282C

Starting scaffold was ThermoPETase. Semi-rational directed evolution with high-throughput HPLC screening (~2,000 variants/round).

- **Tm**: 82.5°C (+25.7°C vs ThermoPETase)
- **Optimal temperature**: 65°C (at PET Tg)
- Per mole, releases more monomers than LCC-ICCG at 65°C
- **PDB**: 7QVH

> Bell EL, et al. *Nat Catal* 5:673–681 (2022). DOI: 10.1038/s41929-022-00821-3

### 3.6 DepoPETase (Shi et al., 2023)

**Mutations** (7 total): T88I / D186H / D220N / N233K / N246D / R260Y / S290P

Error-prone PCR directed evolution (~10,000 clones screened).

- **Tm**: WT + 23.3°C
- **Activity**: 1,407× over wild-type at 50°C
- Complete depolymerization of 7 untreated real-world PET waste samples
- Demonstrated at 19.1 g PET in 1 L reactor

> Shi L, et al. *Angew Chem Int Ed* 62(5):e202218390 (2023). DOI: 10.1002/anie.202218390

## 4. Leaf-Branch Compost Cutinase (LCC) and Variants

### 4.1 Wild-Type LCC

| Property | Value |
|---|---|
| Origin | Metagenomic library from leaf-branch compost |
| UniProt | [G9BY57](https://www.uniprot.org/uniprotkb/G9BY57) |
| PDB | 4EB0 |
| Catalytic triad | Ser165–His242–Asp210 |
| Tm | ~84.7°C |
| Optimal temp | 65–70°C |

A thermophilic cutinase naturally active near PET's glass transition temperature, making it more industrially relevant than mesophilic IsPETase despite lower intrinsic PET specificity.

### 4.2 LCC-ICCG (Tournier et al., 2020)

**Mutations**: F243**I** / D238**C** / S283**C** / Y127**G** (acronym: ICCG)

| Mutation | Effect |
|---|---|
| D238C / S283C | Disulfide bridge; Tm +9.8°C; replaces structural Ca²⁺ binding |
| F243I | Eliminates steric clash from D238C; increases PET activity |
| Y127G | Opens substrate-binding cleft entrance |

Identified by screening 209 variants; 25 showed >75% improved activity.

- **Tm**: 94.5°C
- **Optimal temperature**: 72°C (above PET Tg)
- 90% PET depolymerization in 10 hours; 97.3% in 24 hours
- No Ca²⁺ requirement
- **PDB**: 6THT (1.14 Å), 7VVE, 7W44

**Industrial significance**: This is the enzyme powering Carbios' industrial PET biorecycling process (C-ZYME). Demonstration plant launched Sept 2021 at Clermont-Ferrand, France (20 m³ reactor, ~100,000 bottles/cycle). Industrial plant (50,000 tonnes/year) under construction in Longlaville, France, targeting full operation by 2027.

> Tournier V, et al. "An engineered PET depolymerase to break down and recycle plastic bottles." *Nature* 580:216–219 (2020). DOI: 10.1038/s41586-020-2149-4

## 5. BhrPETase and TurboPETase

### 5.1 BhrPETase (HR-PETase)

- **Origin**: Thermophilic bacterium HR29 (metagenomic)
- **GenBank**: GBD22443.1
- **PDB**: 7EOA
- **Tm**: ~95.6°C — highest native thermostability among characterized PET hydrolases

### 5.2 TurboPETase (Shi et al., 2024)

**Mutations** (8, relative to BhrPETase): H218S / F222I / A209R / D238K / A251C / A281C / W104L / F243T

AI/Transformer-based approach: language model trained on homologous PET hydrolase sequences predicted positions where the wild-type residue was suboptimal.

- **Tm**: ~84°C
- **Optimal**: 65–68°C, pH 8.0
- Near-complete depolymerization in 8 hours at 200 g PET/kg reaction (industrially relevant solids loading)
- Outperforms HotPETase, DepoPETase, FAST-PETase, and LCC-ICCG under high-solids conditions
- **Addgene**: #218693

> Shi L, et al. *Nat Commun* 15:1688 (2024). DOI: 10.1038/s41467-024-45662-9

## 6. Summary of Engineering Strategies

| Strategy | Examples | Key Benefit |
|---|---|---|
| Disulfide bridge formation | LCC-ICCG (D238C/S283C), TurboPETase (A251C/A281C) | Reduces unfolding entropy |
| Hydrophobic core packing | DuraPETase (L117F, A180I) | Stabilizes protein interior |
| Salt bridges | FAST-PETase (N233K↔Glu204) | Favorable at high temperatures |
| H-bond network extension | ThermoPETase (S121E, D186H) | Loop stabilization |
| Entropy reduction | DuraPETase (G165A) | Restricts backbone flexibility |
| Machine learning | FAST-PETase, TurboPETase | Data-driven mutation prediction |
| Directed evolution | HotPETase, DepoPETase | High-throughput screening |

## 7. Variant Comparison Table

| Variant | Scaffold | # Mutations | Tm (°C) | Optimal T (°C) | Fold vs WT | Year |
|---|---|---|---|---|---|---|
| IsPETase WT | — | 0 | ~41 | 30–40 | 1× | 2016 |
| W159H/S238F | IsPETase | 2 | ~41 | 30–40 | 1.7× | 2018 |
| ThermoPETase | IsPETase | 3 | ~50 | 40 | 14× | 2019 |
| DuraPETase | IsPETase | 10 | ~72 | 37 | >300× | 2021 |
| FAST-PETase | IsPETase | 5 | 63.3 | 50 | ~530× | 2022 |
| HotPETase | IsPETase | 21 | 82.5 | 65 | High | 2022 |
| DepoPETase | IsPETase | 7 | ~64 | 50 | 1,407× | 2023 |
| LCC WT | LCC | 0 | 84.7 | 65–70 | — | 2012 |
| LCC-ICCG | LCC | 4 | 94.5 | 72 | Industrial | 2020 |
| BhrPETase | native | 0 | 95.6 | — | — | — |
| TurboPETase | BhrPETase | 8 | 84 | 65–68 | Industrial | 2024 |

## 8. Downloaded Sequences

All sequences saved to `research/data/petase/`:

| File | Content | Source |
|---|---|---|
| `IsPETase_wildtype.fasta` | IsPETase WT (A0A0K8P6T7) | UniProt |
| `FAST-PETase.fasta` | FAST-PETase (7SH6) | PDB |
| `ThermoPETase.fasta` | ThermoPETase (6IJ6) | PDB |
| `DuraPETase.fasta` | DuraPETase (6KY5) | PDB |
| `HotPETase.fasta` | HotPETase (7QVH) | PDB |
| `LCC_wildtype.fasta` | LCC WT (G9BY57) | UniProt |
| `LCC-ICCG.fasta` | LCC-ICCG (6THT) | PDB |
| `BhrPETase.fasta` | BhrPETase/HR-PETase (7EOA) | PDB |

## 9. Key References

1. Yoshida S, et al. *Science* 351:1196–1199 (2016). — IsPETase discovery
2. Austin HP, et al. *PNAS* 115:E4350–E4357 (2018). — W159H/S238F variant
3. Joo S, et al. *Nat Commun* 9:382 (2018). — Crystal structure, mechanism
4. Son HF, et al. *ACS Catal* 9:3519–3526 (2019). — ThermoPETase
5. Tournier V, et al. *Nature* 580:216–219 (2020). — LCC-ICCG, Carbios
6. Knott BC, et al. *PNAS* 117:25476–25485 (2020). — PETase-MHETase chimera
7. Cui Y, et al. *ACS Catal* 11:1340–1350 (2021). — DuraPETase
8. Lu H, et al. *Nature* 604:662–667 (2022). — FAST-PETase
9. Bell EL, et al. *Nat Catal* 5:673–681 (2022). — HotPETase
10. Shi L, et al. *Angew Chem Int Ed* 62:e202218390 (2023). — DepoPETase
11. Shi L, et al. *Nat Commun* 15:1688 (2024). — TurboPETase

## 10. Accession Quick Reference

| Enzyme | UniProt | GenBank | PDB |
|---|---|---|---|
| IsPETase WT | A0A0K8P6T7 | GAP38373.1 | 5XJH, 6QGC, 6ILW |
| ThermoPETase | — | — | 6IJ6 |
| DuraPETase | — | — | 6KY5 |
| FAST-PETase | — | — | 7SH6, 7SH7 |
| HotPETase | — | — | 7QVH |
| LCC WT | G9BY57 | — | 4EB0 |
| LCC-ICCG | — | — | 6THT, 7VVE |
| BhrPETase | — | GBD22443.1 | 7EOA |
