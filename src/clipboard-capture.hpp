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
 * @file clipboard-capture.hpp
 * @brief クリップボードの画像を取り出して保存する機能の公開窓口。
 *
 * @details 実装は clipboard-capture.cpp。呼び出し元は plugin-main.cpp の on_show_requested()。
 */

#pragma once

#include <optional>
#include <string>

/**
 * @brief 保存した画像の情報。save_clipboard_image() の結果として返す。
 */
struct SavedImage {
	/// @brief 保存したファイルのパス(UTF-8、OS 標準の区切り文字)。
	std::string path;
	/// @brief 画像の幅(ピクセル)。回転情報を反映した後の値。
	int width = 0;
	/// @brief 画像の高さ(ピクセル)。回転情報を反映した後の値。
	int height = 0;
};

/**
 * @brief クリップボードの画像を、プラグインの画像フォルダに保存する。
 *
 * @details
 * 対応する入力(優先順):
 * 1. コピーした画像ファイル(エクスプローラーで Ctrl+C)
 *    … 原則は元ファイルをそのままコピー。OBS で表示できない場合だけ PNG に変換。
 * 2. 画像データ(ブラウザの「画像をコピー」、Win+Shift+S)… PNG で保存。
 *
 * 保存先(Windows): %APPDATA%/obs-studio/plugin_config/obs-clipboard-image/images/
 *
 * @return 保存した画像の情報(パスと幅・高さ)。表示するときの大きさの計算に使う。
 * @retval std::nullopt 画像がない、または保存に失敗した。理由は OBS のログに出る。
 *
 * @note スレッド: 必ず UI スレッドから呼ぶこと(Qt のクリップボードの制約)。
 */
std::optional<SavedImage> save_clipboard_image();
