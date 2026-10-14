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

#include <string>

/**
 * @brief 画像ファイルを、現在のシーン(配信に出ているシーン)に表示する。
 *
 * @details
 * プラグイン専用の画像ソース「Clipboard Image」を 1 つだけ使い回す。
 * 1. 専用ソースがなければ作り、あれば表示する画像ファイルだけ差し替える
 * 2. 現在のシーンにそのソースがなければ追加し、既定の位置・大きさに置く
 * 3. ソースをシーンの最前面に移し、表示状態にする
 *
 * 既定の配置: 画面中央。大きさは画面の幅・高さの 60% に収まるよう縮小する
 * (縦横比は保つ。元が小さい画像は拡大しない)。
 *
 * 配置を設定するのは、シーンに初めて追加したときだけ。
 * 配信者がその後 OBS 上で位置や大きさを変えた場合は、その配置を尊重して上書きしない。
 *
 * @param path 表示する画像ファイルのパス(UTF-8)。
 * @return 表示できたら true。現在のシーンが取れない・ソースが作れない場合は false(理由はログに出る)。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
bool show_image_on_current_scene(const std::string &path);
