"""Extract the pinned presenter's pure geometry, replacing only GPU interfaces."""
from pathlib import Path
import sys

source, destination = map(Path, sys.argv[1:3])
text = source.read_text(encoding="utf-8")
helpers = text[text.index("    inline hlslpp::float2 computeHDSize("):
               text.index("    void VIRenderer::render(")]
start = text.index("    void VIRenderer::getViewportAndScissor(")
body = text.index("{", start)
depth = 1
end = body + 1
while depth and end < len(text):
    depth += (text[end] == "{") - (text[end] == "}")
    end += 1
if depth:
    raise SystemExit("Unbalanced pinned VI presenter function")
destination.write_text(helpers + text[start:end] + "\n", encoding="utf-8")
