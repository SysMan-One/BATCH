#!/usr/bin/env python3
"""
**++
**  MODULE:		mkpdf.py
**  IDENT:		X-02-02
**  REV:		02.02.00
**
**  FACILITY:		BATCH
**
**  ABSTRACT:		Renders a manual of the subsystem as PDF from its markdown source.
**
**			The markdown follows the layout of an OpenVMS manual, where the number of a
**			chapter and its title stand on separate lines. That reads well as text and
**			badly as a document, so the two are joined here and the identification block of
**			the title page is lifted out into metadata.
**
**			The faces are named explicitly. The template of pandoc otherwise reaches for
**			Latin Modern, which this installation of TeX does not carry, and the DejaVu
**			family is present everywhere a modern system is.
**
**			A dollar sign is ubiquitous in the names of this subsystem and would otherwise
**			open mathematics in LaTeX, so that extension of the markdown reader is turned
**			off rather than the sources being made to carry escapes.
**
**  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:	29-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	29-JUL-2026	RRL	X-01-01	Initial revision.
**
**	18-OCT-2026	RRL	X-02-02	Revision raised to X-02-02 together with the product; no change of its own.
**--
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

#  The reader: markdown without the extensions that would take a dollar sign for mathematics
READER = "markdown-tex_math_dollars-tex_math_single_backslash-raw_tex"

#  A manual exists in two languages, and each names its divisions and its title page in its own
#  words. Both vocabularies are recognized so that one renderer serves both.
DIVISIONS = (r"Chapter \d+|Appendix [A-Z]"
		r"|\u0413\u043b\u0430\u0432\u0430 \d+|\u041f\u0440\u0438\u043b\u043e\u0436\u0435\u043d\u0438\u0435 [A-Z]")

LABELS = {
	"order number":		"order",
	"software version":	"version",
	"revision/update":	"revision",
	"date":			"date",
	"\u043d\u043e\u043c\u0435\u0440 \u0438\u0437\u0434\u0430\u043d\u0438\u044f":	"order",
	"\u0432\u0435\u0440\u0441\u0438\u044f \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c\u044b":	"version",
	"\u043f\u0440\u0430\u0432\u043a\u0430":	"revision",
	"\u0434\u0430\u0442\u0430":			"date",
	"num\u00e9ro de commande":	"order",
	"version du logiciel":	"version",
	"r\u00e9vision":		"revision",
}

#  Preamble. The manual is set in a serif face with monospace for everything the reader would type,
#  a running head carrying the chapter, and the identification of the software in the foot.
PREAMBLE = r"""
\usepackage{fancyhdr}
\usepackage{titlesec}
\usepackage{needspace}

\pagestyle{fancy}
\fancyhf{}
\fancyhead[L]{\small\nouppercase{\leftmark}}
\fancyhead[R]{\small\thepage}
\fancyfoot[L]{\footnotesize %(facility)s %(version)s}
\fancyfoot[R]{\footnotesize %(kind)s}
\renewcommand{\headrulewidth}{0.4pt}
\renewcommand{\footrulewidth}{0.4pt}

\fancypagestyle{plain}{%%
	\fancyhf{}%%
	\fancyfoot[L]{\footnotesize %(facility)s %(version)s}%%
	\fancyfoot[R]{\footnotesize %(kind)s}%%
	\renewcommand{\headrulewidth}{0pt}%%
	\renewcommand{\footrulewidth}{0.4pt}}

%%  A chapter opens a page of its own, as it does in a printed manual
\titleformat{\section}[display]{\normalfont\Large\bfseries}{}{0pt}{}
\newcommand{\sectionbreak}{\clearpage}

%%  A message entry is not to be split from its explanation
\titlespacing*{\subsubsection}{0pt}{2ex plus 1ex minus .2ex}{1ex plus .2ex}
"""


def split_title(text):
	"""Lift the title page out of the body and return it as metadata."""
	lines = text.split("\n")
	meta = {}

	m = re.match(r"^# (.+)$", lines[0])
	meta["title"] = m.group(1) if m else "BATCH"

	for line in lines[1:12]:
		m = re.match(r"^## (.+)$", line)

		if m:
			meta["kind"] = m.group(1)
			break

	for line in lines[:20]:
		m = re.match(r"^\s{4}(\S[^:]*):\s+(.+?)\s*$", line)

		if m:
			key = LABELS.get(m.group(1).strip().lower())

			if key:
				meta[key] = m.group(2)

	#  The body starts after the rule that closes the title page
	for i, line in enumerate(lines):
		if line.strip() == "---" and i > 3:
			return meta, "\n".join(lines[i + 1:])

	return meta, text


def join_headings(text):
	"""Join a chapter number with its title, which the source keeps on separate lines."""
	text = re.sub(r"^# (%s)\n# (.+)$" % DIVISIONS,
			lambda m: "# %s. %s" % (m.group(1), m.group(2)), text, flags=re.M)

	#  A rule between chapters is a page break in the document, which the chapter opening already
	#  provides, so it is dropped rather than drawn
	return re.sub(r"^---$\n", "", text, flags=re.M)


def build(src, out):
	text = Path(src).read_text(encoding="utf-8")
	meta, body = split_title(text)
	body = join_headings(body)

	#  The language of a manual is marked by the suffix of its name, and is handed to the typesetter
	#  so that it hyphenates and names its table of contents accordingly. A fourth language costs a
	#  line of the table.
	SUFFIXES = { "_rus": "ru", "_fra": "fr" }

	lang = "en"

	for suffix, code in SUFFIXES.items():
		if suffix in Path(src).stem:
			lang = code
			break

	with tempfile.TemporaryDirectory() as tmp:
		hdr = Path(tmp) / "preamble.tex"
		hdr.write_text(PREAMBLE % {
				"facility": meta.get("title", "BATCH").split()[0],
				"version": meta.get("version", "").replace("BATCH Version ", ""),
				"kind": meta.get("kind", "")}, encoding="utf-8")

		md = Path(tmp) / "body.md"
		md.write_text(body, encoding="utf-8")

		cmd = [
			"pandoc", "-f", READER, "-o", out,
			"--pdf-engine=xelatex",
			"--toc", "--toc-depth=2",
			"-V", "documentclass=report",
			"-V", "papersize=a4",
			"-V", "geometry:margin=25mm",
			"-V", "fontsize=10pt",
			"-V", "mainfont=DejaVu Serif",
			"-V", "sansfont=DejaVu Sans",
			"-V", "monofont=DejaVu Sans Mono",
			"-V", "monofontoptions=Scale=0.85",
			#  The template reaches for Latin Modern when no font package is named, and not
			#  every installation of TeX carries it. Naming one that is always present keeps
			#  the template off that path; the faces themselves come from fontspec above.
			"-V", "fontfamily=charter",
			"-V", "colorlinks=true",
			"-V", "linkcolor=black",
			"-V", "toccolor=black",
			"-V", "title=%s" % meta.get("title", "BATCH"),
			"-V", "subtitle=%s" % meta.get("kind", ""),
			"-V", "author=%s" % meta.get("order", ""),
			"-V", "date=%s   ***   %s" % (meta.get("version", ""), meta.get("date", "")),
			"-V", "lang=%s" % lang,
			"--include-in-header=%s" % hdr,
			str(md),
		]

		res = subprocess.run(cmd, capture_output=True, text=True)

		if res.returncode:
			sys.stderr.write(res.stdout + res.stderr)
			return 1

		if res.stderr.strip():
			sys.stderr.write(res.stderr)

	print("%%BATCH-S-PDF, %s -> %s" % (src, out))

	return 0


if __name__ == "__main__":
	rc = 0

	for pair in sys.argv[1:]:
		src, _, out = pair.partition(":")
		rc |= build(src, out)

	sys.exit(rc)
