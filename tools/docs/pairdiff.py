"""Compare each X.md / X.ja.md pair by the words that do not translate: inline code, the number of code blocks and the link targets. Usage: python3 tools/docs/pairdiff.py <repo> (from [oep], 2026-11-13). A translated placeholder or a wrapped `...` shows up too: read before changing."""
import re, sys, subprocess, collections
from pathlib import Path
FENCE = re.compile(r"```.*?```", re.S)
CODE = re.compile(r"`([^`\n]+)`")
LINK = re.compile(r"\]\(([^)\s]+)\)")
def tokens(text):
    fences = [f.strip() for f in FENCE.findall(text)]
    body = FENCE.sub("", text)
    code = collections.Counter(CODE.findall(body))
    links = collections.Counter(re.sub(r"\.ja\.md", ".md", l) for l in LINK.findall(body))
    return fences, code, links
def compare(en, ja):
    fe, ce, le = tokens(en.read_text(encoding="utf-8")); fj, cj, lj = tokens(ja.read_text(encoding="utf-8"))
    out = []
    # fences: compare code (comments may be translated) - only count
    if len(fe) != len(fj): out.append(f"code blocks {len(fe)}/{len(fj)}")
    only_e = sorted((ce - cj).elements()); only_j = sorted((cj - ce).elements())
    if only_e: out.append("code only en: " + ", ".join(only_e[:12]) + (" ..." if len(only_e) > 12 else ""))
    if only_j: out.append("code only ja: " + ", ".join(only_j[:12]) + (" ..." if len(only_j) > 12 else ""))
    le2 = set(le) - set(lj); lj2 = set(lj) - set(le)
    if le2: out.append("links only en: " + ", ".join(sorted(le2)[:8]))
    if lj2: out.append("links only ja: " + ", ".join(sorted(lj2)[:8]))
    return out
for repo in sys.argv[1:]:
    root = Path(repo)
    files = subprocess.run(["git", "-C", repo, "ls-files", "*.ja.md"], capture_output=True, text=True).stdout.split()
    for f in files:
        ja = root / f; en = root / (f[:-6] + ".md")
        if not en.exists(): continue
        d = compare(en, ja)
        if d:
            print(f"== {repo}/{en.relative_to(root)}  ({sum(len(x) for x in d)} chars of diff)")
            for x in d: print("   ", x[:400])
