#!/usr/bin/env python3
"""
Convert the Markdown documents in docs/ to PDF.

A small Markdown subset is turned into HTML (headings, paragraphs, lists,
tables, code blocks, block quotes, images, raw HTML lines) and printed with
headless Chrome/Chromium. Only the Python standard library is used.

    python3 docs/tools/md2pdf.py docs/REPORT.md docs/V-1-13_BD1_Report.pdf
"""

import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CSS = """
@page { size: A4; margin: 18mm 20mm 18mm 20mm; }
body { font: 10.5pt/1.6 "Liberation Sans", Helvetica, Arial, sans-serif; color: #111; }
h1 { font-size: 14pt; margin: 16pt 0 8pt; break-after: avoid; }
h2 { font-size: 12pt; margin: 14pt 0 6pt; break-after: avoid; }
h3 { font-size: 11pt; margin: 12pt 0 4pt; break-after: avoid; }
p { margin: 0 0 8pt; }
ul, ol { margin: 0 0 8pt; padding-left: 22pt; }
li { margin: 1.5pt 0; }
code { font: 9pt "DejaVu Sans Mono", monospace; background: #f2f2f2; padding: 0 2pt; border-radius: 2pt; }
pre { font: 8.4pt/1.35 "DejaVu Sans Mono", monospace; background: #f5f5f5; border: 1px solid #e2e2e2;
      border-radius: 3pt; padding: 6pt 8pt; white-space: pre-wrap; break-inside: avoid; margin: 0 0 8pt; }
pre code { background: none; padding: 0; font-size: inherit; }
table { border-collapse: collapse; width: 100%; margin: 4pt 0 10pt; font-size: 9.3pt; break-inside: avoid; }
th, td { border: 1px solid #cfcfcf; padding: 3pt 5pt; vertical-align: top; text-align: left; }
th { background: #f2f2f2; }
blockquote { margin: 0 0 8pt; padding: 4pt 10pt; border-left: 3px solid #999; background: #f5f5f5; }
blockquote p { margin: 0; }
figure { margin: 6pt 0 10pt; text-align: center; break-inside: avoid; }
figure img { max-width: 100%; max-height: 110mm; border: 1px solid #e2e2e2; }
figcaption { font-size: 9pt; color: #555; margin-top: 3pt; }
img { max-width: 100%; }
.pagebreak { break-after: page; }
.cover { break-after: page; height: 258mm; display: flex; flex-direction: column; align-items: center;
         text-align: center; font: 11pt/1.7 "Times New Roman", "Liberation Serif", serif; }
.cover .uni { font-weight: 700; font-size: 10.5pt; margin-top: 6mm; }
.cover .logo { height: 24mm; margin: 9mm 0 0; }
.cover .title { font-size: 28pt; font-weight: 700; margin-top: 20mm; line-height: 1.3; }
.cover .course { font-size: 21pt; font-weight: 700; line-height: 1.3; margin-top: 2mm; }
.cover .term { margin-top: 8mm; }
.cover .topic { margin-top: 16mm; }
.cover .prepared { margin-top: 14mm; }
.cover .members { margin-top: 6mm; }
.cover .checked { margin-top: 6mm; }
.cover .city { margin-top: auto; }
.doc-title { font-size: 17pt; font-weight: 700; padding-bottom: 5pt; border-bottom: 1px solid #333; margin: 0 0 12pt; }
.meta { font-size: 10pt; }
"""


def inline(text):
    """Inline Markdown: code, images, links, bold, italic."""
    out = []
    parts = re.split(r"(`[^`]+`)", text)
    for part in parts:
        if part.startswith("`") and part.endswith("`") and len(part) > 1:
            out.append("<code>" + html.escape(part[1:-1]) + "</code>")
            continue
        s = html.escape(part, quote=False)
        s = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)",
                   lambda m: f'<img src="{m.group(2)}" alt="{m.group(1)}">', s)
        s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", r'<a href="\2">\1</a>', s)
        s = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", s)
        s = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", s)
        out.append(s)
    return "".join(out)


def split_row(line):
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    cells, cur, in_code = [], "", False
    for ch in line:
        if ch == "`":
            in_code = not in_code
        if ch == "|" and not in_code:
            cells.append(cur.strip())
            cur = ""
        else:
            cur += ch
    cells.append(cur.strip())
    return cells


def convert(md):
    lines = md.splitlines()
    out = []
    i = 0
    para = []
    first_h1 = True

    def flush():
        nonlocal para
        if para:
            text = " ".join(para)
            m = re.fullmatch(r"!\[([^\]]*)\]\(([^)\s]+)\)", text.strip())
            if m:
                out.append(f'<figure><img src="{m.group(2)}" alt="">'
                           f"<figcaption>{inline(m.group(1))}</figcaption></figure>")
            else:
                out.append("<p>" + inline(text) + "</p>")
            para = []

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if stripped.startswith("```"):
            flush()
            code = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code.append(lines[i])
                i += 1
            out.append("<pre><code>" + html.escape("\n".join(code)) + "</code></pre>")
            i += 1
            continue

        if not stripped:
            flush()
            i += 1
            continue

        m = re.match(r"^(#{1,4})\s+(.*)$", stripped)
        if m:
            flush()
            level = len(m.group(1))
            cls = ""
            if level == 1 and first_h1:
                cls = ' class="first"'
                first_h1 = False
            out.append(f"<h{level}{cls}>{inline(m.group(2))}</h{level}>")
            i += 1
            continue

        if stripped.startswith("<"):
            flush()
            block = [line]
            # copy raw HTML until a blank line
            i += 1
            while i < len(lines) and lines[i].strip():
                block.append(lines[i])
                i += 1
            out.append("\n".join(block))
            continue

        if stripped.startswith("|") and i + 1 < len(lines) and \
                re.match(r"^\s*\|?\s*:?-{3,}", lines[i + 1]):
            flush()
            head = split_row(lines[i])
            i += 2
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append(split_row(lines[i]))
                i += 1
            t = ["<table><thead><tr>"]
            t += [f"<th>{inline(c)}</th>" for c in head]
            t.append("</tr></thead><tbody>")
            for r in rows:
                t.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>")
            t.append("</tbody></table>")
            out.append("".join(t))
            continue

        if stripped.startswith(">"):
            flush()
            quote = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                quote.append(lines[i].strip()[1:].strip())
                i += 1
            out.append("<blockquote><p>" + inline(" ".join(quote)) + "</p></blockquote>")
            continue

        if re.match(r"^\s*([-*]|\d+\.)\s+", line):
            flush()
            # nested lists by indentation
            stack = []  # (indent, tag)
            while i < len(lines) and re.match(r"^\s*([-*]|\d+\.)\s+", lines[i]):
                m = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)$", lines[i])
                indent = len(m.group(1))
                tag = "ol" if m.group(2)[0].isdigit() else "ul"
                text = m.group(3)
                i += 1
                # continuation lines
                while i < len(lines) and lines[i].startswith(" " * (indent + 2)) and \
                        lines[i].strip() and not re.match(r"^\s*([-*]|\d+\.)\s+", lines[i]):
                    text += " " + lines[i].strip()
                    i += 1
                if not stack or indent > stack[-1][0]:
                    out.append(f"<{tag}>")
                    stack.append((indent, tag))
                else:
                    while stack and indent < stack[-1][0]:
                        out.append(f"</li></{stack.pop()[1]}>")
                    out.append("</li>")
                out.append("<li>" + inline(text))
            while stack:
                out.append(f"</li></{stack.pop()[1]}>")
            continue

        para.append(stripped)
        i += 1

    flush()
    return "\n".join(out)


def find_chrome():
    for name in (os.environ.get("CHROME"), "chromium", "chromium-browser",
                 "google-chrome", "google-chrome-stable", "chrome"):
        if name and shutil.which(name):
            return shutil.which(name)
    sys.exit("error: Chrome/Chromium not found (set CHROME=/path/to/chrome)")


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: md2pdf.py input.md output.pdf")
    src = Path(sys.argv[1]).resolve()
    dst = Path(sys.argv[2]).resolve()
    title = src.stem
    body = convert(src.read_text(encoding="utf-8"))
    page = (f'<!doctype html><html lang="mn"><head><meta charset="utf-8">'
            f'<base href="{src.parent.as_uri()}/"><title>{title}</title>'
            f"<style>{CSS}</style></head><body>{body}</body></html>")

    with tempfile.TemporaryDirectory() as tmp:
        page_path = Path(tmp) / "page.html"
        page_path.write_text(page, encoding="utf-8")
        if "--html" in os.environ.get("MD2PDF_FLAGS", ""):
            shutil.copy(page_path, dst.with_suffix(".html"))
        cmd = [find_chrome(), "--headless=new", "--disable-gpu", "--no-sandbox",
               f"--user-data-dir={tmp}/profile", "--no-pdf-header-footer",
               "--allow-file-access-from-files", f"--print-to-pdf={dst}",
               page_path.as_uri()]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    print(f"{src.name} -> {dst}")


if __name__ == "__main__":
    main()
