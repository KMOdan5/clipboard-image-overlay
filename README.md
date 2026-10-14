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

You can also change how the box fits images: right-click `Clipboard Image` →
**Transform → Edit Transform** → **Bounding Box Type**.

| Bounding Box Type | Result |
|---|---|
| Scale to inner bounds only if larger (default) | Large images shrink to fit; small images stay as they are |
| Scale to inner bounds | Every image is scaled to fill the box (small images are enlarged) |
| No bounds | Every image is shown at its original size |

> If you create your own image source and name it `Clipboard Image`, the plugin uses it as-is
> and does not add a bounding box. Images are then shown at their original size unless you set one.

### 表示位置・大きさの変え方(日本語)

画像は **`Clipboard Image`** という1つの画像ソースで表示されます。シーンに初めて追加されたときは、
画面中央に置かれ、画面の60%の大きさの「枠(バウンディングボックス)」が設定されます。
画像はこの枠に収まるように表示されます(大きい画像は縮小、小さい画像は元の大きさのまま。拡大はしません)。

- **位置や大きさを変えたい**: プレビュー上で `Clipboard Image` を動かしたり、大きさを変えたりしてください。
  大きさを変えると**枠**の大きさが変わり、以降に貼る画像はすべて新しい枠に収まります。
  一度調整した位置・大きさを、プラグインが元に戻すことはありません。
- **収め方を変えたい**: `Clipboard Image` を右クリック →「変換」→「変換の編集」→「バウンディングボックスの種類」
  - 「大きい場合のみ内側の境界に合わせてサイズ変更」(既定): 大きい画像だけ縮小
  - 「内側の境界に合わせてサイズ変更」: 小さい画像も枠いっぱいに拡大
  - 「境界なし」: すべて元の大きさで表示
- 自分で作った画像ソースに `Clipboard Image` と名前を付けた場合は、そのソースがそのまま使われ、枠は設定されません。
  画像ごとに元の大きさで表示されるので、必要なら上の手順で枠を設定してください。

## Building

This project is based on the [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate). Builds for Windows, macOS and Linux run on GitHub Actions; download the artifacts from a workflow run to test.

## License

GPL-2.0
