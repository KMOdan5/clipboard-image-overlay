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

/*
 * 【このファイルの役割】
 * プラグインの入口。OBS がプラグインを読み込む/外すときの処理と、
 * ユーザー操作の受け口(ツールメニュー・ホットキー)をまとめている。
 *
 * 処理の流れ:
 *   ツールメニュー クリック ─┐
 *                              ├→ on_show_requested() → save_clipboard_image()
 *   ホットキー 押下 ──────────┘     (UI スレッド)        (clipboard-capture.cpp)
 *
 * 関連ファイル:
 *   - clipboard-capture.cpp … クリップボードから画像を取り出して保存する
 *   - data/locale/ の .ini … メニュー名・ホットキー名の表示文字列
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "clipboard-capture.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

/* ホットキーの内部名。OBS の設定ファイルに保存されるため、変更すると既存の割り当てが消える */
constexpr const char *HOTKEY_NAME = "obs_clipboard_image.show";

/* シーンコレクションの保存データ内で、このプラグインが使うキー名 */
constexpr const char *SAVE_KEY = "obs-clipboard-image";
constexpr const char *SAVE_HOTKEY_KEY = "show_hotkey";

/* 登録したホットキーの ID。登録前・解除後は OBS_INVALID_HOTKEY_ID */
obs_hotkey_id show_hotkey_id = OBS_INVALID_HOTKEY_ID;

/*
 * 「クリップボードの画像を表示」の共通処理。
 *
 * 呼び出し元: on_tools_menu_clicked(), on_hotkey_pressed()
 * スレッド  : 必ず UI スレッド(Qt のクリップボードは UI スレッドからしか触れないため)
 * 引数      : trigger … どこから呼ばれたか("menu" / "hotkey")。ログ用
 * 失敗時    : 画像が取れなければ、理由をログに残して何もしない
 */
void on_show_requested(const char *trigger)
{
	obs_log(LOG_INFO, "show requested (trigger: %s)", trigger);

	/* クリップボードの画像をファイルに保存する。画像がなければ終了 */
	const auto path = save_clipboard_image();
	if (!path)
		return;

	/* 保存した画像をシーンに表示する処理は #5 で実装する */
}

/*
 * ツールメニュー「クリップボードの画像を表示」がクリックされたときの処理。
 *
 * 呼び出し元: OBS(obs_module_load() で登録したメニュー項目)
 * スレッド  : UI スレッド(メニュー操作は UI スレッドで届く)
 */
void on_tools_menu_clicked(void *)
{
	on_show_requested("menu");
}

/*
 * ホットキーが押された/離されたときの処理。
 *
 * 呼び出し元: OBS(obs_module_load() で登録したホットキー)
 * スレッド  : ホットキー専用スレッド。UI スレッドではない
 * 引数      : pressed … 押したとき true、離したとき false
 */
void on_hotkey_pressed(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	/* キーを離したときにも呼ばれるので、押したときだけ処理する */
	if (!pressed)
		return;

	/* ここはホットキー専用スレッドなので、UI スレッドに処理を依頼してから共通処理を呼ぶ */
	obs_queue_task(OBS_TASK_UI, [](void *) { on_show_requested("hotkey"); }, nullptr, false);
}

/*
 * ホットキーの割り当てを保存/読み込みする。
 *
 * 呼び出し元: OBS(シーンコレクションの保存時・読み込み時)
 * 引数      : save_data … シーンコレクションの保存データ
 *             saving    … 保存時 true、読み込み時 false
 *
 * なぜ必要か: プラグインが追加したホットキー(フロントエンド用)は、OBS が自動では
 * 保存しない。そのため他のフロントエンドプラグインと同じく、シーンコレクションの
 * データに自分で書き込む。結果として、割り当てはシーンコレクションごとになる。
 */
void on_frontend_save(obs_data_t *save_data, bool saving, void *)
{
	if (saving) {
		/* 保存: 現在の割り当てを { "obs-clipboard-image": { "show_hotkey": [...] } } の形で書き込む */
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

/*
 * プラグインの読み込み時の処理。OBS の起動時に 1 回呼ばれる。
 *
 * やること: ツールメニューの項目追加、ホットキー登録、保存処理の登録
 * 戻り値  : true で読み込み成功。false を返すと OBS はこのプラグインを使わない
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

/*
 * プラグインの解放時の処理。OBS の終了時に 1 回呼ばれる。
 * obs_module_load() で登録したものを逆順に外す。
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
