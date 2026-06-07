# MOBA WinCounter

OBS plugin that tracks match outcomes for multiple MOBAs (MLBB, Honor of Kings, etc.) by recognizing victory/defeat screens from video frames using OpenCV SIFT + FLANN + homography (RANSAC).

## Build

```pwsh
cd obs_plugin
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo
```

| Platform | Toolchain |
|----------|-----------|
| Windows  | Visual Studio 17 2022, CMake 3.30+ |
| macOS    | Xcode 16.0, Homebrew |
| Ubuntu 24.04 | Ninja, `build-essential`, `pkg-config` |

## CI

Builds on all three platforms via GitHub Actions. Windows uses vcpkg for OpenCV, macOS uses Homebrew, Ubuntu uses `libopencv-dev`.

## Features

- SIFT-based template matching with FLANN + RANSAC homography
- Multi-game support (MLBB, HoK, etc.) — templates organized by game key
- Multi-language support — templates organized by language subdirectory
- Scene trigger: show/hide browser source animations on victory/defeat
- Configurable text source labels with `{w}` / `{d}` tokens
- 3-minute cooldown to avoid duplicate counting

## Template Images

Place `{game_key}_victory.png` and `{game_key}_defeat.png` in `data/templates/{lang}/`
(e.g., `data/templates/en/mlbb_victory.png`). Resolution should match the target game.

## Release

Push a semver tag (e.g., `v1.0.0`) to trigger a draft release with packaged artifacts.
