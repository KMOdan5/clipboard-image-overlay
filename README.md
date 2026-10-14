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

- [x] Paste image data or copied image files from the clipboard (copied GIFs keep their animation)
- [x] Auto placement at a preset position and size
- [ ] Fade in → show for N seconds → fade out (with extend)
- [ ] Pinned mode (hide manually)
- [ ] History dock (v2)

## Adjusting where images appear

The plugin shows every image through one image source named **`Clipboard Image`**.
The first time it is added to a scene, it is placed in the center and given a
bounding box of 60% of the canvas. Each image is fitted inside that box:
large images are scaled down, small images keep their original size (they are never scaled up).

To change where images appear, move or resize `Clipboard Image` in the OBS preview.
Resizing changes the **box**, not the current image, so every image you paste afterwards
fits into the new box. The plugin never moves the source back once you have adjusted it.

You can also change how the box fits images. Right-click `Clipboard Image` →
**Transform → Edit Transform**, then change the **Bounding Box Type** in the **Bounds** section.
In the same dialog, **Size** sets the box size in pixels, and the alignment grid next to
the bounding box type decides where a smaller image sits inside the box.

| Bounding Box Type | Result |
|---|---|
| Maximum size only (set by this plugin) | Large images shrink to fit; small images stay as they are |
| Scale to inner bounds | Every image is scaled to fit inside the box (small images are enlarged) |
| No bounds (OBS's default for new sources) | No box; every image is shown at its original size |

The exact labels may differ slightly between OBS versions.

> OBS creates new image sources with no bounding box. This plugin sets **Maximum size only**
> when it creates `Clipboard Image`, so images of different sizes always fit on screen.
> If you create your own image source and name it `Clipboard Image`, the plugin uses it as-is
> and does not change its bounding box. Set **Maximum size only** yourself if you want the same behavior.

### 表示位置・大きさの変え方(日本語)

画像は **`Clipboard Image`** という1つの画像ソースで表示されます。シーンに初めて追加されたときは、
画面中央に置かれ、画面の60%の大きさの「境界(枠)」が設定されます。
画像はこの枠に収まるように表示されます(大きい画像は縮小、小さい画像は元の大きさのまま。拡大はしません)。

- **位置や大きさを変えたい**: プレビュー上で `Clipboard Image` を動かしたり、大きさを変えたりしてください。
  大きさを変えると**枠**の大きさが変わり、以降に貼る画像はすべて新しい枠に収まります。
  一度調整した位置・大きさを、プラグインが元に戻すことはありません。
- **収め方を変えたい**: `Clipboard Image` を右クリック →「変換」→「変換の編集」を開き、
  「**境界**」のプルダウンを変えます。

  | 境界 | 表示のされ方 |
  |---|---|
  | 最大サイズのみ(このプラグインの既定) | 大きい画像だけ枠に収まるよう縮小。小さい画像は元の大きさ |
  | 内側に合わせる | 小さい画像も枠いっぱいに拡大して表示 |
  | 自動(OBS で普通に作ったときの既定) | 枠なし。すべて元の大きさで表示 |

  同じ画面の「大きさ」で枠の大きさを数値で指定でき、境界の右の「配置」で、枠より小さい画像を枠のどこに置くか(中央・左上など)を選べます。
- **OBS で普通に画像ソースを作ると、境界は「自動」(枠なし)になります。** このプラグインは、大きさの違う画像でも画面に収まるよう、`Clipboard Image` を作るときに「最大サイズのみ」を設定しています。
  自分で作った画像ソースに `Clipboard Image` と名前を付けた場合は、プラグインは境界を変えません。同じ動きにしたいときは、上の手順で境界を「最大サイズのみ」にしてください。

画面の表記は OBS 32(日本語)のものです。バージョンによって多少異なる場合があります。

## Building

This project is based on the [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate). Builds for Windows, macOS and Linux run on GitHub Actions; download the artifacts from a workflow run to test.

## License

GPL-2.0
