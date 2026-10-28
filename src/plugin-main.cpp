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
 *   ツール > Clipboard Image > コピーした画像を表示 ─┐
 *                                                    ├→ on_show_requested()
 *   表示ホットキー 押下 ────────────────────────────┘     (UI スレッド)
 *        ├→ save_clipboard_image()         画像を保存(clipboard-capture.cpp)
 *        └→ show_saved_image()             保存した画像を表示する共通処理
 *             ├→ show_image_on_current_scene()  シーンに表示(image-display.cpp)
 *             ├→ cleanup_saved_images()         古い画像を掃除(clipboard-capture.cpp)
 *             └→ auto_hide_restart()            一定時間後に自動で非表示(auto-hide.cpp)
 *
 *   表示履歴のドックのサムネイルをクリック ─→ show_history_image() ─→ show_saved_image()
 *   表示履歴のドックの右クリック > 削除 ─→ delete_history_image()
 *        ├→ clear_clipboard_image()        今の画像なら、即座に非表示にして参照を外す(image-display.cpp)
 *        └→ trash_saved_image()            ごみ箱に送る(clipboard-capture.cpp)
 *   「前の画像を表示」ホットキー ─→ on_show_previous_requested() ─→ show_history_image()
 *
 *   非表示ホットキー 押下 ──→ on_hide_requested() ─→ hide_clipboard_image()  全シーンでフェードアウト(image-display.cpp)
 *
 *   一時停止ホットキー 押下 ──→ on_toggle_pause_requested() ─→ auto_hide_toggle_pause()
 *                                自動で非表示にするタイマーの一時停止/再開(auto-hide.cpp)
 *
 *   シーンの読み込み・切り替え・終了の途中に受け付けた操作は、何もせずに無視する(accept_user_action())
 *
 *   ツール > Clipboard Image > 設定... ─→ open_settings_dialog()  設定画面(settings-dialog.cpp)
 * @endcode
 *
 * 関連ファイル:
 * - clipboard-capture.cpp … クリップボードから画像を取り出して保存する。保存した画像の一覧(履歴)も返す
 * - image-display.cpp … 保存した画像を現在のシーンに表示する/非表示にする。'Clipboard Image' の今の画像を返す
 * - auto-hide.cpp … 表示した画像を一定時間後に自動で消すタイマーと、その一時停止
 * - history-dock.cpp … 表示履歴のドック。保存した画像をサムネイルで並べる
 * - settings.cpp … 設定値の読み込み・保存(起動時に settings_load() で読む)
 * - settings-dialog.cpp … 設定画面
 * - data/locale/ の .ini … メニュー名・ホットキー名・ドックの表示文字列
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "auto-hide.hpp"
#include "clipboard-capture.hpp"
#include "history-dock.hpp"
#include "image-display.hpp"
#include "settings-dialog.hpp"
#include "settings.hpp"

#include <QAction>
#include <QFileInfo>
#include <QMenu>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

/// @brief シーンコレクションの保存データ内で、このプラグインが使うキー名。
constexpr const char *SAVE_KEY = "clipboard-image-overlay";

/**
 * @brief シーンコレクションの読み込みが終わり、シーンを触ってよい状態なら true。
 *
 * @details
 * 起動完了時・切り替え完了時に true、切り替え開始時・終了時に false にする。false の間は次のことをしない。
 * - 通知の登録: シーン一覧の変更(SCENE_LIST_CHANGED)は、読み込み・切り替え・終了の途中にも届く。
 *   その間に通知を登録すると、片付けた後にまた通知を受け取り、タイマーが動き出してしまうことがある
 * - 配信者の操作(ホットキー・メニュー・表示履歴のドック): 片付け中のシーンに画像を出したり、
 *   タイマーを動かしたりしないよう、無視する(accept_user_action())
 * - 表示履歴のドックへの、今の画像の問い合わせへの答え
 */
bool scenes_ready = false;

/**
 * @brief 配信者の操作を受け付けてよいかを返す。受け付けないときは、理由をログに残す。
 *
 * @details
 * シーンの読み込み・切り替え・終了の途中(scenes_ready が false)に受け付けた操作は、何もせずに無視する。
 * 後で実行するために取っておくことはしない。その途中に操作する配信者はまれで、反応がなければもう一度操作すれば済むため。
 * 取っておいて後から実行すると、切り替えた後のシーンに思わぬ画像が出ることもある。
 *
 * @param action 操作の名前。ログ用。
 * @return 受け付けてよいなら true。
 *
 * @note 呼び出し元: on_show_requested()・show_history_image()・on_show_previous_requested()・delete_history_image()・
 *       on_hide_requested()・on_toggle_pause_requested()。
 * @note スレッド: UI スレッドで呼ぶこと。
 */
bool accept_user_action(const char *action)
{
	if (scenes_ready)
		return true;
	obs_log(LOG_INFO, "%s ignored: scenes are loading or shutting down", action);
	return false;
}

/**
 * @brief 保存フォルダにある画像を表示する共通処理。
 *
 * @details
 * 現在のシーンに表示して、自動で消すタイマーを開始する。クリップボードから貼ったときも、
 * 表示履歴から出したときも、ここを通す(フェード・自動で非表示・一時停止が同じように効くように)。
 * 表示中にもう一度呼ばれると、画像が差し替わり、タイマーも最初からやり直す(表示時間の延長)。
 *
 * @param image 表示する画像(保存フォルダにあるもの)。
 *
 * @note スレッド: UI スレッドで呼ぶこと。
 * @see on_show_requested(), show_history_image()(呼び出し元)
 */
void show_saved_image(const SavedImage &image)
{
	/*
	 * 何も表示されていないのに一時停止が残っていたら、貼る前に解除する。
	 * 通常は非表示の通知で解除されるが、ソースの名前を変えた場合など通知では分からない消え方もあるため、
	 * ここで必ず確かめる(残っていると、貼った画像が一時停止のまま消えなくなる)。
	 */
	if (!is_clipboard_image_showing())
		auto_hide_on_image_hidden();

	/* 保存した画像を現在のシーンに表示する */
	const bool shown = show_image_on_current_scene(image);

	/*
	 * 表示するたびに古い画像を掃除する。長い配信で何枚も貼っても、保存フォルダがたまり続けないように。
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
 * @brief 「クリップボードの画像を表示」の処理。
 *
 * @details クリップボードの画像を保存し、show_saved_image() で表示する。
 * 画像が取れなければ、理由をログに残して何もしない。
 * シーンの読み込み・切り替え・終了の途中は、何もしない(accept_user_action())。
 *
 * @param trigger どこから呼ばれたか("menu" / "hotkey")。ログ用。
 *
 * @note スレッド: 必ず UI スレッドで呼ぶこと(Qt のクリップボードは UI スレッドからしか触れないため)。
 * @see add_tools_submenu()、hotkeys(呼び出し元)
 */
void on_show_requested(const char *trigger)
{
	obs_log(LOG_INFO, "show requested (trigger: %s)", trigger);
	if (!accept_user_action("show"))
		return;

	/* クリップボードの画像をファイルに保存する。画像がなければ終了 */
	const auto image = save_clipboard_image();
	if (!image)
		return;

	show_saved_image(*image);
}

/**
 * @brief 表示履歴(保存フォルダ)の画像をもう一度表示する。
 *
 * @details 画像の大きさを読んでから show_saved_image() で表示する。
 * 掃除などでファイルが消えていた場合は、理由をログに残して何もしない。
 * シーンの読み込み・切り替え・終了の途中は、何もしない(accept_user_action())。
 *
 * @param path    画像ファイルのパス(UTF-8)。
 * @param trigger どこから呼ばれたか("history" / "previous hotkey")。ログ用。
 *
 * @note スレッド: UI スレッドで呼ぶこと。
 * @see history_dock_register()(クリックされたとき)、on_show_previous_requested()(呼び出し元)
 */
void show_history_image(const std::string &path, const char *trigger)
{
	obs_log(LOG_INFO, "show history image requested (trigger: %s): %s", trigger, path.c_str());
	if (!accept_user_action("show history image"))
		return;

	const auto image = read_saved_image(path);
	if (!image)
		return;

	show_saved_image(*image);
}

/**
 * @brief 2 つのパスが同じファイルを指すかどうかを返す。
 *
 * @details 'Clipboard Image' に設定されたパスと、保存フォルダの一覧のパスは、区切り文字の向きなどが
 *          違うことがあるため、文字列ではなくファイルとして比べる。
 */
bool same_file(const std::string &a, const std::string &b)
{
	return QFileInfo(QString::fromUtf8(a.c_str())) == QFileInfo(QString::fromUtf8(b.c_str()));
}

/**
 * @brief 「前の画像を表示」ホットキーの処理。
 *
 * @details
 * 今の画像('Clipboard Image' が参照している画像。最後に表示した画像)より 1 つ古い画像を表示する。押すたびにさかのぼる。
 * - 表示中か非表示かに関係なく、今の画像を基準にする。履歴ドックのハイライトと同じ画像なので、
 *   押す前にドックを見れば、何が出るかが分かる
 * - 今の画像が一番古い画像なら、それより前はないので、今の画像をもう一度表示する
 *   - 非表示なら、今の画像を表示する
 *   - 表示中なら、表示はそのままで、自動で非表示にするまでの時間を最初からやり直す
 *     (同じ画像に差し替えると、フェードアウトとフェードインでちらつくため)
 *   - 一番新しい画像には戻らない。気づかずに押し続けたとき、意図しない画像が配信に映らないように
 * - 今の画像が履歴にない(掃除で消えた・配信者が別の画像を設定した・'Clipboard Image' がない)なら、一番新しい画像を表示する
 *
 * 履歴を覚えておく仕組みは持たず、'Clipboard Image' の画像と保存フォルダの一覧だけで決める。
 * シーンの読み込み・切り替え・終了の途中は、何もしない(accept_user_action())。
 *
 * @note スレッド: UI スレッドで呼ぶこと。
 * @see hotkeys(呼び出し元)
 */
void on_show_previous_requested()
{
	if (!accept_user_action("show previous"))
		return;

	const std::vector<std::string> images = list_saved_images();
	if (images.empty()) {
		obs_log(LOG_INFO, "show previous: no saved image");
		return;
	}

	/* 履歴の中で、今の画像がある位置 */
	const std::string current = current_clipboard_image();
	const auto current_pos = std::find_if(images.cbegin(), images.cend(),
					      [&current](const std::string &path) { return same_file(path, current); });
	if (current_pos == images.cend()) {
		show_history_image(images.front(), "previous hotkey");
		return;
	}

	/* 履歴は新しい順なので、1 つ前(古い)の画像は、一覧では 1 つ後ろにある */
	const auto previous_pos = std::next(current_pos);
	if (previous_pos == images.cend()) {
		if (is_clipboard_image_showing()) {
			obs_log(LOG_INFO, "show previous: current image is the oldest, restarting auto-hide");
			auto_hide_restart();
		} else {
			obs_log(LOG_INFO, "show previous: current image is the oldest, showing it again");
			show_history_image(*current_pos, "previous hotkey");
		}
		return;
	}
	show_history_image(*previous_pos, "previous hotkey");
}

/**
 * @brief 表示履歴の画像を削除する(表示履歴のドックの右クリックメニューの「削除」)。
 *
 * @details
 * 配信者が選んで消す操作なので、今の画像でも削除する(古い画像の自動の掃除とは違い、使用中でも残さない)。
 * 1. 今の画像なら、全シーンの 'Clipboard Image' を即座に非表示にし、画像ファイルの参照を外す(clear_clipboard_image())。
 *    自動非表示のタイマーと一時停止も解除する(グループの中に置いた場合は非表示の通知が届かないため、通知を待たずにここで解除する)
 * 2. ファイルを OS のごみ箱に送る(trash_saved_image())。確認の画面は出さない(ごみ箱から元に戻せるため)
 *
 * 1 を先に行うのは、OBS の画像ソースが読み込んだ画像を持っていて、ファイルを先に消しても画面に残ってしまうため。
 * ごみ箱に送れなかった場合も、1 は済んでいる(見せたくない画像が画面に出たままにならないことを優先する)。
 * 今の画像でなければ、2 だけを行う。差し替え待ちの画像は今の画像として扱うので、1 で差し替えも取り消される。
 * シーンの読み込み・切り替え・終了の途中は、何もしない(accept_user_action())。
 *
 * @param path 画像ファイルのパス(UTF-8)。
 *
 * @note スレッド: UI スレッドで呼ぶこと。
 * @see history_dock_register()(呼び出し元)
 */
void delete_history_image(const std::string &path)
{
	obs_log(LOG_INFO, "delete history image requested: %s", path.c_str());
	if (!accept_user_action("delete"))
		return;

	if (same_file(path, current_clipboard_image())) {
		clear_clipboard_image();
		auto_hide_on_image_hidden();
	}
	trash_saved_image(path);
}

/**
 * @brief 「ツール」メニューに「Clipboard Image」のサブメニューを追加する。
 *
 * @details
 * 1 つのプラグインがツールメニューに項目をいくつも並べると、他のプラグインの項目と混ざって探しにくい。
 * そこで項目は 1 つだけにして、その中に「コピーした画像を表示」「設定...」を並べる。
 * - コピーした画像を表示 → on_show_requested()(ホットキーと同じ処理)
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
 * @brief 「'Clipboard Image' を非表示」ホットキーの処理。表示中の画像を、表示時間を待たずに非表示にする。
 *
 * @details
 * 表示時間を 0 秒(手動でのみ非表示)にしたときや、ゲームを全画面で動かしていて目のアイコンを押せないときのため。
 * - 全シーン(グループの中を含む)の 'Clipboard Image' を、非表示トランジション(フェード)で非表示にする。
 *   自動で非表示になるときと同じ動き(hide_clipboard_image())。差し替えの途中なら取り消す
 * - 自動非表示のタイマーと一時停止を解除する(グループの中に置いた場合は非表示の通知が届かないため、通知を待たずにここで解除する)
 * - 何も表示していなければ、何もしない(残っていた一時停止があれば解除する)
 * - 今の画像は変えない(画像ファイルの参照は外さない)。ドックのハイライトと「前の画像を表示」の基準はそのまま
 *
 * シーンの読み込み・切り替え・終了の途中は、何もしない(accept_user_action())。
 *
 * @note スレッド: UI スレッドで呼ぶこと。
 * @see hotkeys(呼び出し元)
 */
void on_hide_requested()
{
	if (!accept_user_action("hide"))
		return;

	if (!is_clipboard_image_showing()) {
		obs_log(LOG_INFO, "hide: no image shown");
		auto_hide_on_image_hidden();
		return;
	}

	obs_log(LOG_INFO, "hide requested (trigger: hotkey)");
	hide_clipboard_image(true);
	auto_hide_on_image_hidden();
}

/**
 * @brief 「自動非表示を一時停止/再開」ホットキーの処理。
 *
 * @details auto_hide_toggle_pause() を呼ぶ。シーンの読み込み・切り替え・終了の途中は、何もしない(accept_user_action())。
 *          表示中かどうかを調べるときに、片付け中のシーンを触らないため。
 *
 * @note スレッド: UI スレッドで呼ぶこと。
 * @see hotkeys(呼び出し元)
 */
void on_toggle_pause_requested()
{
	if (!accept_user_action("toggle pause"))
		return;
	auto_hide_toggle_pause();
}

/**
 * @brief このプラグインのホットキー 1 つ分の情報。
 */
struct HotkeyEntry {
	/**
	 * @brief 内部名。
	 * @warning OBS がこの名前で割り当てを見分けるため、変更すると既存の割り当てが消える。
	 */
	const char *name;
	/// @brief 表示名の、data/locale の ini のキー。表示名は変えても割り当ては消えない。
	const char *text_key;
	/**
	 * @brief SAVE_KEY の中で、割り当てを保存するキー名。
	 * @warning 変更すると既存の割り当てが読み込めなくなる。
	 */
	const char *save_key;
	/// @brief 押したときの処理。UI スレッドで呼ばれる(on_hotkey_pressed() が UI スレッドに回す)。
	void (*action)();
	/// @brief 登録した ID。登録前・解除後は OBS_INVALID_HOTKEY_ID。
	obs_hotkey_id id;
};

/**
 * @brief ホットキーが押された/離されたときの処理。全ホットキーで共通。
 *
 * @details 押したときだけ、そのホットキーの処理(HotkeyEntry::action)を UI スレッドに回す。
 *          表示・ソースの操作・Qt のタイマーやクリップボードは、UI スレッドでしか触れないため。
 *
 * @param data    押されたホットキーの HotkeyEntry(登録時に渡したもの。hotkeys の要素なので、ずっと残る)。
 * @param pressed 押したとき true、離したとき false。
 *
 * @note 呼び出し元: OBS(obs_module_load() で登録したホットキー)。
 * @note スレッド: ホットキー専用スレッド(UI スレッドではない)。
 */
void on_hotkey_pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	/* キーを離したときにも呼ばれるので、押したときだけ処理する */
	if (!pressed)
		return;

	obs_queue_task(
		OBS_TASK_UI, [](void *param) { static_cast<const HotkeyEntry *>(param)->action(); }, data, false);
}

/**
 * @brief このプラグインのホットキーの一覧。
 *
 * @details
 * **この順で、OBS の設定 > ホットキーに並ぶ。**
 * OBS はプラグインのホットキーを、見出しのない一般の欄に登録した順で並べる(名前順には並べ替えない)。
 * 並び順は保存されず、起動のたびにこの一覧の順で登録し直すので、ここで並べ替えれば次の起動から反映される。
 * - 新しいホットキーは、この一覧に足すだけにする(登録・保存・読み込み・解除はこの一覧から行う)
 * - 登録は obs_module_load() でまとめて行う。後から(起動完了時など)登録すると、
 *   ほかのプラグインのホットキーの後ろに離れて並んでしまう
 */
HotkeyEntry hotkeys[] = {
	{"clipboard_image_overlay.show", "Hotkey.ShowClipboardImage", "show_hotkey",
	 []() { on_show_requested("hotkey"); }, OBS_INVALID_HOTKEY_ID},
	{"clipboard_image_overlay.show_previous", "Hotkey.ShowPrevious", "previous_hotkey", on_show_previous_requested,
	 OBS_INVALID_HOTKEY_ID},
	{"clipboard_image_overlay.hide", "Hotkey.Hide", "hide_hotkey", on_hide_requested, OBS_INVALID_HOTKEY_ID},
	{"clipboard_image_overlay.toggle_pause", "Hotkey.TogglePause", "pause_hotkey", on_toggle_pause_requested,
	 OBS_INVALID_HOTKEY_ID},
};

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
 * 保存形式: { "clipboard-image-overlay": { "show_hotkey": [...], "previous_hotkey": [...], "hide_hotkey": [...], "pause_hotkey": [...] } }
 * (キー名は hotkeys の save_key)
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
		for (const HotkeyEntry &hotkey : hotkeys)
			save_hotkey(obj, hotkey.save_key, hotkey.id);
		obs_data_set_obj(save_data, SAVE_KEY, obj);
		obs_data_release(obj);
		return;
	}

	/* 読み込み: 保存データがなければ(初回など)割り当てなしのまま */
	obs_data_t *obj = obs_data_get_obj(save_data, SAVE_KEY);
	if (!obj)
		return;

	for (const HotkeyEntry &hotkey : hotkeys)
		load_hotkey(obj, hotkey.save_key, hotkey.id);
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
 *   その後、全シーンの通知(表示/非表示・追加・削除)を登録し、保存フォルダの古い画像を掃除する。
 * - シーンコレクション切り替えの開始時(SCENE_COLLECTION_CHANGING): タイマーと通知の登録を先に片付ける。
 *   切り替えの途中でタイマーが発火し、片付け中のシーンを触るのを防ぐため。
 * - シーン一覧の変更時(SCENE_LIST_CHANGED): 追加されたシーンの通知も登録する。
 *   読み込み・切り替え・終了の途中は登録しない(scenes_ready)。
 * - 終了時(EXIT): 設定画面が開いていれば閉じ、自動で消すタイマーと、画像差し替えのタイマーを止める。
 *   履歴ドックのフォルダの見張りとサムネイルの作成も止める。
 *   終了処理の途中でタイマーなどが発火し、片付け中のシーンやソース・画面を触るのを防ぐため。
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
		/* 前回までにたまった古い画像を掃除する。読み込んだコレクションで使用中の画像は残す */
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
		history_dock_shutdown();
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
 * @details ツールメニューの項目追加、ホットキー登録、表示履歴のドックの登録、
 *          保存処理と画面側イベント(起動完了・終了など)の登録を行う。
 *
 * @return true で読み込み成功。false を返すと OBS はこのプラグインを使わない。
 */
bool obs_module_load(void)
{
	/* 表示時間などの設定を、表示処理より先に読み込んでおく */
	settings_load();

	add_tools_submenu();

	/* 一覧の順に続けて登録する(この順で設定画面に並ぶ。hotkeys の説明を参照) */
	for (HotkeyEntry &hotkey : hotkeys) {
		hotkey.id = obs_hotkey_register_frontend(hotkey.name, obs_module_text(hotkey.text_key),
							 on_hotkey_pressed, &hotkey);
		/* ホットキーが登録できなくてもメニューやドックからは使えるので、警告だけ出して続ける */
		if (hotkey.id == OBS_INVALID_HOTKEY_ID)
			obs_log(LOG_WARNING, "failed to register hotkey %s", hotkey.name);
	}

	/*
	 * 表示履歴のドック。クリックされた画像は、ホットキーと同じ処理で表示する。
	 * 右クリックメニューの「削除」は delete_history_image()、「設定...」は設定画面を開く。
	 * ドックのハイライトは今の画像('Clipboard Image' が参照している画像)で、表示中か非表示かは問わない。
	 * 問い合わせには、シーンの読み込み・切り替え・終了の途中は答えない(片付け中のシーンを触らないため)
	 */
	HistoryDockCallbacks dock_callbacks;
	dock_callbacks.show = [](const std::string &path) {
		show_history_image(path, "history");
	};
	dock_callbacks.current = []() {
		if (!scenes_ready)
			return std::string();
		return current_clipboard_image();
	};
	dock_callbacks.remove = delete_history_image;
	dock_callbacks.open_settings = open_settings_dialog;
	history_dock_register(std::move(dock_callbacks));

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
	history_dock_unregister();
	auto_hide_shutdown();
	image_display_shutdown();

	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	obs_frontend_remove_save_callback(on_frontend_save, nullptr);

	for (HotkeyEntry &hotkey : hotkeys) {
		if (hotkey.id != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_unregister(hotkey.id);
			hotkey.id = OBS_INVALID_HOTKEY_ID;
		}
	}

	obs_log(LOG_INFO, "plugin unloaded");
}
