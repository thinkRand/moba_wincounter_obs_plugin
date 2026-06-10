# MOBA WinCounter

OBS plugin that tracks match outcomes for multiple MOBAs (MLBB, Honor of Kings, etc.) by recognizing victory/defeat screens from video frames using OpenCV SIFT + FLANN + homography (RANSAC).

Developed using AI

## Features

- SIFT-based template matching with FLANN + RANSAC homography
- Multi-game support (MLBB, HoK, etc.) — templates organized by game key
- Multi-language support — templates organized by language subdirectory
- Scene trigger: show/hide browser source animations on victory/defeat
- Configurable text source labels with `{w}` / `{d}` tokens

## Template Images

Place `{game_key}_victory.png` and `{game_key}_defeat.png` in `data/templates/{lang}/`
(e.g., `data/templates/en/mlbb_victory.png`). 

## Sopport 
Use my link on ko-fi.com/thinkRand to support the development

