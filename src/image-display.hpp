/*
clipboard-image-overlay
Copyright (C) 2026 KMOdan5 <dan5gaogao@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

/**
 * @file image-display.hpp
 * @brief 保存した画像を、現在のシーンに表示する機能の公開窓口。
 *
 * @details 実装は image-display.cpp。呼び出し元は plugin-main.cpp と auto-hide.cpp。
 */

#pragma once

#include "clipboard-capture.hpp"

#include <string>
#include <vector>

/**
 * @brief 画像ファイルを、現在のシーン(配信に出ているシーン)に表示する。
 *
 * @details
 * プラグイン専用の画像ソース「Clipboard Image」を 1 つだけ使い回す。
 * 1. 専用ソースがなければ作り、あれば表示する画像ファイルだけ差し替える
 * 2. 現在のシーン(グループの中を含む)にそのソースがなければ追加する。このときだけ、画面中央に置き(境界は OBS の既定と同じ「自動」)、
 *    表示・非表示トランジションにフェードを設定する
 * 3. 「常に最前面に表示する」がオン(既定)なら、最前面に移す(後から追加されたソースに隠れないように)
 * 4. 画像の大きさに合わせて表示サイズ(「変換の編集」の「大きさ」)を決める
 *    - 画面の幅・高さの「最大サイズの割合」(設定画面で変更。既定 60%)を超える画像は、収まるよう縮小する(縦横比は保つ)
 *    - 小さい画像は等倍(拡大しない)
 *    - 画像や画面の大きさが取れない場合は等倍(元の大きさのまま)
 *    - 境界が「自動」以外のときは、表示サイズを変えない(境界の枠が大きさを決める)
 * 5. 表示する
 *    - 非表示のとき: 画像を設定してフェードイン
 *    - 表示中のとき: フェードアウト → 画像を差し替え → フェードイン
 *      (フェードアウトの時間だけ待ってから差し替える。待っている間に貼られたら、最後の画像を出す)
 *
 * 位置・トランジションを設定するのは、シーンに初めて追加したときだけ。
 * 配信者がその後 OBS 上で変えたものは、上書きしない(トランジションを「なし」にした場合も、そのまま)。
 * 重なり順は「常に最前面に表示する」(設定画面、既定オン)がオンのときだけ、貼るたびに最前面にする。
 * 表示サイズは画像ごとに決め直す(ドラッグで変えた大きさは、次の画像には引き継がない)。
 *
 * @param image 表示する画像の情報(パスと幅・高さ)。
 * @return 表示を開始できたら true(差し替えの場合は、フェードアウト後に表示される)。
 *         現在のシーンが取れない・ソースが作れない場合は false(理由はログに出る)。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
bool show_image_on_current_scene(const SavedImage &image);

/**
 * @brief 全シーンの Clipboard Image を非表示にする。
 *
 * @details
 * 現在のシーンだけでなく、全シーンを対象にする。表示中にシーンを切り替えても、
 * 元のシーンに画像が出しっぱなしにならないようにするため。
 * 同じシーンに 2 つ以上置かれていれば、すべて非表示にする。グループの中に置かれたものも対象にする。
 * Clipboard Image がない、または表示中のものがない場合は何もしない。
 * 差し替えの途中だった場合は、その差し替えを取り消す。
 *
 * @param fade true なら非表示トランジション(フェード)で消す。
 *             false なら即座に消す。起動時に、前回の表示が残っていたものを片付けるときと、
 *             配信者が今の画像を削除するとき(clear_clipboard_image())に使う。
 *
 * @note 呼び出し元: auto-hide.cpp(自動で消すタイマー、fade = true)、
 *       plugin-main.cpp(非表示ホットキー、fade = true / 起動時・シーンコレクション切り替え時、fade = false)、
 *       clear_clipboard_image()(fade = false)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void hide_clipboard_image(bool fade);

/**
 * @brief 全シーンの通知(表示/非表示・追加・削除)を受け取るよう登録する。登録済みのシーンは飛ばす。
 *
 * @details
 * 配信者が Clipboard Image を手動で表示・追加・非表示・削除したことを知るために使う。
 * - 表示・追加されたら、貼ったときと同じく自動で消すカウントを始める(出しっぱなしにしないため)
 * - どのシーンにも表示されなくなったら、一時停止を解除してタイマーを止める
 *
 * 通知はシーン単位でしか受け取れないので全シーンに登録するが、処理するのはプラグインの 'Clipboard Image'
 * (名前が Clipboard Image の画像ソース)のものだけ。他のソースの通知は、名前と種類を比べるだけで何もしない(負荷はほぼない)。
 * 削除されたシーンの登録は、呼ぶたびに一覧から取り除く。
 * グループには登録しないため、グループの中の Clipboard Image を手動で切り替えたことには気づけない(既知の制限。#37 で検討する)。
 *
 * @note 呼び出し元: plugin-main.cpp(起動完了時・シーンコレクション切り替え時・シーン一覧の変更時)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void image_display_watch_scenes();

/**
 * @brief Clipboard Image がいずれかのシーンで表示中かどうかを返す。
 *
 * @details
 * 自動で消す処理は全シーンを対象にするので、「表示中か」も全シーンで判断する。
 * 現在のシーンだけで判断すると、別のシーンに表示が残っているのに一時停止を解除してしまい、
 * その画像が消えなくなるため。
 * 差し替えの途中(フェードアウトして新しい画像を待っている間)も、表示中として扱う。
 *
 * @return 表示中なら true。
 *
 * @note 呼び出し元: auto-hide.cpp(一時停止ホットキーが表示中だけ効くようにするため)、
 *       plugin-main.cpp(貼る前に、残っている一時停止を片付けるため)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
bool is_clipboard_image_showing();

/**
 * @brief 'Clipboard Image' が今使っている画像ファイルのパスを返す。
 *
 * @details
 * 古い画像を掃除するときに、消してはいけないファイルを知るために使う。
 * - 'Clipboard Image' に設定されている画像ファイル(表示中、または最後に表示した画像)
 * - 差し替え待ちの画像(フェードアウトが終わったら表示する画像)
 *
 * @return 画像ファイルのパス(UTF-8)。'Clipboard Image' がなければ差し替え待ちの分だけ(なければ空)。
 *
 * @note 呼び出し元: plugin-main.cpp(cleanup_saved_images() に渡す)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
std::vector<std::string> clipboard_images_in_use();

/**
 * @brief 今の画像('Clipboard Image' が参照している画像ファイル)のパスを返す。
 *
 * @details
 * 「前の画像を表示」ホットキーと履歴ドックのハイライトが、今の画像が履歴のどれかを知るために使う。
 * 非表示になっても、最後に表示した画像が今の画像のまま残る。
 * 差し替えの途中なら、差し替え後に出る画像(差し替え待ちの画像)を返す。
 * それ以外は 'Clipboard Image' に設定されている画像ファイルを返す。
 * 表示中かどうかは見ない。
 *
 * @return 画像ファイルのパス(UTF-8)。'Clipboard Image' がない・画像が設定されていなければ空。
 *
 * @note 呼び出し元: plugin-main.cpp(「前の画像を表示」ホットキー、表示履歴のドックへの問い合わせ、表示履歴からの削除)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
std::string current_clipboard_image();

/**
 * @brief 今の画像を外す。全シーンの 'Clipboard Image' を即座に非表示にし、画像ファイルの参照を空にする。
 *
 * @details
 * 配信者が表示履歴のドックで今の画像を削除するときに、ファイルを削除する前に呼ぶ。
 * - 差し替えの途中なら取り消す
 * - 全シーン(グループの中を含む)の 'Clipboard Image' を、フェードアウトせずに即座に非表示にする。
 *   見せたくない画像を消す操作なので、一瞬も画面に残さないため
 * - 'Clipboard Image' の画像ファイルの設定を空にする。OBS の画像ソースは読み込んだ画像を持っているので、
 *   参照を外さずにファイルだけを消すと、次に表示するまで画像が残り続ける
 *
 * 呼んだ後は今の画像がなくなる(current_clipboard_image() は空を返す)。'Clipboard Image' がなければ何もしない。
 * 自動非表示のタイマーと一時停止は、ここでは解除しない(呼び出し元で auto_hide_on_image_hidden() を呼ぶ)。
 *
 * @note 呼び出し元: plugin-main.cpp の delete_history_image()。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void clear_clipboard_image();

/**
 * @brief 表示処理で使っているタイマーや通知の登録を片付ける。何度呼んでもよい。
 *
 * @details 差し替えの途中なら取り消し、シーンの通知の登録も解除する。
 *          解除する前に UI スレッドへ回した通知は、解除した後に届いても処理しない。
 *          OBS の終了時・起動完了時・シーンコレクション切り替えの前後に呼ぶ。
 *
 * @note 呼び出し元: plugin-main.cpp(画面側のイベント・プラグイン解放時)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void image_display_shutdown();
