"""Collects every ```mermaid block of the course into one HTML page that renders them with
Mermaid (the same renderer GitHub uses) and reports which ones fail.

    python docs/tools/check_mermaid.py            # writes build/mermaid_check.html
    python -m http.server 8765 --directory build  # then open http://localhost:8765/mermaid_check.html

When rendering finishes, the page title becomes "mermaid: N ok, M failed" and every failed
diagram is listed at the top with its file, line and Mermaid's error message.
"""

import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "build", "mermaid_check.html")
MERMAID = "https://cdn.jsdelivr.net/npm/mermaid@11.4.1/dist/mermaid.min.js"


def collect():
    blocks = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in (".git", "third_party") and not d.startswith("build")]
        for name in sorted(filenames):
            if not name.endswith(".md"):
                continue
            path = os.path.join(dirpath, name)
            lines = open(path, encoding="utf-8").read().split("\n")
            i = 0
            while i < len(lines):
                if re.match(r"^\s*```mermaid\s*$", lines[i]):
                    start = i + 1
                    j = start
                    while j < len(lines) and not re.match(r"^\s*```\s*$", lines[j]):
                        j += 1
                    blocks.append({"file": os.path.relpath(path, ROOT).replace("\\", "/"),
                                   "line": start, "code": "\n".join(lines[start:j])})
                    i = j
                i += 1
    return blocks


def main():
    blocks = collect()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(f"""<!doctype html><meta charset="utf-8"><title>mermaid: rendering...</title>
<script src="{MERMAID}"></script>
<body style="font-family:sans-serif;background:#fff">
<pre id="results">running</pre><div id="out"></div>
<script>
const blocks = {json.dumps(blocks)};
mermaid.initialize({{startOnLoad: false, securityLevel: "strict"}});
(async () => {{
  const failed = []; let ok = 0;
  for (let i = 0; i < blocks.length; i++) {{
    const b = blocks[i];
    try {{
      const r = await mermaid.render("m" + i, b.code);
      const d = document.createElement("div");
      d.innerHTML = "<h4>" + b.file + ":" + b.line + "</h4>" + r.svg;
      document.getElementById("out").appendChild(d);
      ok++;
    }} catch (e) {{
      failed.push({{file: b.file, line: b.line, error: String(e && e.message || e).slice(0, 300)}});
    }}
  }}
  document.getElementById("results").textContent = JSON.stringify({{ok, failed}}, null, 1);
  document.title = "mermaid: " + ok + " ok, " + failed.length + " failed";
}})();
</script>""")
    print(f"{len(blocks)} mermaid blocks -> {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
