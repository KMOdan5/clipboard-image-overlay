# clipboard-image-overlay かんたんスタート

コピーした画像を OBS の配信画面に表示するまでの手順です。初めて使うときに、上から順に進めてください。

## 1. インストールする

1. [リリース](https://github.com/KMOdan5/clipboard-image-overlay/releases)から `clipboard-image-overlay-(版)-windows-x64-installer.exe` をダウンロードします。
2. ダウンロードしたファイルを開きます。
   - 「Windows によって PC が保護されました」と表示された場合は「詳細情報」→「実行」を押します。
   - 「このアプリがデバイスに変更を加えることを許可しますか?」と表示されたら「はい」を押します。
3. 画面の案内に沿ってインストールします。

<!-- 画像: 「Windows によって PC が保護されました」の画面(docs/images/installer-smartscreen.png)。画像を置いたら、次の行のコメントの記号を消す -->
<!-- <img src="../images/installer-smartscreen.png" width="600" alt="「Windows によって PC が保護されました」の画面の「詳細情報」と「実行」"> -->

## 2. OBS で確かめる

OBS を起動し、「ツール」メニューに「Clipboard Image」があることを確かめます。

<!-- 画像: インストール後の「ツール」メニュー(docs/images/tools-menu.png)。画像を置いたら、次の行のコメントの記号を消す -->
<!-- <img src="../images/tools-menu.png" width="600" alt="「ツール」メニューの「Clipboard Image」"> -->

## 3. 画像を表示する

1. 表示したい画像をコピーします。ブラウザでは画像を右クリックして「画像をコピー」を選びます。スクリーンショットは `Win + Shift + S` で範囲を選択します。
2. OBS の「ツール」→「Clipboard Image」→「コピーした画像を表示」を選びます。

現在のシーンに「Clipboard Image」という画像ソースが追加され、画面中央に画像が表示されます。画像は 10 秒経つと自動で非表示になります。

<!-- 画像: コピーしてから表示されるまでの動き(docs/images/demo.gif)。README の GIF と同じもの。画像を置いたら、次の行のコメントの記号を消す -->
<!-- <img src="../images/demo.gif" width="720" alt="コピーした画像が配信画面に表示され、自動で非表示になる様子"> -->

## 4. ホットキーを設定する

OBS の「設定」→「ホットキー」で「画像を 'Clipboard Image' に表示」にキーを割り当てると、メニューを開かずにキー 1 つで画像を表示できます。迷ったときは `Ctrl + Shift + V` がおすすめです。

<!-- 画像: ホットキーの設定画面(docs/images/hotkey-settings.png)。画像を置いたら、次の行のコメントの記号を消す -->
<!-- <img src="../images/hotkey-settings.png" width="600" alt="「設定」→「ホットキー」の「画像を 'Clipboard Image' に表示」"> -->

## もっと知りたいとき

表示時間や大きさの変え方、表示履歴、困ったときの対処は[マニュアル](../manual/ja.md)をご覧ください。
