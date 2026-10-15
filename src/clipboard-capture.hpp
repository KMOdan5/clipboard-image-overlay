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
 * @details 実装は clipboard-capture.cpp。呼び出し元は plugin-main.cpp
 *          (on_show_requested() と、起動完了時の掃除)。
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

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

/**
 * @brief 保存フォルダの古い画像を削除し、新しい順に 50 枚だけ残す(#9)。
 *
 * @details
 * 画像は貼るたびに保存フォルダにたまっていくので、上限を超えた古いものを削除する。
 * - 対象は保存フォルダの中で、このプラグインが付けた名前の形(yyyyMMdd-HHmmss-zzz.拡張子)のファイルだけ。
 *   配信者が自分で置いたファイルなど、それ以外は触らない
 * - 新しい・古いはファイル名の日時で判断する(ファイルの更新日時は、コピー元のものが残ることがあるため)
 * - in_use に含まれるファイル(表示中・差し替え待ちの画像)は、古くても削除しない
 * - 削除した枚数と、削除できなかったファイルはログに出す。削除に失敗しても処理は続ける
 *
 * 上限の 50 枚は固定(設定画面で変えられるようにするかは #23 で検討)。
 *
 * @param in_use 削除してはいけないファイルのパス(UTF-8)。
 *
 * @note 呼び出し元: plugin-main.cpp(OBS の起動完了時と、画像を保存して表示したあと)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void cleanup_saved_images(const std::vector<std::string> &in_use);
