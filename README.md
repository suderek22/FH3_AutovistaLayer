# FH3_AutovistaLayer v1.1.0

<img width="480" height="270" alt="Zrzut ekranu 2026-11-23 011423" src="https://github.com/user-attachments/assets/b3c2e853-50a6-43bb-97b4-d35b6d7a1737" />
<img width="480" height="270" alt="Zrzut ekranu 2026-11-23 011629" src="https://github.com/user-attachments/assets/9f3edb5a-22bc-4e25-a257-eaeacaf10eec" />
<img width="480" height="270" alt="Zrzut ekranu 2026-11-23 011728" src="https://github.com/user-attachments/assets/794b035b-4cb6-4e10-b6d6-caec4727d45f" />
<img width="480" height="270" alt="Zrzut ekranu 2026-11-23 011835" src="https://github.com/user-attachments/assets/5ddf8604-2513-4e77-8f31-3a608af91f9e" />
<img width="480" height="270" alt="Zrzut ekranu 2026-11-23 012101" src="https://github.com/user-attachments/assets/9de80418-010a-4825-842e-3e7f37082801" />
<img width="480" height="270" alt="Zrzut ekranu 2026-11-23 012430" src="https://github.com/user-attachments/assets/33a25784-4847-4436-a907-67e47b6a9b8e" />


FH3_AutovistaLayer brings existing Forzavista geometry into Forza Horizon 3 free roam alongside LOD0. Depending on the car, this can include detailed engines, engine bays, hood liners, and trunk interiors.

## How it works

The DLL detects the car's built-in LODS geometry as the game loads it and makes those parts available at LOD0. When the mod finds matching standard engine-bay or trunk parts, it suppresses their LOD0 meshes to reduce overlap. The mod uses the car's existing assets and makes its rendering changes in memory.

## Autoshow preview

After choosing a car and its colour, look for **Y Explode** beside **X Change Camera** on the final purchase preview. Press **Y** again to Implode. This uses the car's existing Forzavista animations so you can inspect its engine bay and interior before purchasing.

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
- Custom car packs remain untested.
