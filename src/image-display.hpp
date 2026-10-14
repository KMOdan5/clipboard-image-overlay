/*
obs-clipboard-image
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
 * @details 実装は image-display.cpp。呼び出し元は plugin-main.cpp の on_show_requested()。
 */

#pragma once

#include "clipboard-capture.hpp"

/**
 * @brief 画像ファイルを、現在のシーン(配信に出ているシーン)に表示する。
 *
 * @details
 * プラグイン専用の画像ソース「Clipboard Image」を 1 つだけ使い回す。
 * 1. 専用ソースがなければ作り、あれば表示する画像ファイルだけ差し替える
 * 2. 現在のシーンにそのソースがなければ追加する。このときだけ、画面中央に置き(境界は OBS の既定と同じ「自動」)、
 *    表示・非表示トランジションにフェードを設定する
 * 3. 「常に手前に表示する」がオン(既定)なら、最前面に移す(後から追加されたソースに隠れないように)
 * 4. 画像の大きさに合わせて表示サイズ(「変換の編集」の「大きさ」)を決める
 *    - 画面の幅・高さの 60% を超える画像は、収まるよう縮小する(縦横比は保つ)
 *    - 小さい画像は等倍(拡大しない)
 *    - 画像や画面の大きさが取れない場合は等倍(元の大きさのまま)
 *    - 境界が「自動」以外のときは表示サイズを変えない(境界の枠が大きさを決める)
 * 5. 表示する
 *    - 非表示のとき: 画像を設定してフェードイン
 *    - 表示中のとき: フェードアウト → 画像を差し替え → フェードイン
 *      (フェードアウトの時間だけ待ってから差し替える。待っている間に貼られたら、最後の画像を出す)
 *
 * 位置・トランジションを設定するのは、シーンに初めて追加したときだけ。
 * 配信者がその後 OBS 上で変えたものは、上書きしない(トランジションを「なし」にした場合も、そのまま)。
 * 重なり順は「常に手前に表示する」がオンのときだけ、貼るたびに最前面にする(#8 で切り替え可能にする)。
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
 * Clipboard Image がない、または表示中のものがない場合は何もしない。
 * 差し替えの途中だった場合は、その差し替えを取り消す。
 * あわせて、Clipboard Image のあるシーンの表示/非表示の通知を受け取るよう登録する
 * (起動完了時にも呼ぶので、OBS を起動し直した直後に手動で表示した場合も、自動で消すカウントを始められる)。
 *
 * @param fade true なら非表示トランジション(フェード)で消す。
 *             false なら即座に消す。起動時に、前回の表示が残っていたものを片付けるときに使う。
 *
 * @note 呼び出し元: auto-hide.cpp(自動で消すタイマー、fade = true)、
 *       plugin-main.cpp(起動時・シーンコレクション切り替え時、fade = false)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void hide_clipboard_image(bool fade);

/**
 * @brief 現在のシーンで Clipboard Image が表示中かどうかを返す。
 *
 * @details 差し替えの途中(フェードアウトして新しい画像を待っている間)も、表示中として扱う。
 *
 * @return 表示中なら true。
 *
 * @note 呼び出し元: auto-hide.cpp(一時停止ホットキーが表示中だけ効くようにするため)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
bool is_clipboard_image_showing();

/**
 * @brief 表示処理で使っているタイマーや通知の登録を片付ける。何度呼んでもよい。
 *
 * @details 差し替えの途中なら取り消し、シーンの表示/非表示の通知の登録も解除する。
 *          OBS の終了時・起動完了時・シーンコレクション切り替え時に呼ぶ。
 *
 * @note 呼び出し元: plugin-main.cpp(画面側のイベント・プラグイン解放時)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void image_display_shutdown();
