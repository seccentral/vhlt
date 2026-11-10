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
| `-texdata #` | Alter maximum texture memory limit (KB). |
| `-lightdata #` | Alter maximum lighting memory limit (KB). |
| `-chart` | Display BSP statistics. |
| `-low` / `-high` | Run at altered process priority. |
| `-nolog` | Do not generate compile log files. |
| `-threads #` | Manually specify thread count (1 to 64; larger values are clamped). |
| `-estimate` | Display estimated time during compile. |
| `-verbose` | Verbose messages. |
| `-noinfo` | Do not show tool configuration information. |
| `-dev #` | Developer message level. |

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
