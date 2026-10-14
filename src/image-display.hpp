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
 * 2. 現在のシーンにそのソースがなければ追加し、画面中央に置く(境界は OBS の既定と同じ「自動」)
 * 3. 画像の大きさに合わせて表示サイズ(「変換の編集」の「大きさ」)を決める
 *    - 画面の幅・高さの 60% を超える画像は、収まるよう縮小する(縦横比は保つ)
 *    - 小さい画像は等倍(拡大しない)
 *    - 画像や画面の大きさが取れない場合は等倍(元の大きさのまま)
 * 4. ソースをシーンの最前面に移し、表示状態にする
 *
 * 位置を設定するのは、シーンに初めて追加したときだけ。
 * 配信者がその後 OBS 上で動かした位置は、上書きしない。
 * 表示サイズは画像ごとに決め直す(ドラッグで変えた大きさは、次の画像には引き継がない)。
 *
 * @param image 表示する画像の情報(パスと幅・高さ)。
 * @return 表示できたら true。現在のシーンが取れない・ソースが作れない場合は false(理由はログに出る)。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
bool show_image_on_current_scene(const SavedImage &image);
