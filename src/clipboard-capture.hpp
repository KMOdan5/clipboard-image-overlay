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
 * @file clipboard-capture.hpp
 * @brief クリップボードの画像を取り出して保存する機能の公開窓口。
 *
 * @details 実装は clipboard-capture.cpp。呼び出し元は plugin-main.cpp
 *          (on_show_requested()・起動完了時の掃除・表示履歴の画像の表示・「前の画像を表示」ホットキー)と、
 *          history-dock.cpp(表示履歴の一覧)。
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
 * 保存先(Windows): %APPDATA%/obs-studio/plugin_config/clipboard-image-overlay/images/
 *
 * @return 保存した画像の情報(パスと幅・高さ)。表示するときの大きさの計算に使う。
 * @retval std::nullopt 画像がない、または保存に失敗した。理由は OBS のログに出る。
 *
 * @note スレッド: 必ず UI スレッドから呼ぶこと(Qt のクリップボードの制約)。
 */
std::optional<SavedImage> save_clipboard_image();

/**
 * @brief 保存フォルダの古い画像を削除し、新しい順に 50 枚だけ残す。
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
 * @note 呼び出し元: plugin-main.cpp(OBS の起動完了時と、画像を表示したあと)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void cleanup_saved_images(const std::vector<std::string> &in_use);

/**
 * @brief 画像の保存フォルダのパスを返す。
 *
 * @details 表示履歴のドックが、フォルダの変化(新しい画像・掃除で消えた画像)を見張るために使う。
 *          フォルダは作らない(まだない場合もある)。
 *
 * @return 保存フォルダのパス(UTF-8、OS 標準の区切り文字)。取得できなければ空。
 *
 * @note 呼び出し元: history-dock.cpp。
 */
std::string saved_images_folder();

/**
 * @brief 保存した画像のパスを、新しい順に返す。
 *
 * @details
 * 保存フォルダの画像を、そのまま表示履歴として使う。
 * - 対象は cleanup_saved_images() と同じく、このプラグインが付けた名前の形のファイルだけ
 * - 新しい・古いはファイル名の日時で判断する
 * - 返すのは最大 50 枚(保存の上限と同じ)。掃除の前に呼ばれても、それより多くは返さない
 *
 * @return 画像ファイルのパス(UTF-8、OS 標準の区切り文字)。先頭が一番新しい。フォルダがなければ空。
 *
 * @note 呼び出し元: history-dock.cpp(一覧)、plugin-main.cpp(「前の画像を表示」ホットキー)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
std::vector<std::string> list_saved_images();

/**
 * @brief 保存した画像の情報(パスと幅・高さ)を読む。
 *
 * @details 表示履歴から表示するとき、show_image_on_current_scene() に渡す大きさを知るために使う。
 *          画像の中身は読まず、大きさだけを読む(回転情報は反映する)。
 *
 * @param path 画像ファイルのパス(UTF-8)。list_saved_images() が返したもの。
 * @return 画像の情報。
 * @retval std::nullopt ファイルがない(掃除などで消えた)・画像として読めない。理由は OBS のログに出る。
 *
 * @note 呼び出し元: plugin-main.cpp の show_history_image()。
 */
std::optional<SavedImage> read_saved_image(const std::string &path);
