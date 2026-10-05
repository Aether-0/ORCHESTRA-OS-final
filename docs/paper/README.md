# Paper and evidence guide

## Manuscript

**Signal-Coordinated Process Scheduling on Linux sched_ext: Design,
Implementation, and Bounded Evaluation of ORCHESTRA-OS**

Authors in the supplied manuscript: Geetha Ganesan, S. W. Zaw, and
Panchabi Vaithiyanathan. The authors report submission to IEEE Access;
submission does not establish acceptance or publication. The submitted text
and a final article DOI are not included in this repository. Use the software
citation in [CITATION.cff](../../CITATION.cff) for this release.

The earlier architecture/simulation study and this hardware evaluation are
separate evidence layers. The public [ORCHESTRA OS simulation dataset on
Figshare](https://doi.org/10.6084/m9.figshare.32925431.v1) contains the earlier
simulation materials; it is not the DOI for the submitted hardware paper or
a verified deposit of the September hardware supplement.

## Reading order

1. [Evidence map and interpretation](EVIDENCE.md).
2. [Reproduction and integrity checks](REPRODUCIBILITY.md).
3. [Data dictionary](DATA_DICTIONARY.md) and [original calculation record](ARCHIVED_CALCULATIONS.md).
4. [Original table-package provenance](ARCHIVED_PROVENANCE.md).
5. [Current source-to-research mapping](../research/RESEARCH_TO_CODE.md) and [limitations](../../LIMITATIONS.md).

## What is included

`data/` contains byte-preserved tables from the prepared 18 September 2026
hardware evidence package, including foreground pairs, fixed-work raw rows,
action outcomes, stress, integrity, security, and negative checklist results.
`CHECKSUMS.sha256` covers the copied data and archived supporting documents.
The historical package's single-author provenance describes that package;
it does not replace the submitted manuscript's author list.

`protocol/` retains the frozen three-condition protocol, randomization
schedule, target manifest, and provenance recheck. These are archived design
records, not completed three-condition results. Some referenced fixtures and
campaign files remain in the full author-held supplement; the selected files
here do not constitute a ready-to-run complete benchmark bundle.

The current software release differs from the artifacts evaluated in the
paper. Do not attribute historical measurements to the latest code or rebuild
an old experiment with the current source and call it an exact reproduction.
Historical reports and failures remain under [artifacts](https://github.com/Aether-0/ORCHESTRA-OS-final/blob/v1.0.4/artifacts/README.md)
and [docs/history](../history/README.md).

Publication checks are recorded in the [1.0.3 audit](../validation/PUBLICATION_1_0_3.md).

## Submitted manuscript provenance

The supplied manuscript `Orchestra final 5th oct.pdf` cites
`https://github.com/Aether-0/ORCHESTRA-OS-final` in reference [25]. Its SHA-256 is
`715f3ba7dd1ce74ec72c4c2ffad1fb9d71407f8e4820ab2190f3db391959b168`.
The manuscript itself is not bundled with the source release.
