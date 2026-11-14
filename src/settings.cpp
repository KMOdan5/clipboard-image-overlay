// SPDX-FileCopyrightText: 2026 KMOdan5
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file settings.cpp
 * @brief プラグインの設定値の読み込みと保存。
 *
 * @details
 * 設定は OBS のデータ形式(obs_data)で JSON ファイルに保存する。
 * 保存は obs_data_save_json_safe() で一時ファイルに書いてから置き換えるので、
 * 書き込み中に OBS が落ちても設定ファイルが壊れにくい(前の内容は .bak に残る)。
 *
 * 公開している関数の説明は settings.hpp を参照。
 */

#include "settings.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

/// @brief 設定ファイルの名前(プラグインの設定フォルダの中)。
constexpr const char *SETTINGS_FILE = "settings.json";

/**
 * @brief 設定ファイルのキー名。
 * @warning 変更すると、保存済みの設定が読めなくなる(既定値に戻る)。
 */
constexpr const char *KEY_DISPLAY_SECONDS = "display_seconds";
/// @brief 設定ファイルのキー名(最大サイズの割合)。
constexpr const char *KEY_MAX_SIZE_PERCENT = "max_size_percent";
/// @brief 設定ファイルのキー名(常に最前面に表示する)。
constexpr const char *KEY_ALWAYS_ON_TOP = "always_on_top";
/// @brief 設定ファイルのキー名(スタジオモードでもすぐに配信画面に表示する)。
constexpr const char *KEY_STUDIO_MODE_SHOW_ON_PROGRAM = "studio_mode_show_on_program";
/// @brief 設定ファイルのキー名(保存する枚数)。
constexpr const char *KEY_MAX_SAVED_IMAGES = "max_saved_images";

/// @brief 今の設定値。settings_load() で読み込み、settings_save() で変更する。
PluginSettings current;

/**
 * @brief 範囲外の値を範囲内に収める。
 * @param settings 対象の設定値。
 * @return 範囲内に収めた設定値。
 */
PluginSettings clamp_settings(PluginSettings settings)
{
	settings.display_seconds = std::clamp(settings.display_seconds, DISPLAY_SECONDS_MIN, DISPLAY_SECONDS_MAX);
	settings.max_size_percent = std::clamp(settings.max_size_percent, MAX_SIZE_PERCENT_MIN, MAX_SIZE_PERCENT_MAX);
	settings.max_saved_images = std::clamp(settings.max_saved_images, MAX_SAVED_IMAGES_MIN, MAX_SAVED_IMAGES_MAX);
	return settings;
}

/**
 * @brief 設定ファイルのパスを返す。設定フォルダがなければ作る。
 * @return 設定ファイルのパス(UTF-8)。取れなければ空文字列。
 */
std::string settings_path()
{
	char *folder = obs_module_config_path("");
	if (folder) {
		os_mkdirs(folder);
		bfree(folder);
	}

	/* obs_module_config_path() が確保した文字列は、呼び出し側で解放する決まり */
	char *path = obs_module_config_path(SETTINGS_FILE);
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

} // namespace

/**
 * @brief 今の設定値を返す。
 * @details 仕様の詳細は settings.hpp の宣言側を参照。
 */
const PluginSettings &settings_get()
{
	return current;
}

/**
 * @brief 設定ファイルを読み込む。
 * @details 仕様の詳細は settings.hpp の宣言側を参照。
 */
void settings_load()
{
	const PluginSettings defaults;
	const std::string path = settings_path();

	obs_data_t *data = path.empty() ? nullptr : obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data) {
		/* 初回起動など、ファイルがないときは既定値のまま */
		current = defaults;
		obs_log(LOG_INFO, "settings: no settings file; using defaults");
		return;
	}

	/* キーがない(古いバージョンで保存した)項目は既定値になるよう、既定値を登録してから読む */
	obs_data_set_default_int(data, KEY_DISPLAY_SECONDS, defaults.display_seconds);
	obs_data_set_default_int(data, KEY_MAX_SIZE_PERCENT, defaults.max_size_percent);
	obs_data_set_default_bool(data, KEY_ALWAYS_ON_TOP, defaults.always_on_top);
	obs_data_set_default_bool(data, KEY_STUDIO_MODE_SHOW_ON_PROGRAM, defaults.studio_mode_show_on_program);
	obs_data_set_default_int(data, KEY_MAX_SAVED_IMAGES, defaults.max_saved_images);

	PluginSettings loaded;
	loaded.display_seconds = static_cast<int>(obs_data_get_int(data, KEY_DISPLAY_SECONDS));
	loaded.max_size_percent = static_cast<int>(obs_data_get_int(data, KEY_MAX_SIZE_PERCENT));
	loaded.always_on_top = obs_data_get_bool(data, KEY_ALWAYS_ON_TOP);
	loaded.studio_mode_show_on_program = obs_data_get_bool(data, KEY_STUDIO_MODE_SHOW_ON_PROGRAM);
	loaded.max_saved_images = static_cast<int>(obs_data_get_int(data, KEY_MAX_SAVED_IMAGES));
	obs_data_release(data);

	current = clamp_settings(loaded);
	obs_log(LOG_INFO,
		"settings: loaded (display %d s, max size %d%%, always on top %s, studio mode on program %s,"
		" keep %d images)",
		current.display_seconds, current.max_size_percent, current.always_on_top ? "on" : "off",
		current.studio_mode_show_on_program ? "on" : "off", current.max_saved_images);
}

/**
 * @brief 設定値を変更し、設定ファイルに保存する。
 * @details 仕様の詳細は settings.hpp の宣言側を参照。
 */
bool settings_save(const PluginSettings &settings)
{
	current = clamp_settings(settings);

	const std::string path = settings_path();
	if (path.empty()) {
		obs_log(LOG_WARNING, "settings: no config path; settings are not saved");
		return false;
	}

	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, KEY_DISPLAY_SECONDS, current.display_seconds);
	obs_data_set_int(data, KEY_MAX_SIZE_PERCENT, current.max_size_percent);
	obs_data_set_bool(data, KEY_ALWAYS_ON_TOP, current.always_on_top);
	obs_data_set_bool(data, KEY_STUDIO_MODE_SHOW_ON_PROGRAM, current.studio_mode_show_on_program);
	obs_data_set_int(data, KEY_MAX_SAVED_IMAGES, current.max_saved_images);
	const bool saved = obs_data_save_json_safe(data, path.c_str(), "tmp", "bak");
	obs_data_release(data);

	if (saved) {
		obs_log(LOG_INFO,
			"settings: saved (display %d s, max size %d%%, always on top %s, studio mode on program %s,"
			" keep %d images)",
			current.display_seconds, current.max_size_percent, current.always_on_top ? "on" : "off",
			current.studio_mode_show_on_program ? "on" : "off", current.max_saved_images);
	} else {
		obs_log(LOG_WARNING, "settings: failed to save %s", path.c_str());
	}
	return saved;
}

/**
 * @brief 「最大サイズの割合」から、キャンバスの幅と高さにかける割合(√割合)を出す。
 *
 * @details 仕様の詳細は settings.hpp の宣言側を参照。
 */
double max_size_side_ratio(int percent)
{
	return std::sqrt(static_cast<double>(percent) / 100.0);
}
