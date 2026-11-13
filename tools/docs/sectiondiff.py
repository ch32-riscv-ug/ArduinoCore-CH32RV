"""Compare each X.md / X.ja.md pair section by section: headings, table rows, list items, code blocks and the body length ratio (a guide: English runs about 1.2 to 3.2 times the Japanese). Usage: python3 tools/docs/sectiondiff.py <repo> (from [oep], 2026-11-13). Read each section it names before changing."""
import re, sys, subprocess
from pathlib import Path
FENCE = re.compile(r"```.*?```", re.S)
def sections(text):
    text = FENCE.sub(lambda m: "\n@@CODE@@\n", text)
    out, cur = [], {"h": "(top)", "body": []}
    for line in text.splitlines():
        if re.match(r"#{1,6} ", line):
            out.append(cur); cur = {"h": line.strip(), "body": []}
        else:
            cur["body"].append(line)
    out.append(cur)
    res = []
    for s in out:
        b = s["body"]
        res.append(dict(h=s["h"], items=sum(1 for l in b if re.match(r"\s*([-*]|\d+\.) ", l)),
                        rows=sum(1 for l in b if l.startswith("|")), code=sum(1 for l in b if l == "@@CODE@@"),
                        chars=len("".join(l for l in b if l and not l.startswith("|")))))
    return res
for repo in sys.argv[1:]:
    files = subprocess.run(["git", "-C", repo, "ls-files", "*.ja.md"], capture_output=True, text=True).stdout.split()
    for f in files:
        ja = Path(repo) / f; en = Path(repo) / (f[:-6] + ".md")
        if not en.exists(): continue
        se, sj = sections(en.read_text()), sections(ja.read_text())
        if len(se) != len(sj):
            print(f"{en}: sections {len(se)}/{len(sj)}"); continue
        for a, b in zip(se, sj):
            notes = [k for k in ("items", "rows", "code") if a[k] != b[k]]
            ratio = (a["chars"] + 1) / (b["chars"] + 1)
            if notes or (max(a["chars"], b["chars"]) > 300 and not 1.2 <= ratio <= 3.2):
                print(f"{en}: {a['h'][:50]} | " + " ".join(f"{k} {a[k]}/{b[k]}" for k in notes) + f" | en/ja chars {a['chars']}/{b['chars']} ratio {ratio:.2f}")
