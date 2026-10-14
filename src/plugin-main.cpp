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
 * @file plugin-main.cpp
 * @brief プラグインの入口。読み込み/解放の処理と、ユーザー操作(ツールメニュー・ホットキー)の受け口。
 *
 * @details
 * 処理の流れ:
 * @code
 *   ツールメニュー クリック ─┐
 *                              ├→ on_show_requested() ─→ save_clipboard_image()         画像を保存
 *   ホットキー 押下 ──────────┘     (UI スレッド)     │   (clipboard-capture.cpp)
 *                                                      └→ show_image_on_current_scene()  シーンに表示
 *                                                          (image-display.cpp)
 * @endcode
 *
 * 関連ファイル:
 * - clipboard-capture.cpp … クリップボードから画像を取り出して保存する
 * - image-display.cpp … 保存した画像を現在のシーンに表示する
 * - data/locale/ の .ini … メニュー名・ホットキー名の表示文字列
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "clipboard-capture.hpp"
#include "image-display.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

/**
 * @brief ホットキーの内部名。
 * @warning OBS の設定ファイルに保存されるため、変更すると既存の割り当てが消える。
 */
constexpr const char *HOTKEY_NAME = "obs_clipboard_image.show";

/// @brief シーンコレクションの保存データ内で、このプラグインが使うキー名。
constexpr const char *SAVE_KEY = "obs-clipboard-image";

/// @brief SAVE_KEY の中で、ホットキーの割り当てを保存するキー名。
constexpr const char *SAVE_HOTKEY_KEY = "show_hotkey";

/// @brief 登録したホットキーの ID。登録前・解除後は OBS_INVALID_HOTKEY_ID。
obs_hotkey_id show_hotkey_id = OBS_INVALID_HOTKEY_ID;

/**
 * @brief 「クリップボードの画像を表示」の共通処理。
 *
 * @details クリップボードの画像を保存し、現在のシーンに表示する。
 * 画像が取れなければ、理由をログに残して何もしない。
 *
 * @param trigger どこから呼ばれたか("menu" / "hotkey")。ログ用。
 *
 * @note スレッド: 必ず UI スレッドで呼ぶこと(Qt のクリップボードは UI スレッドからしか触れないため)。
 * @see on_tools_menu_clicked(), on_hotkey_pressed()(呼び出し元)
 */
void on_show_requested(const char *trigger)
{
	obs_log(LOG_INFO, "show requested (trigger: %s)", trigger);

	/* クリップボードの画像をファイルに保存する。画像がなければ終了 */
	const auto image = save_clipboard_image();
	if (!image)
		return;

	/* 保存した画像を現在のシーンに表示する */
	show_image_on_current_scene(*image);
}

/**
 * @brief ツールメニュー「クリップボードの画像を表示」がクリックされたときの処理。
 *
 * @note 呼び出し元: OBS(obs_module_load() で登録したメニュー項目)。
 * @note スレッド: UI スレッド(メニュー操作は UI スレッドで届く)。
 */
void on_tools_menu_clicked(void *)
{
	on_show_requested("menu");
}

/**
 * @brief ホットキーが押された/離されたときの処理。
 *
 * @param pressed 押したとき true、離したとき false。
 *
 * @note 呼び出し元: OBS(obs_module_load() で登録したホットキー)。
 * @note スレッド: ホットキー専用スレッド(UI スレッドではない)。そのため UI スレッドに処理を依頼する。
 */
void on_hotkey_pressed(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	/* キーを離したときにも呼ばれるので、押したときだけ処理する */
	if (!pressed)
		return;

	/* ここはホットキー専用スレッドなので、UI スレッドに処理を依頼してから共通処理を呼ぶ */
	obs_queue_task(OBS_TASK_UI, [](void *) { on_show_requested("hotkey"); }, nullptr, false);
}

/**
 * @brief ホットキーの割り当てを保存/読み込みする。
 *
 * @details
 * プラグインが追加したホットキー(フロントエンド用)は、OBS が自動では保存しない。
 * そのため他のフロントエンドプラグインと同じく、シーンコレクションのデータに自分で書き込む。
 * 結果として、割り当てはシーンコレクションごとになる。
 *
 * 保存形式: { "obs-clipboard-image": { "show_hotkey": [...] } }
 *
 * @param save_data シーンコレクションの保存データ。
 * @param saving    保存時 true、読み込み時 false。
 *
 * @note 呼び出し元: OBS(シーンコレクションの保存時・読み込み時)。
 */
void on_frontend_save(obs_data_t *save_data, bool saving, void *)
{
	if (saving) {
		/* 保存: 現在の割り当てを書き込む */
		obs_data_t *obj = obs_data_create();
		obs_data_array_t *hotkey = obs_hotkey_save(show_hotkey_id);
		obs_data_set_array(obj, SAVE_HOTKEY_KEY, hotkey);
		obs_data_set_obj(save_data, SAVE_KEY, obj);
		obs_data_array_release(hotkey);
		obs_data_release(obj);
		return;
	}

	/* 読み込み: 保存データがなければ(初回など)割り当てなしのまま */
	obs_data_t *obj = obs_data_get_obj(save_data, SAVE_KEY);
	if (!obj)
		return;

	obs_data_array_t *hotkey = obs_data_get_array(obj, SAVE_HOTKEY_KEY);
	obs_hotkey_load(show_hotkey_id, hotkey);
	obs_data_array_release(hotkey);
	obs_data_release(obj);
	obs_log(LOG_INFO, "hotkey binding loaded");
}

} // namespace

/**
 * @brief プラグインの読み込み時の処理。OBS の起動時に 1 回呼ばれる。
 *
 * @details ツールメニューの項目追加、ホットキー登録、保存処理の登録を行う。
 *
 * @return true で読み込み成功。false を返すと OBS はこのプラグインを使わない。
 */
bool obs_module_load(void)
{
	obs_frontend_add_tools_menu_item(obs_module_text("Menu.ShowClipboardImage"), on_tools_menu_clicked, nullptr);

	show_hotkey_id = obs_hotkey_register_frontend(HOTKEY_NAME, obs_module_text("Hotkey.ShowClipboardImage"),
						      on_hotkey_pressed, nullptr);
	/* ホットキーが登録できなくてもメニューからは使えるので、警告だけ出して続ける */
	if (show_hotkey_id == OBS_INVALID_HOTKEY_ID)
		obs_log(LOG_WARNING, "failed to register hotkey");

	obs_frontend_add_save_callback(on_frontend_save, nullptr);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

/**
 * @brief プラグインの解放時の処理。OBS の終了時に 1 回呼ばれる。
 *
 * @details obs_module_load() で登録したものを逆順に外す。
 */
void obs_module_unload(void)
{
	obs_frontend_remove_save_callback(on_frontend_save, nullptr);

	if (show_hotkey_id != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(show_hotkey_id);
		show_hotkey_id = OBS_INVALID_HOTKEY_ID;
	}

	obs_log(LOG_INFO, "plugin unloaded");
}
