# clipboard-image-overlay

**English** | [日本語](README.ja.md)

An OBS plugin that shows a copied image on your stream with a single hotkey. The image is hidden automatically after a few seconds. Made for just-chatting streams, so you can say "look at this" without saving the file first.

> **This is a beta version.** If you find bugs, have requests, or notice anything, please let us know via [Bug reports and feature requests](#bug-reports-and-feature-requests).

## Features

### Show a copied image directly on your stream

Just copy an image and press the hotkey to show it on your stream.

For example, you can show images like these:

- Images copied from a browser or chat app
- Screenshots taken with `Win + Shift + S`
- Selections copied from paint tools (transparent areas stay transparent)
- Image files copied in File Explorer (GIF animations are played)

In addition, large images are scaled down automatically so they do not go off the screen.

### Hidden automatically after a while

Images fade in and fade out after the display time (10 seconds by default), so you never have to worry about forgetting to hide them.

- Press the pause hotkey to keep the image on screen
- You can switch to another image even before the display time is up
- The transition effect (fade, etc.) can be changed with OBS's built-in Show/Hide Transition

### Show images again from the history (coming soon)

You will be able to show an image again from a list of images you have shown.

## Privacy Policy

- This plugin does not connect to the internet and never sends images anywhere.
- Displayed images are saved only in the plugin's folder on your computer.
- AI is used as an aid in developing the system, but this plugin has no AI features and never uses images to train AI.

## Terms of Use

- Free to use for both individuals and companies, including monetized streams.
- No credit is required. If you like it, I would be happy if you mention it on your stream or social media.
- Please make sure you have the rights (such as copyright) to the images you show.
- Modifications and redistribution must follow GPL-2.0.

## Requirements

- Windows 10 / 11
- OBS Studio 31.1.1 or later

Files for macOS and Linux are also provided, but they have not been tested.

## Installation and usage

See the [manual (Japanese)](docs/manual/ja.md). An English manual will be added in version 1.0.

## Bug reports and feature requests

Please use the templates in [Issues](https://github.com/KMOdan5/clipboard-image-overlay/issues/new/choose). For security issues, follow the instructions in [SECURITY.md](SECURITY.md).

## License

GPL-2.0
