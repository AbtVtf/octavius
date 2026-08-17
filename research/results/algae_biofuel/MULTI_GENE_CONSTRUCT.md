# Multi-Gene Construct: DGAT1 + PDAT1 + STA6 CRISPRi

## Single-Vector Lipid Overproduction System for *C. reinhardtii*

**Prepared by:** Atlas, Research Director, Octo Corp
**Date:** 2026-04-08
**Reference:** OCT-11 (parent: OCT-1 algae genetic enhancement protocol)

---

## 1. Design Rationale

Individual genetic modifications yield modest lipid increases (2-5x). Combining
three complementary strategies in a single transformation vector maximizes
lipid accumulation by simultaneously:

1. **Pushing flux into TAG** — overexpress DGAT1 (rate-limiting final step)
2. **Broadening TAG substrate supply** — overexpress PDAT1 (converts membrane phospholipids to TAG)
3. **Blocking competing carbon sink** — CRISPRi knockdown of STA6 (starch synthesis)

Single-vector design simplifies transformation, ensures co-segregation of all
components, and avoids the need for multiple selectable markers.

---

## 2. Construct Map

```
                          MULTI-GENE LIPID CONSTRUCT (~8.5 kb total insert)
 ┌──────────────────────────────────────────────────────────────────────────────────────────┐
 │                                                                                          │
 │  CASSETTE 1: OPTIMIZED DGAT1 OVEREXPRESSION (~1,650 bp)                                 │
 │  ┌─────────────────┬──────────────────────────┬───────────────┐                          │
 │  │ generated_6      │ DGAT1 CDS (optimized)    │ RBCS2 3'UTR   │                          │
 │  │ promoter (500bp) │ 5×AA + 5×syn muts (846bp)│ + polyA (300bp)│                         │
 │  │ Evo2: -0.308     │ M163T,R113P,V153L,       │               │                          │
 │  │                  │ T148A,A149D + codon opt   │               │                          │
 │  └─────────────────┴──────────────────────────┴───────────────┘                          │
 │                                                                                          │
 │  CASSETTE 2: PDAT1 OVEREXPRESSION (~1,550 bp)                                           │
 │  ┌─────────────────┬──────────────────────────┬───────────────┐                          │
 │  │ HSP70A/RBCS2     │ PDAT1 CDS (576 bp)       │ β2-tubulin    │                          │
 │  │ tandem promoter  │ Evo2 baseline: -0.5616   │ 3'UTR (350bp) │                          │
 │  │ (350 bp)         │ (native sequence)         │               │                          │
 │  └─────────────────┴──────────────────────────┴───────────────┘                          │
 │                                                                                          │
 │  CASSETTE 3: STA6 CRISPRi (~2,800 bp)                                                   │
 │  ┌─────────────────┬──────────────────────────┬───────────────┐                          │
 │  │ HSP70A/RBCS2     │ dCas9-SRDX fusion        │ RBCS2 3'UTR   │                          │
 │  │ promoter (350bp) │ (H840A/D10A, ~4.2 kb*)   │ (300 bp)      │                          │
 │  └─────────────────┴──────────────────────────┴───────────────┘                          │
 │  ┌─────────────────┬──────────────────────────┐                                          │
 │  │ CrU6-1 promoter  │ STA6-g1 spacer + scaffold│                                          │
 │  │ (250 bp)         │ AGGAAATGCGTGGGTGTTCG     │                                          │
 │  └─────────────────┴──────────────────────────┘                                          │
 │  ┌─────────────────┬──────────────────────────┐                                          │
 │  │ CrU6-2 promoter  │ STA6-g2 spacer + scaffold│                                          │
 │  │ (250 bp)         │ TGTTCGCGGAGAAAAAGGTC     │                                          │
 │  └─────────────────┴──────────────────────────┘                                          │
 │                                                                                          │
 │  CASSETTE 4: SELECTION MARKER (~1,800 bp)                                                │
 │  ┌─────────────────┬──────────────────────────┬───────────────┐                          │
 │  │ HSP70A/RBCS2     │ AphVIII (paromomycin     │ RBCS2 3'UTR   │                          │
 │  │ promoter (350bp) │ resistance, 795 bp)      │ (300 bp)      │                          │
 │  └─────────────────┴──────────────────────────┴───────────────┘                          │
 │                                                                                          │
 └──────────────────────────────────────────────────────────────────────────────────────────┘

 * dCas9 CDS is large (~4.2 kb). Total construct with backbone: ~12-13 kb.
   Use a high-capacity backbone (pBR322-derived or BAC) if standard plasmid limits apply.
```

---

## 3. Component Details

### 3.1 Cassette 1 — Optimized DGAT1

Source: OCT-1 modification blueprint (all phases complete).

**Promoter:** generated_6 synthetic promoter (500 bp)
- Evo2 score: -0.308 (vs -1.324 native; Δ = +1.016)
- Contains tandem repeat architecture — recombination risk; test generated_7 (Δ+0.511) as backup

**CDS:** DGAT1 with 10 point mutations (5 non-synonymous + 5 synonymous)

Non-synonymous (Tier 1 STABLE):
| Mutation | DNA Position | AA Change | Evo2 Δ |
|----------|-------------|-----------|--------|
| M163T | bp 806 (T→C) | Met→Thr | +0.006333 |
| R113P | bp 656 (G→C) | Arg→Pro | +0.006278 |
| V153L | bp 775 (G→C) | Val→Leu | +0.005810 |
| T148A | bp 760 (A→G) | Thr→Ala | +0.003704 |
| A149D | bp 764 | Ala→Asp | +0.003117 |

Synonymous codon optimization:
| DNA Position | WT→Opt | Evo2 Δ |
|-------------|--------|--------|
| 691 | A→C | +0.000988 |
| 408 | G→C | +0.000988 |
| 640 | C→T | +0.000986 |
| 676 | T→A | +0.000986 |
| 486 | C→G | +0.000978 |

**Terminator:** RBCS2 3'UTR (well-characterized in C. reinhardtii)

### 3.2 Cassette 2 — PDAT1 Overexpression

PDAT1 (Cre02.g106400) catalyzes an alternative TAG synthesis pathway:
phospholipid + DAG → TAG + lysophospholipid

This is complementary to DGAT1 because:
- PDAT1 uses membrane phospholipids as acyl donors (not acyl-CoA like DGAT1)
- Under nitrogen stress, membrane remodeling releases phospholipids
- PDAT1 channels these directly to TAG, preventing wasteful phospholipid degradation

**CDS:** Native PDAT1 sequence (576 bp, Evo2 score: -0.5616)
- Already has the highest baseline Evo2 fitness of all scored genes
- Native sequence recommended — optimization has lower marginal benefit here

**Promoter:** HSP70A/RBCS2 tandem promoter
- Well-validated constitutive promoter for C. reinhardtii
- Different from generated_6 to avoid repeat-mediated recombination between cassettes

**Terminator:** β2-tubulin 3'UTR
- Different from RBCS2 3'UTR used in Cassette 1 to avoid repeat-mediated deletion

### 3.3 Cassette 3 — STA6 CRISPRi

Source: OCT-1 modification blueprint section 6.

**dCas9-SRDX:** Catalytically dead SpCas9 (H840A + D10A) fused to SRDX repressor domain
- Codon-optimized for C. reinhardtii (high-GC usage)
- SRDX (EAR motif: LDLDLELRLGFA) — proven transcriptional repressor in algae
- Driven by HSP70A/RBCS2 promoter for constitutive expression

**Guide RNAs (dual-guide for robust knockdown):**
- STA6-g1: `AGGAAATGCGTGGGTGTTCG` (PAM: CGG, position +17-36, primary)
- STA6-g2: `TGTTCGCGGAGAAAAAGGTC` (PAM: CGG, position +30-49, secondary)
- Each under independent CrU6 snRNA promoters (Pol III)
- Target the non-template strand of early STA6 CDS for maximal elongation block

### 3.4 Cassette 4 — Selection Marker

**AphVIII** — aminoglycoside phosphotransferase conferring paromomycin resistance
- Standard C. reinhardtii selectable marker
- Selection: 10 µg/mL paromomycin on TAP agar

---

## 4. Design Principles

### 4.1 Avoiding Repeat-Mediated Recombination

Critical for construct stability in the GC-rich C. reinhardtii genome:

| Element | Cassette 1 | Cassette 2 | Cassette 3 | Cassette 4 |
|---------|-----------|-----------|-----------|-----------|
| Promoter | generated_6 | HSP70A/RBCS2 | HSP70A/RBCS2 | HSP70A/RBCS2 |
| 3'UTR | RBCS2 | β2-tubulin | RBCS2 | RBCS2 |

**Risk:** Cassettes 2, 3, 4 share the HSP70A/RBCS2 promoter and some share RBCS2 3'UTR.
This creates recombination hotspots.

**Mitigations:**
1. Use variant promoters where possible (e.g., PsaD promoter for Cassette 2 instead of HSP70A/RBCS2)
2. Alternate 3'UTR elements (β2-tubulin, NIT1, FDX1) to minimize repeats
3. Insert unique spacer sequences (50-100 bp) between cassettes
4. Screen transformants by Southern blot for full-length integration

**Revised promoter/terminator assignments (recommended):**

| Cassette | Promoter | 3'UTR |
|----------|---------|-------|
| 1 (DGAT1) | generated_6 (unique) | RBCS2 |
| 2 (PDAT1) | PsaD (Cre09.g412100) | β2-tubulin |
| 3 (dCas9) | HSP70A/RBCS2 | NIT1 |
| 3 (guides) | CrU6-1, CrU6-2 | N/A (Pol III terminator) |
| 4 (AphVIII) | RBCS2 minimal | FDX1 |

This eliminates all shared promoter/terminator sequences between cassettes.

### 4.2 Cassette Orientation

Arrange cassettes in alternating orientations to reduce read-through interference:

```
→ DGAT1 → ← PDAT1 ← → dCas9/guides → ← AphVIII ←
```

### 4.3 Insulator Elements

Consider flanking the entire construct with MAR (matrix attachment region) elements
from C. reinhardtii (e.g., from the RBCS2 locus) to:
- Reduce position effects at the integration site
- Stabilize expression across cell divisions
- Prevent silencing of the multi-gene array

---

## 5. Vector Backbone Options

| Backbone | Capacity | Features | Recommendation |
|----------|---------|---------|----------------|
| pGenD-derived | ~10 kb insert | Standard Chlamy vector; well-validated | Tight fit (~12-13 kb insert); may need truncation |
| pOptimized | ~12 kb insert | Improved expression cassette architecture | Good fit; preferred |
| BAC-based | >50 kb | High capacity; lower copy number | Overkill unless additional cassettes planned |
| Linear fragment | Unlimited | No bacterial propagation needed | Synthesize as 2-3 overlapping fragments; assemble by Gibson/HiFi |

**Recommendation:** Synthesize the entire construct as 3 overlapping fragments (~4-5 kb each)
and assemble by Gibson Assembly or NEBuilder HiFi. Transform the linearized assembled
product directly into C. reinhardtii by electroporation — no circular vector needed for
random nuclear integration.

---

## 6. Assembly Strategy

### 6.1 Fragment Design for Gibson Assembly

```
Fragment 1 (~4.0 kb):
  [MAR-L] → [generated_6 promoter] → [opt-DGAT1 CDS] → [RBCS2 3'UTR] → [overlap]

Fragment 2 (~4.5 kb):
  [overlap] → [PsaD promoter] → [PDAT1 CDS] → [β2-tub 3'UTR] →
  [CrU6-1::STA6-g1] → [CrU6-2::STA6-g2] → [overlap]

Fragment 3 (~4.5 kb):
  [overlap] → [HSP70A/RBCS2 promoter] → [dCas9-SRDX] → [NIT1 3'UTR] →
  [RBCS2min promoter] → [AphVIII] → [FDX1 3'UTR] → [MAR-R]
```

Overlap regions: 30-40 bp for efficient Gibson Assembly.

### 6.2 Synthesis & Assembly Workflow

1. Order synthesis of 3 fragments from IDT (gBlocks MAX, up to 5 kb) or Twist Bioscience
2. Gibson Assembly: combine equimolar fragments with NEBuilder HiFi DNA Assembly Master Mix
3. Verify by restriction digest and Sanger sequencing of junction regions
4. (Optional) Clone into pUC19 for propagation, then linearize with SacI/KpnI
5. Transform linearized construct into C. reinhardtii CC-400 by electroporation

---

## 7. Expected Performance

| Metric | DGAT1 alone | DGAT1 + PDAT1 | DGAT1 + PDAT1 + STA6-KD |
|--------|------------|---------------|-------------------------|
| TAG fold-change (vs WT) | 3-5× | 5-8× | **8-15×** |
| TAG % dry weight (N-replete) | 8-15% | 12-20% | **20-30%** |
| TAG % dry weight (N-starved) | 30-40% | 35-50% | **50-65%** |
| Growth rate impact | ≤10% | ≤15% | ≤25% |

**Key synergies:**
- DGAT1 and PDAT1 use different acyl donors (acyl-CoA vs phospholipid), so both pathways
  operate simultaneously without competing for substrate
- STA6 knockdown frees carbon skeletons that would otherwise be locked in starch granules,
  increasing the pool of glycerol-3-phosphate and acyl-CoA available to both DGAT1 and PDAT1
- The three-way combination should be multiplicative, not just additive

---

## 8. Quality Control Checkpoints

| Step | QC Method | Pass Criteria |
|------|----------|---------------|
| Fragment synthesis | Sequence verification | 100% match to design |
| Gibson assembly | Restriction digest (BamHI/EcoRI diagnostic) | Expected band pattern |
| Junction verification | Sanger sequencing (6 primers across 5 junctions) | No frameshifts, no deletions |
| Transformation | Colony count on paromomycin plates | ≥50 colonies per µg DNA |
| Integration check | PCR with cassette-specific primers | All 4 cassettes detected |
| Full-length verification | Southern blot with probe cocktail | Single band at expected size |
| Expression validation | RT-qPCR for DGAT1, PDAT1, STA6 | DGAT1 ≥5×, PDAT1 ≥3×, STA6 ≤30% WT |
| Functional validation | Nile Red + GC-MS | TAG ≥5× WT (N-replete) |

---

## 9. Risk Matrix

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| Repeat-mediated recombination | Medium | High | Unique promoters/terminators per cassette |
| Gene silencing of multi-gene array | Medium | High | MAR insulators; screen many transformants |
| dCas9 toxicity | Low | Medium | Moderate expression; inducible promoter backup |
| generated_6 promoter instability | Medium | Medium | Test generated_7 in parallel construct |
| Construct too large for efficient transformation | Low | Medium | Gibson assembly of overlapping fragments |
| Off-target CRISPRi effects | Low | Medium | Computational off-target screen; phenotype controls |

---

*Construct design based on data from OCT-1 through OCT-5 (algae genetic enhancement protocol).
All Evo2 scores, mutation data, and CRISPRi guides sourced from the Phase 1-5 experimental results.*
