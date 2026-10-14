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
- [x] Pause/resume the auto-hide timer with a hotkey (hide manually with OBS's own controls)
- [x] Settings dialog (display time, maximum size, always on top)
- [ ] History dock (v2)

## Showing and hiding

- Each image **fades in**, stays for **7 seconds** (changeable in the settings), then **fades out** automatically.
- Press the hotkey again while an image is shown: the current image **fades out**, the new clipboard image **fades in**, and the 7 seconds **restart**.
- When the time is up, `Clipboard Image` is hidden in **every scene**, so nothing is left on screen even if you switched scenes.
- If OBS is closed while an image is shown, it is hidden right away the next time OBS starts (or when you switch scene collections).
- The fade uses the source's **Show/Hide Transition** (right-click `Clipboard Image` → Show Transition / Hide Transition).
  The plugin sets a fade **only when it first adds the source to a scene**. After that it never changes the transitions,
  so if you choose another transition or set it to none, your choice is kept.
- The display time can be changed in the settings (0 = keep showing until you hide it yourself).
  To change the fade length, edit the Show/Hide Transition in OBS.

### Pausing and hiding manually

- **Pause/Resume Auto-Hide Timer** hotkey (Settings → Hotkeys): press while an image is shown to stop the countdown,
  press again to resume from the remaining time. It does nothing when no image is shown.
- Pasting a new image always resets the countdown to 7 seconds. If the timer is paused, it **stays paused**
  (the next image replaces the current one and keeps showing).
- To hide the image yourself, use OBS's own controls: the **eye icon** in the Sources list, or the
  **Hide 'Clipboard Image'** hotkey in Settings → Hotkeys (OBS lists it per scene, so set it in each scene you use).
- Whenever the image is hidden (manually or automatically), the pause is released, so the next image disappears
  after 7 seconds as usual. After restarting OBS, nothing is paused.
- If you show `Clipboard Image` yourself (eye icon or OBS's **Show 'Clipboard Image'** hotkey), or add it to a scene
  as a visible source, it is treated like a paste: it disappears after 7 seconds, and the pause hotkey works.
- Removing `Clipboard Image` from a scene (or deleting the source) counts as hiding it. The pause is released once
  the image is no longer shown in any scene.

### 表示と自動で消える動き(日本語)

- 画像は**フェードイン**し、**7秒**(設定で変更できます)表示したあと、自動で**フェードアウト**して消えます。
- 表示中にもう一度ホットキーを押すと、今の画像が**フェードアウト**し、新しくコピーした画像が**フェードイン**します。7秒は**最初から数え直します**(延長)。
- 時間が来ると、**すべてのシーン**の `Clipboard Image` を非表示にします。途中でシーンを切り替えても、画像が出しっぱなしになりません。
- 画像の表示中に OBS を終了した場合は、次に OBS を起動したとき(シーンコレクションを切り替えたときも)すぐに非表示にします。
- フェードには、ソースの「**表示トランジション**」「**非表示トランジション**」を使っています(`Clipboard Image` を右クリック)。
  プラグインがフェードを設定するのは、**シーンに初めて追加したときだけ**です。その後は一切変えないので、
  別のトランジションや「なし」に変えた場合は、その設定がずっと使われます。
- 表示時間は設定で変えられます(0秒にすると、自分で消すまで表示し続けます)。
  フェードの長さは、OBS の「表示トランジション」「非表示トランジション」の画面で変えられます。

### 一時停止と手動で消す方法(日本語)

- 「設定」→「ホットキー」の **「自動で消すタイマーを一時停止/再開」**: 画像の表示中に押すとカウントが止まり、
  もう一度押すと残り時間から再開します。画像が出ていないときは何もしません。
- 新しい画像を貼ると、残り時間はいつも7秒に戻ります。一時停止中に貼った場合は**一時停止のまま**です(画像だけ差し替わり、出たままになります)。
- 自分で消したいときは、OBS 標準の機能を使ってください。ソース一覧の**目のアイコン**か、「設定」→「ホットキー」の
  **「'Clipboard Image' を非表示」**です(OBS ではシーンごとに並ぶので、使うシーンそれぞれで割り当ててください)。
- 画像が非表示になると(手動でも自動でも)、一時停止は解除されます。次に貼った画像は、普段どおり7秒で消えます。
  OBS を起動し直したときも、一時停止していない状態から始まります。
- 自分で `Clipboard Image` を表示した場合(目のアイコンや、OBS 標準の「'Clipboard Image' を表示」ホットキー)や、
  表示した状態でシーンに追加した場合も、貼ったときと同じ扱いです。7秒で消え、一時停止ホットキーも使えます。
- シーンから `Clipboard Image` を削除した場合(ソースごと削除した場合も)は、非表示と同じ扱いです。
  どのシーンにも表示されなくなった時点で、一時停止は解除されます。

## Settings

Open **Tools → Clipboard Image Settings...** in OBS.

| Setting | Default | What it does |
|---|---|---|
| Display time | 7 s | Images disappear automatically after this time. **0** keeps them shown until you hide them yourself (the pause hotkey then does nothing). |
| Fit large images within N% of the screen | On, 60% | Images larger than N% of the canvas width or height are scaled down to fit. Works only while the **Bounds Type** of `Clipboard Image` is **Automatic**. When off, the plugin never changes the size. |
| Always show on top | On | Moves `Clipboard Image` to the top of the sources every time you paste. When off, the plugin never changes the layer order. |

- **Apply** saves without closing the window, so you can keep OBS in use and paste images to try the settings.
- Changes apply from the next image you show (an image already on screen keeps its timer and size).
- Settings are shared by all scene collections and saved in the plugin's config folder
  (Windows: `%APPDATA%/obs-studio/plugin_config/obs-clipboard-image/settings.json`).
- The fade length is not a plugin setting. Change it with the Show/Hide Transition of `Clipboard Image`.

### 設定(日本語)

OBS の「**ツール**」→「**Clipboard Image の設定...**」で開きます。

| 項目 | 既定 | 内容 |
|---|---|---|
| 表示時間 | 7秒 | この時間がたつと自動で消えます。**0** にすると自動で消さず、自分で消すまで表示し続けます(一時停止ホットキーは何もしません)。 |
| 大きい画像を画面の N% に収める | オン・60% | 画面の幅・高さの N% を超える画像を、収まるように縮小します。`Clipboard Image` の**境界の種類が「自動」**のときだけ働きます。オフにすると大きさを一切変えません。 |
| 常に手前に表示する | オン | 貼るたびに `Clipboard Image` を一番手前に移します。オフにすると重なり順を一切変えません。 |

- 「**適用**」を押すと、画面を開いたまま保存します。開いたまま OBS を操作したり画像を貼ったりできるので、試しながら調整できます。
- 変更は、次に画像を表示したときから反映されます(表示中の画像のタイマーや大きさは変わりません)。
- 設定はすべてのシーンコレクションで共通です。プラグインの設定フォルダに保存されます
  (Windows: `%APPDATA%/obs-studio/plugin_config/obs-clipboard-image/settings.json`)。
- フェードの長さはプラグインの設定ではありません。`Clipboard Image` の「表示トランジション」「非表示トランジション」で変えてください。

## Adjusting where images appear

The plugin shows every image through one image source named **`Clipboard Image`**.
The first time it is added to a scene, it is placed in the center of the screen.

**Size:** each time you paste, the plugin looks at the image size.
Images larger than **60% of the canvas width or height** are scaled down to fit (the aspect ratio is kept).
Smaller images are shown at their original size (they are never enlarged).
The limit (60% by default) and whether to fit images at all can be changed in the settings.

**What the plugin changes after the first time:** the image file, the size (while Bounds Type is Automatic),
the layer order (moved to the top on every paste, see below), and visibility. Position, bounds, and transitions
are set once when the source is first added to a scene and are never changed again.

**Layer order:** `Clipboard Image` is moved to the **top** every time you paste, so it is never hidden behind
sources you add later (OBS puts new sources on top). If you manage the layer order yourself,
turn off **Always show on top** in the settings; the plugin then never changes the layer order.

**Position:** move `Clipboard Image` in the OBS preview to wherever you like.
The plugin keeps your position for every image you paste afterwards.
Resizing the source by dragging only affects the image currently shown; the next image is sized by the 60% rule again.

**Bounds:** the plugin creates the source with **Automatic** bounds, the same as OBS's default
(right-click → Transform → Edit Transform → Bounds). The 60% sizing only works while the bounds
type is **Automatic**. If you change it to another type (for example **Fit**), the plugin no longer
changes the size and OBS sizes every image by the bounding box. Set it back to **Automatic** to use the 60% sizing again.

### 表示位置・大きさの変え方(日本語)

画像は **`Clipboard Image`** という1つの画像ソースで表示されます。シーンに初めて追加されたときは、画面中央に置かれます。

**初回以降にプラグインが変えるのは、画像・表示サイズ(境界が「自動」のときだけ)・重なり順(下記)・表示/非表示だけです。**
位置・境界・トランジションは、シーンに初めて追加したときに設定するだけで、その後は変えません。

- **重なり順**: 貼るたびに `Clipboard Image` を**一番手前**に移します。OBS では後から追加したソースが一番上に置かれるため、
  テロップや枠を後で追加しても画像がその下に隠れないようにするためです。
  重なり順を自分で管理したい場合は、設定で「**常に手前に表示する**」をオフにしてください。重なり順を一切変えなくなります。

- **大きさ**: 貼るたびに画像の大きさを見て、**画面の幅・高さの60%** を超える画像だけ、収まるように縮小します(縦横比は保ちます)。
  小さい画像は元の大きさのまま表示します(拡大はしません)。割合(既定60%)と、収めるかどうかは設定で変えられます。
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
