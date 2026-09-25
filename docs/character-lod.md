# Character model LOD

The original game switches character geometry at a camera distance of 300 game
units. The port exposes a separate **Character LOD distance** slider (1x–5x).
Modern and the default select 5x (1500 units); Original selects 1x (300 units).
Settings from an older build without `graphics.model_lod_distance` default to 5x.
An explicitly saved value is preserved.
The setting applies immediately and is saved with the other graphics settings.
It is shared by Android and desktop builds.

## Verified path in the original code

- `func_8001DAC8` computes the Euclidean camera distance using `sqrtf`, then
  stores it at actor offset `0x9C` (`0x8001DE34`). It is not squared distance.
- The shared character update `func_80072A60` calls `func_80073E80`.
- `func_80073E80` compares that distance against 300.0f (`0x43960000`), selects
  LOD 0/1, and stores the current selection at actor offset `0xCA`.
- When the selection changes, the byte table at `0x802BD870`, indexed by
  `actor.type * 2 + lod`, supplies the geometry offset to `func_8002439C`.
  That helper updates node geometry indices (`+0x6`) from their base (`+0x8`).
- The existing code refreshes associated parts through `func_80076B64` where
  required. `func_80077164` also reads the same LOD state for character-specific
  rendering. These paths are preserved.

The only generated-code change calls `doraemon_model_lod_limit` immediately
after loading the comparison threshold at `0x80073EA8`. The helper multiplies
the threshold by an independently configured atomic value. It does not alter
the real cached distance, actor lifetime, animation state, or object culling.
The hook is recorded in `recomp/patches`, not just edited in local generated C.

This is the confirmed character LOD path, not a claim that every object or NPC
uses it. The existing Object / NPC draw distance setting controls visibility
and unloading independently. RT64's `f3dex.forceBranch` already forces RSP
branch decisions, so changing that option is not the fix for this CPU-side LOD.

## Manual check

Keep other graphics/camera settings fixed. Compare the same character at 1x,
3x, and 5x while moving the camera through the old switch distance. Verify that
the more detailed model persists farther away and that parts/held items remain
consistent. Restart to check persistence. Selecting Original should restore
the original threshold; selecting Modern should set 5x.

Source analysis and exact patch replay were verified. The maintainer confirmed
the LOD adjustment works in Android r38. The release uses 5x for Modern/default
at their request. The agent does not install or run Android APKs.
