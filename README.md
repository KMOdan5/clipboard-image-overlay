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
- [x] Fade in → show for 7 seconds → fade out (press the hotkey again to extend)
- [ ] Pinned mode (hide manually)
- [ ] History dock (v2)

## Showing and hiding

- Each image **fades in**, stays for **7 seconds**, then **fades out** automatically.
- Press the hotkey again while an image is shown to paste the new clipboard image and **restart the 7 seconds**.
- When the time is up, `Clipboard Image` is hidden in **every scene**, so nothing is left on screen even if you switched scenes.
- The fade uses the source's **Show/Hide Transition** (right-click `Clipboard Image` → Show Transition / Hide Transition).
  The plugin only sets a fade when no transition is set, so you can choose another transition and it will be kept.
- The display time (7 s) and fade duration (0.4 s) are fixed for now and will be configurable in the settings dialog.

### 表示と自動で消える動き(日本語)

- 画像は**フェードイン**し、**7秒**表示したあと、自動で**フェードアウト**して消えます。
- 表示中にもう一度ホットキーを押すと、新しくコピーした画像に差し替わり、**7秒を最初から数え直します**(延長)。
- 時間が来ると、**すべてのシーン**の `Clipboard Image` を非表示にします。途中でシーンを切り替えても、画像が出しっぱなしになりません。
- フェードには、ソースの「**表示トランジション**」「**非表示トランジション**」を使っています(`Clipboard Image` を右クリック)。
  プラグインがフェードを設定するのは未設定のときだけなので、別のトランジションに変えた場合はそれが使われます。
- 表示時間(7秒)とフェードの時間(0.4秒)は今は固定で、設定画面ができたら変えられるようにします。

## Adjusting where images appear

The plugin shows every image through one image source named **`Clipboard Image`**.
The first time it is added to a scene, it is placed in the center of the screen.

**Size:** each time you paste, the plugin looks at the image size.
Images larger than **60% of the canvas width or height** are scaled down to fit (the aspect ratio is kept).
Smaller images are shown at their original size (they are never enlarged).
The 60% limit is fixed for now and will be configurable in the settings dialog.

**Position:** move `Clipboard Image` in the OBS preview to wherever you like.
The plugin keeps your position for every image you paste afterwards.
Resizing the source by dragging only affects the image currently shown; the next image is sized by the 60% rule again.

**Bounds:** the plugin creates the source with **Automatic** bounds, the same as OBS's default
(right-click → Transform → Edit Transform → Bounds). The 60% sizing only works while the bounds
type is **Automatic**. If you change it to another type (for example **Fit**), the plugin no longer
changes the size and OBS sizes every image by the bounding box. Set it back to **Automatic** to use the 60% sizing again.

### 表示位置・大きさの変え方(日本語)

画像は **`Clipboard Image`** という1つの画像ソースで表示されます。シーンに初めて追加されたときは、画面中央に置かれます。

- **大きさ**: 貼るたびに画像の大きさを見て、**画面の幅・高さの60%** を超える画像だけ、収まるように縮小します(縦横比は保ちます)。
  小さい画像は元の大きさのまま表示します(拡大はしません)。60%は今は固定で、設定画面ができたら変えられるようにします。
- **位置**: プレビュー上で `Clipboard Image` を好きな場所に動かしてください。以降に貼る画像も、その位置に表示されます。
  ドラッグで大きさを変えた場合は、今表示している画像だけが変わります。次の画像は、また60%のルールで大きさが決まります。
- **境界**: プラグインは、OBS の既定と同じ「**自動**」でソースを作ります(右クリック →「変換」→「変換の編集」→「境界」)。
  **60%に収める大きさの調整は、境界が「自動」のときだけ働きます。**
  「内側に合わせる」など別の境界に変えると、プラグインは大きさを変えなくなり、すべての画像が境界の枠で大きさを決められます。
  60%の調整に戻したいときは、境界を「自動」に戻してください。

## Building

This project is based on the [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate). Builds for Windows, macOS and Linux run on GitHub Actions; download the artifacts from a workflow run to test.

## License

GPL-2.0
