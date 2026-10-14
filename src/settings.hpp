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
 * @file settings.hpp
 * @brief プラグインの設定値(表示時間・最大サイズなど)の公開窓口。
 *
 * @details
 * 実装は settings.cpp。設定画面(settings-dialog.cpp)で変更し、
 * 自動で消す処理(auto-hide.cpp)と表示処理(image-display.cpp)が読む。
 *
 * 保存先: プラグインの設定フォルダの settings.json
 * (Windows: %APPDATA%/obs-studio/plugin_config/obs-clipboard-image/settings.json)。
 * シーンコレクションではなくプラグイン全体の設定なので、どのシーンコレクションでも同じ値を使う。
 *
 * 変更は、次に画像を表示したときから反映する(表示中のタイマーや画像の大きさは変えない)。
 */

#pragma once

/// @brief 表示時間(秒)の下限。0 は「自動で消さない」。
constexpr int DISPLAY_SECONDS_MIN = 0;
/// @brief 表示時間(秒)の上限(10 分)。
constexpr int DISPLAY_SECONDS_MAX = 600;
/// @brief 最大サイズ(画面に対する割合、%)の下限。
constexpr int MAX_SIZE_PERCENT_MIN = 10;
/// @brief 最大サイズ(画面に対する割合、%)の上限。
constexpr int MAX_SIZE_PERCENT_MAX = 100;

/**
 * @brief プラグインの設定値。初期値が既定値。
 */
struct PluginSettings {
	/**
	 * @brief 画像を表示してから自動で消すまでの秒数。
	 * @details 0 なら自動で消さず、手動で消すまで表示し続ける(一時停止ホットキーも何もしない)。
	 */
	int display_seconds = 7;

	/**
	 * @brief 大きい画像を画面に収めるかどうか(表示サイズの自動調整)。
	 * @details Clipboard Image の境界が「自動」のときだけ働く。オフなら表示サイズを一切変えない。
	 */
	bool fit_large_images = true;

	/// @brief 画像の最大の大きさ(画面の幅・高さに対する割合、%)。fit_large_images がオンのときだけ使う。
	int max_size_percent = 60;

	/**
	 * @brief 貼るたびに Clipboard Image を最前面(一番手前)に移すかどうか。
	 * @details オフなら重なり順を一切変えない(重なり順を自分で管理したい配信者向け)。
	 */
	bool always_on_top = true;
};

/**
 * @brief 今の設定値を返す。
 *
 * @return 設定値。settings_load() の前は既定値。
 *
 * @note スレッド: UI スレッドから呼ぶこと(設定画面と同じスレッドで読み書きするため、ロックはしない)。
 */
const PluginSettings &settings_get();

/**
 * @brief 設定ファイルを読み込む。ファイルがない・壊れている項目は既定値を使う。
 *
 * @details 範囲外の値(ファイルを手で書き換えた場合など)は、範囲内に収めて使う。
 *
 * @note 呼び出し元: plugin-main.cpp の obs_module_load()。
 * @note スレッド: UI スレッド。
 */
void settings_load();

/**
 * @brief 設定値を変更し、設定ファイルに保存する。
 *
 * @details 範囲外の値は範囲内に収める。保存に失敗しても、今回の起動中は変更後の値を使う。
 *
 * @param settings 新しい設定値。
 * @return 保存できたら true。
 * @retval false 保存に失敗した(理由は OBS のログに出る)。
 *
 * @note 呼び出し元: settings-dialog.cpp(「OK」を押したとき)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
bool settings_save(const PluginSettings &settings);
