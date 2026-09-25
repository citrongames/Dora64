# Android touch controls (r32)

The touch overlay is Android-only. It translates fingers to the original N64
buttons/analog stick; it does not change the game's controls or dialogue logic.

## Player controls

- Left analog stick, A/B/Z, START, L/R, and a gear for the port menu.
- Modern camera: drag the free area on the right to look, + / - to zoom.
- Classic camera: horizontal/vertical swipes send C-left/right/up/down pulses.
- L/R remain distinct N64 buttons. R follows the existing physical-input policy:
  suppressed while modern manual camera is active, available in original menus.
- Four corner HUD areas and the bottom dialogue area are clear by default.
- Dialogue keeps the same positions and buttons, with lower opacity; A/START
  retain their original behavior. Camera gestures are inactive during dialogue.
- Intentional controller input hides the overlay and cancels virtual holds.
  First touch reveals it without pressing an unseen button; subsequent touches
  act normally. Controller axis noise and a stationary held stick do not
  immediately hide a newly revealed overlay.
- The port menu hides/blocks game touch controls. Its widgets use SDL's existing
  touch-to-mouse path. Editing also blocks physical/virtual gameplay input.

## Layout settings

Gear > Touch > Edit button positions opens a drag editor. Save persists the
positions; Cancel restores the entry layout; Reset positions restores defaults
inside the editor, so it is still reversible until Save. All ten controls,
including modern zoom buttons, are editable. Size, normal/dialogue opacity and
swipe sensitivity have separate sliders in Touch settings.

Settings live beside the other port settings in `doraemon_touch_settings.json`.
Version 1 stores named normalized positions, global scale, both opacities and
sensitivity. Writes use a temporary file and same-directory rename. Invalid or
unknown data uses defaults/clamped values; save failures remain visible and
can be retried. Positions are independent of internal game render resolution.
The layout uses full SDL window coordinates, including any game letterboxing.
Physical size uses display DPI, with a height cap for small landscape screens.

## Implementation and ownership

- `doraemon_touch_state.hpp`: independently testable finger ownership, hit tests,
  analog stick, camera gestures, quick-tap latches, context transitions/editor.
- `doraemon_touch_layout.hpp`: versioned JSON encoding/validation.
- `doraemon_touch.cpp`: Android adapter; one mutex for SDL event, game input and
  render threads. Rendering takes a snapshot. SDL window/DPI queries stay on the
  event thread. Non-Android builds use inert stubs.
- `doraemon_input.cpp`: game samples merge N64 buttons and give a touched stick
  ownership over physical stick values. Fast press+release between SDL/game
  samples survives one controller read. Touch camera deltas are consumed once.
- `system_overlay.cpp`: Touch tab and overlay draw; menu transitions release
  virtual holds and discard queued camera motion.
- `doraemon_camera.cpp`: publishes configured camera mode and dialogue activity
  as atomics. Dialogue comes from signed halfword `0x800E6B20`, states 1..4,
  read on the game thread in the existing sync hook. This follows
  `func_80012B80`, not a camera-lock or letterbox heuristic.

Touch lifetimes include the SDL touch device and finger IDs. A finger cannot
steal a control from another finger or turn an existing button hold into a
camera drag. Menu, focus loss, background, controller switch, resize, and editor
transitions release virtual input. Android relative mouse capture is disabled
so touch-generated mouse events cannot also rotate the game camera.

## Validation

`tests/touch_controls.cpp` is available via `DORAEMON_BUILD_TESTS` or directly:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -Isrc -Ilib/rt64/src/contrib \
    tests/touch_controls.cpp -o /tmp/dora64-touch-test
/tmp/dora64-touch-test
```

Covers simultaneous stick/A/Z, finger/device ownership, quick taps, menu
blocking, release/reveal behavior, resize, camera gesture modes, dialogue,
editor save/cancel, JSON round trip/invalid values, resolution/DPI scaling.
Existing modern camera regression checks also pass. A separate headless host
harness exercised the actual Android adapter and ImGui geometry with synthetic
SDL events: gear/menu, background/resume, persisted positions, controller drift,
and dialogue visibility. It does not emulate the Android driver or ergonomics.

On 2026-09-25, the user tested r32 on their Android device and confirmed
button input, layout editing/positions, overlay behavior during dialogue and
gamepad use, and swipes with both modern and classic cameras work as intended.
The user approved committing the implementation. The report did not separately
enumerate every extended checklist item (such as cold-restart persistence or
screen-lock cycles); those are not claimed as individually device-verified.
The agent built and inspected the APK but did not install or run it.


## Android port menu (r33)

The Android title is now Port menu. Exit game, the keyboard binding column,
Background gamepad input, Capture mouse and Camera mouse sensitivity are hidden.
Help text describes touch/gamepad input. Existing saved settings and keyboard
bindings remain compatible; this is a UI change, not a settings-file migration.
New Android installs and the Modern profile use 2x game render resolution.
Existing chosen resolution is preserved until the user selects a profile/value.
Original remains 1x. Desktop profiles and menu controls retain their behavior.

Android buttons, tabs, checkboxes, combo rows, scrollbars and slider grabs have
larger hit areas. The taller menu keeps its tabs and Continue/Reset fixed while
each tab body scrolls independently. These sizes use the existing 720p baseline.

Tapping outside the menu closes it like Continue. Raw SDL finger-down handling
consumes the dismissing contact before game controls can receive it. Rendered
menu bounds are published under the touch mutex; active popups prevent this
dismissal. Real mouse clicks use ImGui's outside-click path. The layout editor
retains its explicit Save/Cancel behavior.

Native/optimized APK build passed. Before delivery, headless checks covered
dismissal without game-input leakage and fixed-footer/scroll geometry at 720p,
2560x1600 and 2400x1080; desktop syntax was also checked. These checks did not
discover a new failure. On 2026-09-25, the user tested r33, confirmed the
result worked well, and approved committing the Android menu changes.
The agent did not install or run the APK.
