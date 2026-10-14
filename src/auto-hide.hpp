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
 * @file auto-hide.hpp
 * @brief 表示した画像を、一定時間後に自動で消すタイマーの公開窓口。
 *
 * @details 実装は auto-hide.cpp。呼び出し元は plugin-main.cpp。
 */

#pragma once

/**
 * @brief 自動で消すタイマーを開始する。動いている途中なら、残り時間を最初からやり直す(延長)。
 *
 * @details
 * 時間が来ると hide_clipboard_image()(image-display.cpp)を呼び、フェードアウトで画像を消す。
 * 表示中にもう一度ホットキーを押すと、この関数がまた呼ばれて表示時間が延びる。
 *
 * 表示時間は AUTO_HIDE_SECONDS(auto-hide.cpp)。MVP では固定で、#8 の設定画面で変更できるようにする。
 *
 * @note スレッド: UI スレッドから呼ぶこと(Qt のタイマーを使うため)。
 */
void auto_hide_restart();

/**
 * @brief タイマーを止めて片付ける。何度呼んでもよい。
 *
 * @details OBS の終了時に呼ぶ。終了処理の途中でタイマーが動き、
 *          片付け済みのシーンやソースを触ってしまうのを防ぐため。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void auto_hide_shutdown();
