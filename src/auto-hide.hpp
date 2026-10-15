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
 * @file auto-hide.hpp
 * @brief 表示した画像を一定時間後に自動で消すタイマーと、その一時停止の公開窓口。
 *
 * @details 実装は auto-hide.cpp。呼び出し元は plugin-main.cpp と image-display.cpp。
 *
 * 一時停止のルール(#7):
 * 1. 貼ると、残り時間は表示時間(設定画面で変更。既定 10 秒)に戻る。一時停止中かどうかに関係ない
 *    - 手動(目のアイコン・OBS 標準のホットキー)で表示したとき、表示状態でシーンに追加したときも、貼ったときと同じ扱い
 * 2. 一時停止は、画像が表示されている間だけ続く
 *    - 新しい画像を貼って差し替えても、一時停止のまま
 *    - 画像がどのシーンにも表示されなくなった時点で(手動で非表示・削除・自動で消えた)、一時停止は解除される
 * 3. 一時停止ホットキーは表示中だけ効く。押すたびに「一時停止 ↔ 残り時間から再開」を切り替える
 *
 * 「表示中」は、いずれかのシーンで Clipboard Image が表示状態になっていること(is_clipboard_image_showing())。
 */

#pragma once

/**
 * @brief 画像を貼ったとき・手動で表示されたときに呼ぶ。自動で消すまでの残り時間を表示時間に戻す。
 *
 * @details
 * - カウント中・非表示から: 表示時間でカウントを始め直す(表示中に貼ると延長になる)
 * - 一時停止中: 一時停止のまま、残り時間だけ表示時間に戻す(再開したら表示時間で消える)
 * - 表示時間が 0(自動で消さない): タイマーを止め、一時停止も解除する
 *
 * 表示時間は呼ばれるたびに設定(settings_get())から読む。設定画面での変更は、次に表示したときから反映される。
 *
 * 時間が来ると hide_clipboard_image()(image-display.cpp)でフェードアウトさせる。
 *
 * @note 呼び出し元: plugin-main.cpp(貼ったとき)、image-display.cpp(手動で表示されたのを検知したとき)。
 * @note スレッド: UI スレッドから呼ぶこと(Qt のタイマーを使うため)。
 */
void auto_hide_restart();

/**
 * @brief 一時停止ホットキーが押されたときに呼ぶ。一時停止と再開を切り替える。
 *
 * @details
 * - カウント中: 止めて、残り時間を覚えておく
 * - 一時停止中: 覚えておいた残り時間から再開する
 * - 画像が表示されていないとき: 何もしない(一時停止が残っていれば解除する)
 * - タイマーが動いていないとき(表示時間 0 で表示したときなど): 何もしない
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void auto_hide_toggle_pause();

/**
 * @brief 画像がどのシーンにも表示されなくなったときに呼ぶ。一時停止を解除し、タイマーを止める。何度呼んでもよい。
 *
 * @details 手動(目のアイコン・OBS 標準のホットキー・削除)でも、自動で消えたときでも呼ばれる。
 *          一時停止が残ったままだと、次に貼った画像も消えなくなってしまうため。
 *
 * @note 呼び出し元: image-display.cpp(表示されていないのを検知したとき)、
 *       plugin-main.cpp(貼る前に何も表示されていなかったとき)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void auto_hide_on_image_hidden();

/**
 * @brief タイマーを止めて片付け、一時停止も解除する。何度呼んでもよい。
 *
 * @details OBS の終了時・起動完了時・シーンコレクション切り替え時に呼ぶ。
 *          終了処理の途中でタイマーが動き、片付け済みのシーンやソースを触ってしまうのを防ぐため。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void auto_hide_shutdown();
