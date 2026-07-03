#!/usr/bin/env bash
# Build the paper: Markdown -> standalone LaTeX -> PDF (tectonic).
# Produces paper/paper.tex (editable LaTeX source) and paper/paper.pdf.
set -euo pipefail
cd "$(dirname "$0")/.."

# --shift-heading-level-by=-1 promotes the leading `# Title` heading to the
# document title and lifts every real section to top level.
# headings already carry their own numbers ("1. Abstract"), so no --number-sections.
pandoc paper/paper.md \
  --standalone --toc \
  --shift-heading-level-by=-1 \
  --syntax-highlighting=tango \
  -H paper/preamble.tex \
  -M date="$(date +'%B %Y')" \
  -V documentclass=article \
  -V fontsize=11pt \
  -V geometry:margin=1in \
  -V colorlinks=true -V linkcolor=blue -V urlcolor=blue \
  -o paper/paper.tex

tectonic paper/paper.tex --outdir paper >/dev/null 2>&1
echo "wrote paper/paper.tex and paper/paper.pdf"
ls -l paper/paper.tex paper/paper.pdf
