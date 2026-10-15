# obs-clipboard-image

**English** | [日本語](README.ja.md)

An OBS plugin that shows a copied image on your stream with a single hotkey. The image is hidden automatically after a few seconds. Made for just-chatting streams, so you can say "look at this" without saving the file first.

> **This is a beta version.** If you find bugs, have requests, or notice anything, please let us know via [Bug reports and feature requests](#bug-reports-and-feature-requests).

## Features

### Show a copied image without saving it

Copy an image and press the hotkey to show it on your stream. There is no need to save it as a file.

- Images copied from a browser or chat app
- Screenshots taken with `Win+Shift+S`
- Selections copied from paint tools (transparent areas stay transparent)
- Image files copied in File Explorer (GIF animations are played)

Large images are scaled down automatically to fit 60% of the canvas (changeable in the settings).

### Hidden automatically after a while

Images fade in and fade out after the display time (10 seconds by default), so they are never left on screen.

- Showing another image while one is on screen switches the image and restarts the display time
- Press the pause hotkey to keep the image on screen
- The transition effect (fade, etc.) can be changed with OBS's built-in Show/Hide Transition

### Show images again from the history (coming soon)

You will be able to show an image again from a list of images you have shown.

## Privacy Policy

- This plugin does not connect to the internet and never sends images anywhere.
- Displayed images are saved only in the plugin's folder on your computer.
- AI is used as an aid in developing the system, but this plugin has no AI features and never uses images to train AI.

## Requirements

- Windows 10 / 11
- OBS Studio 31.1.1 or later

Files for macOS and Linux are also provided, but they have not been tested.

## Installation and usage

See the [manual (Japanese)](docs/manual/ja.md). An English manual will be added in version 1.0.

## Bug reports and feature requests

Please use the templates in [Issues](https://github.com/KMOdan5/obs-clipboard-image/issues/new/choose). For security issues, follow the instructions in [SECURITY.md](SECURITY.md).

## License

GPL-2.0
