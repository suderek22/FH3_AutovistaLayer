# FH3_AutovistaLayer v1.0.0

**Author** · suderek22

FH3_AutovistaLayer brings existing Forzavista geometry into Forza Horizon 3 free roam alongside LOD0. Depending on the car, this can include detailed engines, engine bays, hood liners, and trunk interiors.

## How it works

The DLL detects the car's built-in LODS geometry as the game loads it and makes those parts available at LOD0. When the mod finds matching standard engine-bay or trunk parts, it suppresses their LOD0 meshes to reduce overlap. The mod uses the car's existing assets and makes its rendering changes in memory.

## Requirements

- Forza Horizon 3 Opus 1.0.125.0 running in Azimuth.
- FH3 Mod Platform 1.4.2 or later.
- Car Detail set to Ultra in Azimuth to use LOD0 in gameplay.

## Installation

1. Close FH3 and Azimuth.
2. Copy `FH3_AutovistaLayer.dll` into the `fh3-mods` folder beside `azimuth.exe`, replacing any earlier version.
3. Launch FH3. The mod takes effect automatically when a car renders at LOD0.

To uninstall, move the DLL out of `fh3-mods` while Azimuth is closed.

FH3 Mod Platform checks GitHub Releases for newer versions. [AutovistaLayer releases](https://github.com/suderek22/FH3_AutovistaLayer/releases)

## Known issues

- Some cars lack Forzavista parts, so the available detail varies by car.
- A flat LOD0 engine-bay or trunk surface can still overlap the detailed geometry if the mod cannot match it to a showroom replacement.
- Custom car packs remain untested.
