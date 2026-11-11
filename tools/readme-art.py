"""Generate the animated README graphics in docs/assets/.

    python tools/readme-art.py

Both SVGs replay measured stalkyard run times (maximum-quality preset, Ryzen 7 2700X with
16 threads, RTX 2080; see docs/research/hlrad-optimizations.md) at SPEEDUP x speed, using
CSS animations only, so GitHub shows them animated inside an <img> with no script.
"""
from __future__ import annotations

import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "assets")
SPEEDUP = 8.0  # replay speed
CYCLE = 16.0  # seconds per animation loop

# Measured run times in seconds (stalkyard, maximum-quality preset).
CLASSIC_TOTAL = 95.44
CLASSIC_PHASES = [("BuildFacelights", 23.48), ("BuildVisLeafs", 44.93), ("MakeScales", 11.40)]
NG_CPU_TOTAL = 17.66
NG_GPU_TOTAL = 5.62
NG_LOG = [  # (line, seconds since start when the line completes); "%P" = progress dots
    ("Sample positions: %P (0.08 seconds)", 0.08),
    ("GPU: NVIDIA GeForce RTX 2080, hardware ray tracing (0.50 seconds)", 0.58),
    ("Patch lighting (GPU): %P (0.20 seconds)", 0.78),
    ("Direct lighting (GPU): %P (1.19 seconds)", 1.97),
    ("Patch visibility (GPU): %P (0.53 seconds)", 2.50),
    ("Transfers (GPU): %P (0.29 seconds)", 2.79),
    ("Transfers on the GPU: 249.42M, 868.4 megs with the visibility matrix", 2.79),
    ("Light bounces (16, GPU): %P (1.50 seconds)", 4.29),
    ("Patch triangulation: %P (0.15 seconds)", 4.44),
    ("Bounced light on lightmaps: %P (0.75 seconds)", 5.19),
    ("Final lightmaps: %P (0.01 seconds)", 5.20),
    ("5.62 seconds elapsed", NG_GPU_TOTAL),
]
PROGRESS = "".join(f"{i}0%..." for i in range(1, 10))
FLAGS = "-extra -bounce 16 -chop 16 -texchop 8 -dlight 1 -compress 0 -vismatrix normal"

# Palette: night-blue ground, warm "baked light" for hlrad-ng, cool blue for the GPU method.
BG, PANEL, TRACK, RULE = "#151C2E", "#111827", "#1F2840", "#2A3550"
TEXT, SOFT, MUTED = "#F3F5FA", "#D5DCE8", "#97A4BB"
CLASSIC, NG_CPU, NG_HOT, COOL = "#5B6882", "#E0A458", "#FFB25C", "#8FD3FF"

SANS = "'Segoe UI Variable Display','Segoe UI',-apple-system,BlinkMacSystemFont,'Helvetica Neue',Arial,sans-serif"
MONO = "'Cascadia Mono','Cascadia Code',SFMono-Regular,Menlo,Consolas,'DejaVu Sans Mono','Liberation Mono',monospace"
MONO_SIZE, CELL = 13, 7.8  # every terminal line is stretched to CELL px per character


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def pct(t_real: float) -> float:
    """Real run time -> percentage of the animation loop."""
    return round(min(t_real / SPEEDUP / CYCLE * 100.0, 99.0), 3)


class Anim:
    """Collects one @keyframes rule per animated element."""

    def __init__(self) -> None:
        self.rules: list[str] = []

    def _add(self, frames: str, timing: str = "linear") -> str:
        name = f"a{len(self.rules)}"
        self.rules.append(f"@keyframes {name}{{{frames}}}")
        return f"animation:{name} {CYCLE}s {timing} infinite"

    def appear(self, t_real: float) -> str:
        if t_real <= 0:
            return ""
        p = pct(t_real)
        return self._add(f"0%,{p}%{{opacity:0}}{p + 0.2}%,100%{{opacity:1}}")

    def grow(self, t_real: float) -> str:
        return self._add(f"0%{{transform:scaleX(0)}}{pct(t_real)}%,100%{{transform:scaleX(1)}}")

    def slide(self, t0: float, t1: float, dx: float) -> str:
        return self._add(
            f"0%,{pct(t0)}%{{transform:translateX(0)}}{pct(t1)}%,100%{{transform:translateX({dx}px)}}")

    def css(self) -> str:
        return "\n".join(self.rules)


def svg(w: int, h: int, title: str, body: list[str], anim: Anim) -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" role="img" aria-label="{esc(title)}">
<title>{esc(title)}</title>
<style>
.s{{font-family:{SANS}}} .m{{font-family:{MONO};font-size:{MONO_SIZE}px}}
.bar{{transform-box:fill-box;transform-origin:left center}}
{anim.css()}
@media (prefers-reduced-motion:reduce){{*{{animation:none!important}}}}
</style>
{chr(10).join(body)}
</svg>
"""


def text(x, y, s, size, fill, weight=400, anchor="start", cls="s", style="", extra=""):
    st = f' style="{style}"' if style else ""
    return (f'<text class="{cls}" x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" '
            f'fill="{fill}" text-anchor="{anchor}"{st}{extra}>{s}</text>')


def hero() -> str:
    a, b = Anim(), []
    w, h = 960, 520
    b.append(f'<defs><linearGradient id="hot" x1="0" x2="1"><stop offset="0" stop-color="#FFD27A"/>'
             f'<stop offset="1" stop-color="#FF8A3D"/></linearGradient></defs>')
    b.append(f'<rect width="{w}" height="{h}" rx="16" fill="{BG}"/>')

    speedup = round(CLASSIC_TOTAL / NG_GPU_TOTAL)
    b.append(text(38, 118, f"{speedup}× faster", 88, TEXT, 800, style="letter-spacing:-2px"))
    b.append(text(42, 158, "lighting with hlrad-ng", 26, TEXT, 600))
    b.append(text(42, 190, "stalkyard at the maximum-quality preset, same options in both tools", 15.5, MUTED))

    x0, max_w = 40, 470
    rows = [("hlrad (classic, CPU)", CLASSIC_TOTAL, CLASSIC, CLASSIC),
            ("hlrad-ng, CPU only", NG_CPU_TOTAL, NG_CPU, NG_CPU),
            ("hlrad-ng, GPU (RTX 2080)", NG_GPU_TOTAL, "url(#hot)", NG_HOT)]
    for i, (label, secs, fill, ink) in enumerate(rows):
        y = 246 + i * 58
        bw = max(max_w * secs / CLASSIC_TOTAL, 16)
        b.append(text(x0, y, label, 15, SOFT, 500))
        b.append(f'<rect x="{x0}" y="{y + 10}" width="{max_w}" height="16" rx="8" fill="{TRACK}"/>')
        b.append(f'<rect class="bar" x="{x0}" y="{y + 10}" width="{bw:.1f}" height="16" rx="8" fill="{fill}" '
                 f'style="{a.grow(secs)}"/>')
        b.append(text(round(x0 + bw + 12), y + 24, f"{secs:.1f} s", 16, ink, 700, style=a.appear(secs)))

    # What runs on the GPU, with the classic and hlrad-ng phase times from the same runs.
    rx, c1, c2 = 600, 832, 920
    b.append(text(rx, 72, "What hlrad-ng runs on the GPU", 17, TEXT, 650))
    b.append(text(c1, 100, "hlrad", 12.5, MUTED, 500, "end"))
    b.append(text(c2, 100, "hlrad-ng", 12.5, MUTED, 500, "end"))
    phases = [("Direct lighting", "sun, sky, point, spot and texture lights", "23.5 s", "1.19 s"),
              ("Patch visibility", None, "44.9 s", "0.53 s"),
              ("Transfers", None, "11.4 s", "0.29 s"),
              ("16 light bounces", None, "on CPU", "1.50 s")]
    y = 128
    for name, sub, old, new in phases:
        b.append(text(rx, y, name, 15, SOFT, 500))
        b.append(text(c1, y, old, 15, MUTED, 500, "end"))
        b.append(text(c2, y, new, 15, NG_HOT, 700, "end"))
        if sub:
            b.append(text(rx, y + 18, sub, 12.5, MUTED))
        y += 34 if sub else 22
        b.append(f'<line x1="{rx}" x2="{c2}" y1="{y - 6}" y2="{y - 6}" stroke="{RULE}"/>')
        y += 22
    b.append(text(rx, 312, "Vulkan compute with SPIR-V shaders", 15.5, COOL, 650))
    for i, line in enumerate(["Hardware ray tracing (VK_KHR_ray_query), or a",
                              "BSP walk on cards without it. Nothing to install;",
                              "with no usable GPU it runs on the CPU instead."]):
        b.append(text(rx, 334 + i * 19, line, 13, MUTED))

    b.append(f'<line x1="40" x2="920" y1="404" y2="404" stroke="{RULE}"/>')
    b.append(text(40, 436, f'<tspan font-weight="700" fill="{TEXT}">Hardened throughout.</tspan> '
                           "BSP, WAD, .prt, .vis and lights.rad input is validated and dozens of", 15, SOFT))
    b.append(text(40, 458, "memory bugs are fixed; every tool builds with zero warnings and zero clang-tidy findings.",
                  15, SOFT))
    b.append(text(40, 494, f"Measured on a Ryzen 7 2700X (16 threads) with an RTX 2080. "
                           f"The bars replay the run times at {SPEEDUP:g}× speed.", 12.5, MUTED))
    return svg(w, h, f"hlrad-ng lights stalkyard {speedup} times faster than hlrad", b, a)


def term_line(x, y, s, fill, style="", n=None):
    """One terminal line; n is the visible character count when s holds markup."""
    if n is None:
        n = len(s.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">"))
    st = f' style="{style}"' if style else ""
    return (f'<text class="m" x="{x}" y="{y}" fill="{fill}" textLength="{n * CELL:.1f}" '
            f'lengthAdjust="spacingAndGlyphs"{st} xml:space="preserve">{s}</text>')


def terminal() -> str:
    a, b = Anim(), []
    w, x, right, step = 960, 40, 920, 20
    b.append(f'<rect width="{w}" height="600" rx="14" fill="#0D1322"/>')
    b.append(term_line(x, 36, esc(f"$ FLAGS='{FLAGS}'"), MUTED))

    def pane(top, rows, title, done_s, done_fill):
        hgt = 52 + rows * step
        b.append(f'<rect x="24" y="{top}" width="912" height="{hgt}" rx="10" fill="{PANEL}" stroke="{RULE}"/>')
        b.append(text(x, top + 27, title, 14, SOFT, 650))
        b.append(text(right, top + 27, f"{done_s:.2f} s", 14, done_fill, 700, "end", style=a.appear(done_s)))
        return top + 56

    # Classic hlrad: each phase name shows at once, its progress dots creep, the time follows.
    y = pane(56, 6, "hlrad (classic, CPU)", CLASSIC_TOTAL, MUTED)
    b.append(f'<clipPath id="cp"><rect x="24" y="56" width="912" height="172"/></clipPath>')
    b.append(term_line(x, y, "$ hlrad $FLAGS stalkyard", SOFT))
    t = 0.0
    for name, secs in CLASSIC_PHASES:
        y += step
        head = f"{name}: "
        b.append(f'<g style="{a.appear(t)}">' + term_line(
            x, y, esc(head + PROGRESS) + f'<tspan style="{a.appear(t + secs)}"> ({secs:.2f} seconds)</tspan>',
            SOFT, n=len(f"{head}{PROGRESS} ({secs:.2f} seconds)")) + "</g>")
        cover_x = x + len(head) * CELL
        dx = right + 16 - cover_x
        b.append(f'<g clip-path="url(#cp)"><rect x="{cover_x:.1f}" y="{y - 15}" width="{dx:.1f}" height="20" '
                 f'fill="{PANEL}" style="transform:translateX({dx:.1f}px);{a.slide(t, t + secs, dx)}"/></g>')
        t += secs
    y += step
    b.append(term_line(x, y, "Bounces 1-16, CreateTriangulations, AddPatchLights, FinalLightFace", MUTED,
                       a.appear(t)))
    y += step
    b.append(term_line(x, y, f"{CLASSIC_TOTAL:.2f} seconds elapsed", SOFT, a.appear(CLASSIC_TOTAL)))

    # hlrad-ng: the real one-line-per-phase log.
    y = pane(248, len(NG_LOG) + 1, "hlrad-ng (Vulkan, RTX 2080)", NG_GPU_TOTAL, NG_HOT)
    b.append(term_line(x, y, "$ hlrad-ng $FLAGS stalkyard", SOFT))
    for line, done in NG_LOG:
        y += step
        fill = NG_HOT if line.endswith("elapsed") else SOFT
        b.append(term_line(x, y, esc(line.replace("%P", PROGRESS)), fill, a.appear(done)))

    b.append(text(40, 586, f"Real output from stalkyard runs (classic progress lines condensed), replayed at "
                           f"{SPEEDUP:g}× speed.", 12.5, MUTED))
    return svg(w, 600, "Terminal replay: hlrad takes 95 seconds, hlrad-ng 5.6 seconds", b, a)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, content in (("hlrad-ng-speed.svg", hero()), ("hlrad-ng-terminal.svg", terminal())):
        with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(content)
        print("wrote", os.path.normpath(os.path.join(OUT, name)))
