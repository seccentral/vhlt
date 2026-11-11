# Maximum-Quality Compiles

For the most accurate lighting, use `hlvis -full` plus a few `hlrad` settings: `-extra`, more light bounces, smaller lighting patches, and full-precision light transfers. The defaults below match the tools' own help output and the settings table each tool prints at the start of a compile.

## Final-quality preset

Append the map path to each line as usual. In an editor's compile dialog this is typically `"$bspdir/$file"`.

```sh
hlcsg  -console 0 -cliptype precise
hlbsp  -console 0
hlvis  -console 0 -full
hlrad  -console 0 -extra -bounce 16 -chop 16 -texchop 8 -dlight 1 -compress 0 -vismatrix normal -estimate
```

These settings give noticeably better results but need much more compute; the costs are in the table below. hlrad prints a "not recommended" warning for `-chop` and `-texchop` values this small. That warning is about compile time, not about errors in the result.

**Add `-console 0` to every tool.** It has no effect on quality, but it makes compiles started from J.A.C.K. (or Hammer) faster. Without it, each tool opens its own console window and writes every line of output twice: once to that window, flushed line by line, and once to the editor's compile log. With `-console 0`, output only goes to the compile log.

## What each setting does

| Flag | Default | Effect | Cost |
|------|---------|--------|------|
| `hlcsg -cliptype precise` | `simple` | More accurate player collision around angled brushes | Small |
| `hlvis -full` | normal | More accurate visibility, so fewer hidden surfaces are drawn | Can take much longer on large open maps |
| `hlrad -extra` | off | Samples each lighting point 9 times: smooth shadow edges, less stair-stepping | About 2–4× the lighting time |
| `-bounce 16` | 8 | More light bouncing off walls: brighter, fuller indirect light in deep corners | Roughly linear; diminishing returns past about 12 |
| `-chop 16` | 64 | Much smaller patches for bounced light, so indirect light is far more detailed | About 16× the patches; time and memory grow faster than the patch count. **This is the expensive one.** |
| `-texchop 8` | 32 | Finer patches on light-texture faces: smoother, more accurate light from texture lights | About 16× the patches on light-texture faces; scales with their area |
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
- **Use `hlrad-ng` for this preset.** It takes the same options as `hlrad` and is built for exactly this load: on a graphics card with hardware ray tracing the preset takes seconds where `hlrad` takes minutes, and it has no 65,535-patch limit with `-vismatrix normal`. See [hlrad-ng in the user manual](manual.md#hlrad-ng).
- **Running out of memory or time** with `-chop 16 -compress 0`: raise `-chop` to 32 (and `-texchop` to 16) first, then drop `-vismatrix normal` (use the default `sparse`).
- **Keep `-texchop` at or below `-chop`.**

See the [user manual](manual.md) for every option of every tool.
