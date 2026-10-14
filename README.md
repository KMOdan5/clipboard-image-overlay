# obs-clipboard-image

Paste images from your clipboard into OBS with a single hotkey — they appear on stream and fade out automatically after a few seconds.

> **Status:** early development. Not ready for use yet.

## What it does

- Copy an image (from a browser, chat app, or a screenshot taken with `Win+Shift+S`)
- Press the hotkey in OBS
- The image fades in on your current scene and fades out after N seconds — no saving files, no forgetting to hide it

Built for just-chatting streams, where you want to say "look at this" without breaking the flow.

## 日本語

コピーした画像を、ホットキー1つでそのまま配信画面に出せるOBSプラグインです。数秒後には自動で消えるので、出しっぱなしの心配もありません。雑談配信で「これ見て」とサッと見せたいときのために作っています。

## Planned features

- [ ] Paste image data or copied image files from the clipboard
- [ ] Auto placement at a preset position and size
- [ ] Fade in → show for N seconds → fade out (with extend)
- [ ] Pinned mode (hide manually)
- [ ] History dock (v2)

## Building

This project is based on the [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate). Builds for Windows, macOS and Linux run on GitHub Actions; download the artifacts from a workflow run to test.

## License

GPL-2.0
