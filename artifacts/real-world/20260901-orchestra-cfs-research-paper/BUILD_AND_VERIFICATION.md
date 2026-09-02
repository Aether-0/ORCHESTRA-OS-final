# Build and verification record

## Render command

From this directory:

```text
pdflatex -interaction=nonstopmode -halt-on-error -file-line-error ORCHESTRA_OS_CFS_COMPARATIVE_RESEARCH_PAPER.tex
pdflatex -interaction=nonstopmode -halt-on-error -file-line-error ORCHESTRA_OS_CFS_COMPARATIVE_RESEARCH_PAPER.tex
```

Both invocations returned zero. The second pass resolves cross-references and
citations. The output is a seven-page PDF with embedded vector graphs and
flowcharts.

## Verification performed

- `pdfinfo` confirms the title, author form `S.W. ZAW`, subject, and final page count.
- `pdftotext` confirms the abstract, tables, figures, references, quantitative
  values, and final claim are present.
- Pages containing the evidence-chain, simulation, fixed-work, foreground,
  service-cost, bottleneck, and field-fit figures were rendered and visually
  inspected.
- No forbidden project label or full-name author form appears in the manuscript
  source, PDF text, provenance, or compact evidence package.
- The parameter register is included and its CFS host snapshot is labelled as a
  baseline observation rather than a semantic equivalence claim.
- `CHECKSUMS.sha256` records the final artifact digests, excluding the checksum
  file itself and transient LaTeX auxiliary files.

LaTeX reports benign underfull boxes from narrow evidence tables and deliberate
two-column page balancing; no content is clipped. There are no overfull boxes
or unresolved references in the final render.
