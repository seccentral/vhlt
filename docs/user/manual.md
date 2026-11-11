# VHLT User Manual

This manual documents the command-line usage of the Vluzacn Half-Life Compilation Tools (VHLT).

## Typical Compile Pipeline

```sh
hlcsg  mapname
hlbsp  mapname
hlvis  mapname
hlrad  mapname
```

Each tool reads `mapname.map` or `mapname.bsp` and writes the next stage. The final `.bsp` is produced by `hlbsp`.

For final builds with the most accurate lighting, see [Maximum-Quality Compiles](max-quality.md).

## Common Options

These options are accepted by most tools:

| Option | Description |
|--------|-------------|
| `-console #` | Set to 0 to disable the pop-up console (default 1). |
| `-lang file` | Localization file. |
| `-texdata #` | Alter maximum texture memory limit (KB, 1 to 2097151). |
| `-lightdata #` | Alter maximum lighting memory limit (KB, 1 to 2097151). |
| `-chart` | Display BSP statistics. |
| `-low` / `-high` | Run at altered process priority. |
| `-nolog` | Do not generate compile log files. |
| `-threads #` | Manually specify thread count (1 to 64; larger values are clamped). |
| `-estimate` | Display estimated time during compile. |
| `-verbose` | Verbose messages. |
| `-noinfo` | Do not show tool configuration information. |
| `-dev #` | Developer message level. |

Numeric command-line values must be valid numbers. An invalid value stops the tool with `invalid value '<value>' for <option>` instead of silently becoming 0. Numbers read from map files, entities and data files keep their historical lenient parsing.

## hlcsg Options

| Option | Description |
|--------|-------------|
| `-nowadtextures` | Include all used textures into the BSP. |
| `-wadinclude file` | Place textures used from the specified WAD into the BSP. |
| `-noclip` | Do not create clipping hull. |
| `-clipeconomy` | Turn clipnode economy mode on. |
| `-cliptype value` | `smallest`, `normalized`, `simple`, `precise`, or `legacy` (default). |
| `-nullfile file` | List of entities to retexture with NULL. |
| `-onlyents` | Entity update from `.map` to `.bsp`. |
| `-noskyclip` | Disable automatic clipping of SKY brushes. |
| `-tiny #` | Minimum brush face surface area before discard. |
| `-brushunion #` | Threshold to warn about overlapping brushes. |
| `-hullfile file` | Read custom collision hull dimensions. |
| `-wadcfgfile file` | WAD configuration file. |
| `-wadconfig name` | Old wad configuration approach (select group from `wad.cfg`). |
| `-nonulltex` | Turn off null texture stripping. |
| `-nonullifytrigger` | Do not remove `aaatrigger` texture. |
| `-nolightopt` | Do not optimize engine light entities. |
| `-notextconvert` | Do not convert `game_text` messages from Windows ANSI to UTF-8. |
| `-wadautodetect` | Force auto-detection of WAD files. |
| `-scale #` | Scale the world (use at your own risk). |
| `-noresetlog` | Do not delete log file. |
| `mapfile` | The map file to compile. |

Example:

```sh
hlcsg -cliptype precise -threads 4 mymap
```

## hlbsp Options

| Option | Description |
|--------|-------------|
| `-leakonly` | Run BSP only enough to check for leaks. |
| `-subdivide #` | Face subdivide size. |
| `-maxnodesize #` | Maximum portal node size. |
| `-notjunc` | Do not break edges on T-junctions. |
| `-nobrink` | Do not smooth brinks. |
| `-noclip` | Do not process clipping hull. |
| `-nofill` | Do not fill outside (masks leaks). |
| `-noinsidefill` | Do not fill empty spaces. |
| `-noopt` | Do not optimize planes on BSP write. |
| `-noclipnodemerge` | Do not optimize clipnodes. |
| `-nohull2` | Do not generate hull 2. |
| `-viewportal` | Show portal boundaries in `mapname_portal.pts`. |
| `mapfile` | The map file to compile. |

Example:

```sh
hlbsp -leakonly mymap
```

## hlvis Options

| Option | Description |
|--------|-------------|
| `-full` | Full vis. |
| `-fast` | Fast vis. |
| `-maxdistance #` | Alter maximum visibility distance. |
| `mapfile` | The map file to compile. |

Example:

```sh
hlvis -full mymap
```

## hlrad Options

| Option | Description |
|--------|-------------|
| `-waddir folder` | Search this folder for WAD files. |
| `-fast` | Fast radiosity. |
| `-vismatrix value` | `normal`, `sparse`, or `off`. |
| `-extra` | 9-point oversampling for better quality. |
| `-bounce #` | Number of radiosity bounces. |
| `-ambient r g b` | Ambient world light (0.0-1.0). |
| `-limiter #` | Light clipping threshold (-1 = none). |
| `-circus` | Enable circus mode for locating unlit lightmaps. |
| `-nospread` | Disable sunlight spread angles. |
| `-nopaque` | Disable opaque `zhlt_lightflags`. |
| `-smooth #` | Smoothing threshold in degrees. |
| `-smooth2 #` | Smoothing threshold between different textures. |
| `-chop #` | Radiosity patch size for normal textures. |
| `-texchop #` | Radiosity patch size for texture light faces. |
| `-notexscale` | Do not scale radiosity patches with texture scale. |
| `-coring #` | Lighting threshold before blackness. |
| `-dlight #` | Direct lighting threshold. |
| `-nolerp` | Disable radiosity interpolation. |
| `-fade #` | Global fade value. |
| `-texlightgap #` | Global gap distance for texlights. |
| `-scale #` | Global light scaling value. |
| `-gamma #` | Global gamma value. |
| `-sky #` | Ambient sunlight contribution in shade. |
| `-lights file` | Manually specify a `lights.rad` file. |
| `-noskyfix` | Disable global `light_environment`. |
| `-incremental` | Use or create incremental transfer list. |
| `-dump` | Dump light patches for debugging. |
| `-colourgamma r g b` | Per-channel gamma. |
| `-colourscale r g b` | Per-channel light scale. |
| `-colourjitter r g b` | Per-channel noise. |
| `-jitter r g b` | Monochromatic noise. |
| `-customshadowwithbounce` | Custom shadows with bounce. |
| `-rgbtransfers` | Enable RGB transfers. |
| `-minlight #` | Minimum final light (0-255). |
| `-compress #` | Transfer compression (0=32bit,1=16bit,2=8bit). |
| `-rgbcompress #` | RGB transfer compression. |
| `-softsky #` | Smooth skylight (0=off,1=on). |
| `-depth #` | Translucent object thickness. |
| `-blockopaque #` | Remove black areas around opaque entities. |
| `-notextures` | Do not load textures. |
| `-texreflectgamma #` | Reflectivity gamma. |
| `-texreflectscale #` | Reflectivity scale. |
| `-blur #` | Enlarge lightmap sample (blur the lightmap). Valid range 1.0 to 16.0; larger values are clamped. |
| `-noemitterrange` | Do not fix pointy texlights. |
| `-nobleedfix` | Do not fix wall bleeding. |
| `-drawpatch` | Export light patch positions. |
| `-drawsample x y z r` | Export light sample positions. |
| `-drawedge` | Export smooth edge positions. |
| `-drawlerp` | Show bounce triangulation status. |
| `-drawnudge` | Show nudged samples. |
| `-drawoverload` | Highlight fullbright spots. |
| `mapfile` | The map file to compile. |

Example:

```sh
hlrad -extra -bounce 2 -ambient 0.1 0.1 0.1 mymap
```

## hlrad-ng

`hlrad-ng` is a faster `hlrad`. It takes the same options (the table above), reads and writes the same files, and uses the same lighting model, so it can replace `hlrad` in a compile preset: point the RAD step of J.A.C.K. or Hammer at `hlrad-ng` instead of `hlrad`.

```sh
hlrad-ng -extra -bounce 2 -ambient 0.1 0.1 0.1 mymap
```

What is different from `hlrad`:

- **Speed.** It traces rays in packets on the CPU, and runs direct lighting, patch visibility, the transfers and the light bounces on the graphics card when it can. The gain grows with the settings: with the [maximum-quality preset](max-quality.md) it is more than ten times faster than `hlrad` on a card with hardware ray tracing, and about five times faster with no card at all.
- **Lightmaps are not byte-identical to `hlrad`'s.** Most lightmap bytes are equal; some differ by one brightness level, and a few points at shadow edges by more. A very dim switchable-light map can appear on, or be missing from, a face that only just reaches the cut-off. `hlrad` is unchanged and remains the reference.
- **No patch limit with `-vismatrix normal`.** The limit is memory: patches² / 16 bytes.
- **`-vismatrix sparse` (the default) uses the normal matrix** while that stays below 1 GB, because it gives the same result faster.
- **The log has one line per phase**, with `(GPU)` after the phases that ran on the graphics card.

### Graphics card

`hlrad-ng` needs nothing installed. It opens the Vulkan loader that graphics drivers bring along (`vulkan-1.dll` on Windows, `libvulkan.so.1` on Linux) when it starts lighting; without it, or without a usable card, it lights the map on the CPU.

| Card | What happens |
|------|--------------|
| Hardware ray tracing (`VK_KHR_ray_query`: NVIDIA RTX, AMD RX 6000 or newer, Intel Arc) | Rays are traced in hardware. This is the fast path. |
| Other graphics card with Vulkan 1.1 | The shaders walk the BSP tree themselves; about as fast as a current 8-core CPU. A map with opaque entities is lit on the CPU. |
| Integrated graphics without ray tracing, or no Vulkan | Not used, unless forced with `HLRAD_NG_GPU=1`. |

The card is used from 4,096 patches up; smaller maps are done sooner on the CPU. Some maps keep part of the work on the CPU:

- An opaque entity with a custom shadow (`zhlt_customshadow`), a switchable shadow (`light_shadow`) or a `{` texture: the whole map is lit on the CPU.
- `-incremental`, `-rgbtransfers`, `-customshadowwithbounce`, a translucent texture, or transfers that do not fit in video memory: transfers and bounces run on the CPU; direct lighting and patch visibility still use the card.
- `-vismatrix off`, or the default `-vismatrix sparse` on a map whose normal matrix would exceed 1 GB (about 131,000 patches): patch visibility, transfers and bounces run on the CPU. `-vismatrix normal` keeps them on the card.
- `-noskyfix`, `-texlightgap`, or more than eight `light_environment` entities: direct lighting runs on the CPU.

On the card the transfers are stored in the 16-bit format (`-compress 1`) whatever `-compress` says.

### Environment variables

These are for comparing and troubleshooting; a normal compile needs none of them.

| Variable | Effect |
|----------|--------|
| `HLRAD_NG_GPU=0` | Do not use the graphics card. |
| `HLRAD_NG_GPU=1` | Use whatever Vulkan device there is, also for small maps. |
| `HLRAD_NG_GPU_RAYQUERY=0` | Do not use hardware ray tracing; the shaders walk the BSP tree. |
| `HLRAD_NG_GPU_RADIOSITY=0` | Keep transfers and bounces on the CPU. |
| `HLRAD_NG_GPU_CHECK=1` | Also gather every 64th point on the CPU and report how far the card's result is off (`2`: every point; slow). |
| `HLRAD_NG_GPU_VALIDATE=1` | Turn on the Vulkan validation layer, if the Vulkan SDK is installed. |
| `HLRAD_NG_NOPACKETS=1` | Trace every CPU ray one at a time, as `hlrad` does. |
| `HLRAD_NG_NOAVX2=1` | Use 4-wide instead of 8-wide ray packets on the CPU. |
| `HLRAD_NG_NOFASTBOUNCE=1` | Use `hlrad`'s bounce loop on the CPU. |
| `HLRAD_NG_LEGACY=1` | Light the lightmap samples face by face on the CPU, as `hlrad` does. |

## ripent Options

| Option | Description |
|--------|-------------|
| `-export` | Export entity data. |
| `-import` | Import entity data. |
| `-parse` | Parse and format entity data. |
| `-textureexport` | Export texture data. |
| `-textureimport` | Import texture data. |
| `-textureparse` | Parse and format texture data. |
| `-writeextentfile` | Create extent file for the map. |
| `-deleteembeddedlightmaps` | Delete textures created by `hlrad`. |
| `-pause` | Pause before exit. |
| `mapfile` | The map file to process. |

Example:

```sh
ripent -export mymap
```
