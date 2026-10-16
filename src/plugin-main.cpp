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
 * @file plugin-main.cpp
 * @brief プラグインの入口。読み込み/解放の処理と、ユーザー操作(ツールメニュー・ホットキー)の受け口。
 *
 * @details
 * 処理の流れ:
 * @code
 *   ツール > Clipboard Image > 画像を表示 ─┐
 *                                          ├→ on_show_requested()
 *   表示ホットキー 押下 ──────────────────┘     (UI スレッド)
 *        ├→ save_clipboard_image()         画像を保存(clipboard-capture.cpp)
 *        ├→ show_image_on_current_scene()  シーンに表示(image-display.cpp)
 *        ├→ cleanup_saved_images()         古い画像を掃除(clipboard-capture.cpp)
 *        └→ auto_hide_restart()            一定時間後に自動で非表示(auto-hide.cpp)
 *
 *   一時停止ホットキー 押下 ──→ auto_hide_toggle_pause()  自動で非表示にするタイマーの一時停止/再開
 *                                (auto-hide.cpp)
 *
 *   ツール > Clipboard Image > 設定... ─→ open_settings_dialog()  設定画面(settings-dialog.cpp)
 * @endcode
 *
 * 関連ファイル:
 * - clipboard-capture.cpp … クリップボードから画像を取り出して保存する
 * - image-display.cpp … 保存した画像を現在のシーンに表示する/消す
 * - auto-hide.cpp … 表示した画像を一定時間後に自動で消すタイマーと、その一時停止
 * - settings.cpp … 設定値の読み込み・保存(起動時に settings_load() で読む)
 * - settings-dialog.cpp … 設定画面
 * - data/locale/ の .ini … メニュー名・ホットキー名の表示文字列
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "auto-hide.hpp"
#include "clipboard-capture.hpp"
#include "image-display.hpp"
#include "settings-dialog.hpp"
#include "settings.hpp"

#include <QAction>
#include <QMenu>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

/**
 * @brief ホットキーの内部名。
 * @warning OBS の設定ファイルに保存されるため、変更すると既存の割り当てが消える。
 */
constexpr const char *HOTKEY_NAME = "clipboard_image_overlay.show";

/// @brief シーンコレクションの保存データ内で、このプラグインが使うキー名。
constexpr const char *SAVE_KEY = "clipboard-image-overlay";

/// @brief SAVE_KEY の中で、表示ホットキーの割り当てを保存するキー名。
constexpr const char *SAVE_HOTKEY_KEY = "show_hotkey";

/**
 * @brief 一時停止ホットキーの内部名。
 * @warning OBS の設定ファイルに保存されるため、変更すると既存の割り当てが消える。
 */
constexpr const char *PAUSE_HOTKEY_NAME = "clipboard_image_overlay.toggle_pause";

/// @brief SAVE_KEY の中で、一時停止ホットキーの割り当てを保存するキー名。
constexpr const char *SAVE_PAUSE_HOTKEY_KEY = "pause_hotkey";

/// @brief 登録した表示ホットキーの ID。登録前・解除後は OBS_INVALID_HOTKEY_ID。
obs_hotkey_id show_hotkey_id = OBS_INVALID_HOTKEY_ID;

/// @brief 登録した一時停止ホットキーの ID。登録前・解除後は OBS_INVALID_HOTKEY_ID。
obs_hotkey_id pause_hotkey_id = OBS_INVALID_HOTKEY_ID;

/**
 * @brief シーンコレクションの読み込みが終わり、通知を登録してよい状態なら true。
 *
 * @details
 * シーン一覧の変更(SCENE_LIST_CHANGED)は、読み込みの途中・切り替えの途中・終了の途中にも届く。
 * その間に通知を登録すると、片付けた後にまた通知を受け取り、タイマーが動き出してしまうことがある。
 * 起動完了時・切り替え完了時に true、切り替え開始時・終了時に false にする。
 */
bool scenes_ready = false;

/**
 * @brief 「クリップボードの画像を表示」の共通処理。
 *
 * @details クリップボードの画像を保存し、現在のシーンに表示して、自動で消すタイマーを開始する。
 * 表示中にもう一度呼ばれると、画像が差し替わり、タイマーも最初からやり直す(表示時間の延長)。
 * 画像が取れなければ、理由をログに残して何もしない。
 *
 * @param trigger どこから呼ばれたか("menu" / "hotkey")。ログ用。
 *
 * @note スレッド: 必ず UI スレッドで呼ぶこと(Qt のクリップボードは UI スレッドからしか触れないため)。
 * @see add_tools_submenu(), on_hotkey_pressed()(呼び出し元)
 */
void on_show_requested(const char *trigger)
{
	obs_log(LOG_INFO, "show requested (trigger: %s)", trigger);

	/* クリップボードの画像をファイルに保存する。画像がなければ終了 */
	const auto image = save_clipboard_image();
	if (!image)
		return;

	/*
	 * 何も表示されていないのに一時停止が残っていたら、貼る前に解除する。
	 * 通常は非表示の通知で解除されるが、ソースの名前を変えた場合など通知では分からない消え方もあるため、
	 * ここで必ず確かめる(残っていると、貼った画像が一時停止のまま消えなくなる)。
	 */
	if (!is_clipboard_image_showing())
		auto_hide_on_image_hidden();

	/* 保存した画像を現在のシーンに表示する */
	const bool shown = show_image_on_current_scene(*image);

	/*
	 * 保存するたびに古い画像を掃除する(#9)。長い配信で何枚も貼っても、保存フォルダがたまり続けないように。
	 * 表示を始めた後に行うのは、今表示する画像と差し替え待ちの画像を「使用中」として確実に残すため
	 */
	cleanup_saved_images(clipboard_images_in_use());

	/* 表示できなければタイマーも動かさない */
	if (!shown)
		return;

	/* 一定時間後に自動で消す(既に動いていれば延長) */
	auto_hide_restart();
}

/**
 * @brief 「ツール」メニューに「Clipboard Image」のサブメニューを追加する。
 *
 * @details
 * 1 つのプラグインがツールメニューに項目をいくつも並べると、他のプラグインの項目と混ざって探しにくい。
 * そこで項目は 1 つだけにして、その中に「画像を表示」「設定...」を並べる。
 * - 画像を表示 → on_show_requested()(ホットキーと同じ処理)
 * - 設定...    → open_settings_dialog()(settings-dialog.cpp)
 *
 * サブメニューの親は OBS のメイン画面にする(OBS の終了時に一緒に解放されるように)。
 *
 * @note 呼び出し元: obs_module_load()。
 * @note スレッド: UI スレッド(メニューの操作も UI スレッドで届く)。
 */
void add_tools_submenu()
{
	auto *root = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Menu.Root")));
	if (!root) {
		obs_log(LOG_WARNING, "failed to add Tools menu item");
		return;
	}

	auto *menu = new QMenu(static_cast<QWidget *>(obs_frontend_get_main_window()));
	QAction *show = menu->addAction(QString::fromUtf8(obs_module_text("Menu.ShowClipboardImage")));
	QObject::connect(show, &QAction::triggered, []() { on_show_requested("menu"); });
	QAction *settings = menu->addAction(QString::fromUtf8(obs_module_text("Menu.Settings")));
	QObject::connect(settings, &QAction::triggered, []() { open_settings_dialog(); });
	root->setMenu(menu);
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
 * @brief 一時停止ホットキーが押された/離されたときの処理。自動で消すタイマーを一時停止/再開する。
 *
 * @param pressed 押したとき true、離したとき false。
 *
 * @note 呼び出し元: OBS(obs_module_load() で登録したホットキー)。
 * @note スレッド: ホットキー専用スレッド(UI スレッドではない)。Qt のタイマーを触るため UI スレッドに処理を依頼する。
 */
void on_pause_hotkey_pressed(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;

	obs_queue_task(OBS_TASK_UI, [](void *) { auto_hide_toggle_pause(); }, nullptr, false);
}

/**
 * @brief 1 つのホットキーの割り当てを、保存データに書き込む。
 *
 * @param obj 書き込み先(このプラグイン用の保存データ)。
 * @param key 保存に使うキー名。
 * @param id  ホットキーの ID。
 */
void save_hotkey(obs_data_t *obj, const char *key, obs_hotkey_id id)
{
	obs_data_array_t *hotkey = obs_hotkey_save(id);
	obs_data_set_array(obj, key, hotkey);
	obs_data_array_release(hotkey);
}

/**
 * @brief 1 つのホットキーの割り当てを、保存データから読み込む。
 *
 * @param obj 読み込み元(このプラグイン用の保存データ)。
 * @param key 保存に使ったキー名。
 * @param id  ホットキーの ID。
 */
void load_hotkey(obs_data_t *obj, const char *key, obs_hotkey_id id)
{
	obs_data_array_t *hotkey = obs_data_get_array(obj, key);
	obs_hotkey_load(id, hotkey);
	obs_data_array_release(hotkey);
}

/**
 * @brief ホットキーの割り当てを保存/読み込みする。
 *
 * @details
 * プラグインが追加したホットキー(フロントエンド用)は、OBS が自動では保存しない。
 * そのため他のフロントエンドプラグインと同じく、シーンコレクションのデータに自分で書き込む。
 * 結果として、割り当てはシーンコレクションごとになる。
 *
 * 保存形式: { "clipboard-image-overlay": { "show_hotkey": [...], "pause_hotkey": [...] } }
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
		save_hotkey(obj, SAVE_HOTKEY_KEY, show_hotkey_id);
		save_hotkey(obj, SAVE_PAUSE_HOTKEY_KEY, pause_hotkey_id);
		obs_data_set_obj(save_data, SAVE_KEY, obj);
		obs_data_release(obj);
		return;
	}

	/* 読み込み: 保存データがなければ(初回など)割り当てなしのまま */
	obs_data_t *obj = obs_data_get_obj(save_data, SAVE_KEY);
	if (!obj)
		return;

	load_hotkey(obj, SAVE_HOTKEY_KEY, show_hotkey_id);
	load_hotkey(obj, SAVE_PAUSE_HOTKEY_KEY, pause_hotkey_id);
	obs_data_release(obj);
	obs_log(LOG_INFO, "hotkey bindings loaded");
}

/**
 * @brief OBS の画面側(フロントエンド)のイベントを受け取る。
 *
 * @details
 * - 起動完了時(FINISHED_LOADING)・シーンコレクション切り替え時(SCENE_COLLECTION_CHANGED):
 *   タイマー・一時停止・通知の登録を片付けてから、Clipboard Image をフェードなしで即座に非表示にする。
 *   OBS は終了処理の最初にシーンコレクションを保存し、EXIT イベントはその後に来る。
 *   そのため表示中に終了すると「表示中」のまま保存され、次の起動時に出たままになる。
 *   保存より前に届くイベントはないので、読み込んだ直後に片付ける。
 *   その後、全シーンの通知(表示/非表示・追加・削除)を登録し、保存フォルダの古い画像を掃除する(#9)。
 * - シーンコレクション切り替えの開始時(SCENE_COLLECTION_CHANGING): タイマーと通知の登録を先に片付ける。
 *   切り替えの途中でタイマーが発火し、片付け中のシーンを触るのを防ぐため。
 * - シーン一覧の変更時(SCENE_LIST_CHANGED): 追加されたシーンの通知も登録する。
 *   読み込み・切り替え・終了の途中は登録しない(scenes_ready)。
 * - 終了時(EXIT): 設定画面が開いていれば閉じ、自動で消すタイマーと、画像差し替えのタイマーを止める。
 *   終了処理の途中でタイマーが発火し、片付け中のシーンやソースを触るのを防ぐため。
 *
 * @param event 発生したイベント。
 *
 * @note 呼び出し元: OBS(obs_module_load() で登録)。
 * @note スレッド: UI スレッド。
 */
void on_frontend_event(obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		/* 切り替え前のタイマー・一時停止・通知の登録が、新しいコレクションに持ち越されないように */
		auto_hide_shutdown();
		image_display_shutdown();
		hide_clipboard_image(false);
		/* 片付けた後で通知を登録する(起動直後に手動で表示・追加した場合もカウントできるように) */
		image_display_watch_scenes();
		scenes_ready = true;
		/* 前回までにたまった古い画像を掃除する(#9)。読み込んだコレクションで使用中の画像は残す */
		cleanup_saved_images(clipboard_images_in_use());
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		/* 切り替えの途中でタイマーが発火し、片付け中のシーンを触らないように先に止める */
		scenes_ready = false;
		auto_hide_shutdown();
		image_display_shutdown();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		/* シーンが追加されたら、そのシーンの通知も受け取る(読み込み・切り替え・終了の途中は登録しない) */
		if (scenes_ready)
			image_display_watch_scenes();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		scenes_ready = false;
		close_settings_dialog();
		auto_hide_shutdown();
		image_display_shutdown();
		break;
	default:
		break;
	}
}

} // namespace

/**
 * @brief プラグインの読み込み時の処理。OBS の起動時に 1 回呼ばれる。
 *
 * @details ツールメニューの項目追加、ホットキー登録、保存処理と画面側イベント(起動完了・終了など)の登録を行う。
 *
 * @return true で読み込み成功。false を返すと OBS はこのプラグインを使わない。
 */
bool obs_module_load(void)
{
	/* 表示時間などの設定を、表示処理より先に読み込んでおく */
	settings_load();

	add_tools_submenu();

	show_hotkey_id = obs_hotkey_register_frontend(HOTKEY_NAME, obs_module_text("Hotkey.ShowClipboardImage"),
						      on_hotkey_pressed, nullptr);
	/* ホットキーが登録できなくてもメニューからは使えるので、警告だけ出して続ける */
	if (show_hotkey_id == OBS_INVALID_HOTKEY_ID)
		obs_log(LOG_WARNING, "failed to register show hotkey");

	pause_hotkey_id = obs_hotkey_register_frontend(PAUSE_HOTKEY_NAME, obs_module_text("Hotkey.TogglePause"),
						       on_pause_hotkey_pressed, nullptr);
	if (pause_hotkey_id == OBS_INVALID_HOTKEY_ID)
		obs_log(LOG_WARNING, "failed to register pause hotkey");

	obs_frontend_add_save_callback(on_frontend_save, nullptr);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_DISPLAY_VERSION);
	return true;
}

/**
 * @brief プラグインの解放時の処理。OBS の終了時に 1 回呼ばれる。
 *
 * @details obs_module_load() で登録したものを逆順に外す。
 */
void obs_module_unload(void)
{
	/* 終了イベントで止まっているはずだが、念のためここでも止める(何度呼んでもよい) */
	close_settings_dialog();
	auto_hide_shutdown();
	image_display_shutdown();

	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	obs_frontend_remove_save_callback(on_frontend_save, nullptr);

	if (show_hotkey_id != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(show_hotkey_id);
		show_hotkey_id = OBS_INVALID_HOTKEY_ID;
	}
	if (pause_hotkey_id != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(pause_hotkey_id);
		pause_hotkey_id = OBS_INVALID_HOTKEY_ID;
	}

	obs_log(LOG_INFO, "plugin unloaded");
}
