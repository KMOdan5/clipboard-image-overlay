# obs-clipboard-image

**English** | [日本語](README.ja.md)

An OBS plugin that shows a copied image on your stream with a single hotkey. The image is hidden automatically after a few seconds. Made for just-chatting streams, so you can say "look at this" without saving the file first.

> **This is a beta version.** It may contain bugs. If you notice anything, please let us know via [Bug reports and feature requests](#bug-reports-and-feature-requests).

## Features

- Shows images copied from a browser or chat app, or screenshots taken with `Win+Shift+S`, with a hotkey
- Images fade in and fade out after the display time (10 seconds by default)
- Showing another image while one is on screen switches the image and restarts the display time
- A pause hotkey keeps the image on screen
- Large images are scaled down automatically to fit 60% of the canvas (changeable in the settings)

## Requirements

- Windows 10 / 11
- OBS Studio 31.1.1 or later

Files for macOS and Linux are also provided, but they have not been tested.

## Installation and usage

See the [manual (Japanese)](docs/manual/ja.md). An English manual will be added in version 1.0.

## Planned features

- History dock: show a previously displayed image again from a list

## Bug reports and feature requests

Please use the templates in [Issues](https://github.com/KMOdan5/obs-clipboard-image/issues/new/choose). For security issues, follow the instructions in [SECURITY.md](SECURITY.md).

## License

GPL-2.0
