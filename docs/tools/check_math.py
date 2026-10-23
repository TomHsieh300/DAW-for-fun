"""Collects every formula of the course (```math blocks and inline $`...`$) into one HTML page
that typesets them with MathJax (the TeX engine GitHub uses) and lists the ones that fail.

    python docs/tools/check_math.py                # writes build/math_check.html
    python -m http.server 8765 --directory build   # then open http://localhost:8765/math_check.html

When typesetting finishes, the page title becomes "math: N ok, M failed"; every failure is
listed with its file, line, the TeX source and MathJax's error message. Macros that GitHub
rejects although MathJax knows them (\\operatorname) are reported as failures too.
"""

import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "build", "math_check.html")
MATHJAX = "https://cdn.jsdelivr.net/npm/mathjax@3.2.2/es5/tex-svg.js"
INLINE = re.compile(r"\$`(.+?)`\$")
GITHUB_REJECTS = ["\\operatorname"]


def collect():
    items = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in (".git", "third_party") and not d.startswith("build")]
        for name in sorted(filenames):
            if not name.endswith(".md"):
                continue
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, ROOT).replace("\\", "/")
            lines = open(path, encoding="utf-8").read().split("\n")
            fence = None
            i = 0
            while i < len(lines):
                line = lines[i]
                m = re.match(r"^\s*(`{3,}|~{3,})(\S*)", line)
                if fence is None and m:
                    if m.group(2) == "math":
                        j = i + 1
                        while j < len(lines) and not re.match(r"^\s*`{3,}\s*$", lines[j]):
                            j += 1
                        items.append({"file": rel, "line": i + 2, "tex": "\n".join(lines[i + 1:j]), "display": True})
                        i = j + 1
                        continue
                    fence = m.group(1)[0] * 3
                elif fence is not None:
                    if line.strip().startswith(fence):
                        fence = None
                else:
                    for mm in INLINE.finditer(line):
                        items.append({"file": rel, "line": i + 1, "tex": mm.group(1), "display": False})
                i += 1
    return items


def main():
    items = collect()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(f"""<!doctype html><meta charset="utf-8"><title>math: typesetting...</title>
<script>window.MathJax = {{startup: {{typeset: false}}, svg: {{fontCache: "none"}}}};</script>
<script src="{MATHJAX}"></script>
<body style="font-family:sans-serif"><pre id="results">running</pre>
<script>
const items = {json.dumps(items)};
const rejected = {json.dumps(GITHUB_REJECTS)};
(async () => {{
  await MathJax.startup.promise;
  const failed = []; let ok = 0;
  for (const it of items) {{
    let err = rejected.find(m => it.tex.includes(m));
    if (!err) {{
      const node = await MathJax.tex2svgPromise(it.tex, {{display: it.display}});
      const e = node.querySelector("[data-mml-node=merror]");
      if (e) err = e.getAttribute("data-mjx-error") || e.textContent;
    }} else err = "GitHub rejects " + err;
    if (err) failed.push({{file: it.file, line: it.line, tex: it.tex.slice(0, 200), error: String(err).slice(0, 200)}});
    else ok++;
  }}
  document.getElementById("results").textContent = JSON.stringify({{ok, failed}}, null, 1);
  document.title = "math: " + ok + " ok, " + failed.length + " failed";
}})();
</script>""")
    print(f"{len(items)} formulas -> {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
