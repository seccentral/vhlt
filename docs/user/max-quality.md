# Maximum-Quality Compiles

For the most accurate lighting, use `hlvis -full` plus a few `hlrad` settings: `-extra`, more light bounces, smaller lighting patches, and full-precision light transfers. The defaults below match the tools' own help output and the settings table each tool prints at the start of a compile.

## Final-quality preset

Append the map path to each line as usual. In an editor's compile dialog this is typically `"$bspdir/$file"`.

```sh
hlcsg  -console 0 -cliptype precise
hlbsp  -console 0
hlvis  -console 0 -full
hlrad  -console 0 -extra -bounce 16 -chop 32 -texchop 16 -dlight 1 -compress 0 -vismatrix normal -estimate
```

`-console 0` only disables the pop-up console window; it has no effect on quality.

## What each setting does

| Flag | Default | Effect | Cost |
|------|---------|--------|------|
| `hlcsg -cliptype precise` | `simple` | More accurate player collision around angled brushes | Small |
| `hlvis -full` | normal | More accurate visibility, so fewer hidden surfaces are drawn | Can take much longer on large open maps |
| `hlrad -extra` | off | Samples each lighting point 9 times: smooth shadow edges, less stair-stepping | About 2–4× the lighting time |
| `-bounce 16` | 8 | More light bouncing off walls: brighter, fuller indirect light in deep corners | Roughly linear; diminishing returns past about 12 |
| `-chop 32` | 64 | Smaller patches for bounced light, so indirect light is more detailed | About 4× the patches and up to about 16× the time and memory. **This is the expensive one.** |
| `-texchop 16` | 32 | Finer patches on light-texture faces: smoother, more accurate light from texture lights | Modest; scales with the area of light textures |
| `-dlight 1` | 10 | Counts faint direct light that would otherwise be dropped, so lights reach farther | Small to moderate |
| `-compress 0` | 1 (16-bit) | Stores light transfers at 32-bit precision instead of 16-bit, reducing banding in soft gradients | Twice the memory for transfers |
| `-vismatrix normal` | `sparse` | Same result, faster, but uses more RAM | Speed/memory trade-off only; no quality change |
| `-estimate` | off | Shows the estimated time remaining in the compile window | None |

## Texture scale: often the biggest factor

Lighting detail is tied to texture scale. Lightmaps get one sample per 16 texture pixels, so a face at texture scale 0.5 gets 4× the lighting detail of one at 1.0. Use smaller scales on surfaces where shadows matter.

This increases the lighting data, so run with `-chart` to see whether you're near the size limit (`-lightdata` raises it).

## What to leave at the default

- **Brightness settings** (`-gamma`, `-scale`, `-limiter`, `-texreflectscale`, `-minlight`): these change the look, not the accuracy.
- **`-blur`:** softens shadows, which hides detail rather than adding it.
- **`-incremental`:** reuses a saved transfer file between compiles, and gives stale results after you change settings.
- **`-low`:** only lowers process priority, which makes compiles slower.

## Practical tips

- **Iterate fast, finish slow:** iterate with `hlvis -fast` and `hlrad` without `-extra`, then run this preset for final builds. Save it as a second preset in your editor's compile dialog, for example *Run Map → Expert* in J.A.C.K. or Hammer.
- **Running out of memory** with `-chop 32 -compress 0`: set `-chop` back to 48 first, or drop `-vismatrix normal` (use the default `sparse`).
- **Keep `-texchop` at or below `-chop`.**

See the [user manual](manual.md) for every option of every tool.
