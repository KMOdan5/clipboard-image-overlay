// SPDX-FileCopyrightText: 2026 KMOdan5
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file image-display.cpp
 * @brief 保存した画像を、プラグイン専用の画像ソースとして現在のシーン(と、別キャンバスで今映っているシーン)に表示する。
 *
 * @details
 * 使う OBS の部品:
 * - 画像ソース(内部 ID: image_source)… 画像ファイルを表示する OBS 標準のソース。GIF のアニメーションも再生される
 * - シーンアイテム … シーンに置かれたソース。位置・大きさ・表示/非表示はここで持つ
 *
 * 専用ソースは名前「Clipboard Image」で作り、毎回それを使い回す。
 * こうすることで、新しい画像を貼ると前の画像と差し替わり、画面に画像が溜まらない。
 * 作るときは、画像ソースのプロパティ「表示されていないときに画像を読み込まない」をオンにする(create_image_source())。
 *
 * 画像ソースを増やす(「画像を新しい 'Clipboard Image' に表示」):
 * - 新しい画像ソースを「Clipboard Image 2」「3」… と番号付きの名前で作り、それに表示する。前の画像ソースはそのまま残す
 * - 新しい画像ソースには、それまでのアクティブのプロパティ・フィルタと、今のシーンでの見た目(位置を除く)を引き継ぐ。
 *   位置は画面中央にする。今のシーンに置かれていなければ、見た目は初めて追加したときと同じ(画面中央とフェード)
 * - プラグインが操作するのは、番号が一番大きい画像ソース(アクティブ)だけ。無印の「Clipboard Image」は 1 番として数える。
 *   どのシーンにあるかに関係なく、全体で一番大きい番号を見る(シーンごとに見ると、シーンを切り替えるたびに操作の対象が変わるため)
 * - アクティブ以外(非アクティブ)の画像ソースは、表示/非表示も含めて一切触らない(表示したまま残るのは配信者が消す)。
 *   例外: カウント中に増やしたときは、前のアクティブを、残り時間がたったら一度だけ非表示にしてから手放す
 *   (次々に出した画像が、古いものから順に消えるように。残したいときは一時停止してから増やす。retire_source())
 * - アクティブを削除すると、一つ前の画像ソースがアクティブに戻る。それが表示中でも、自動で非表示にするタイマーは止め、
 *   表示したまま残す(削除した画像のための残り時間で、配信者が残していた画像を非表示にしないため。item_removed_task())
 *   古い画像の掃除では、非アクティブの画像ソースが参照している画像も残す(clipboard_images_in_use())
 * - 以下の説明の「Clipboard Image」「専用ソース」は、アクティブの画像ソースのこと
 *
 * 大きさの決め方:
 * - 境界(バウンディングボックス)は使わない(OBS の既定と同じ「自動」)。
 *   境界を使うと、ソースをドラッグで拡大縮小しても枠だけが変わり画像の大きさが変わらず、
 *   OBS の普段の操作と違って分かりにくいため。
 * - 代わりに貼るたびに画像の幅・高さを見て表示サイズを決め、大きい画像を
 *   画面の幅・高さの「最大サイズの割合」(設定画面で変更。既定 60%)に収める。小さい画像は等倍(拡大しない)。
 * - 表示サイズは OBS の仕組み上「倍率」で設定する。境界が「自動」のとき、シーンアイテムが持つのは
 *   倍率で、「変換の編集」の「大きさ」(幅・高さ)は 元の画像の大きさ × 倍率 で計算された表示用の値のため。
 * - 画像や画面の大きさが取れない場合は、等倍(元の大きさのまま)で表示する。
 * - 境界が「自動」以外のときは表示サイズを変えない(境界の枠が大きさを決めるため)。
 *
 * 表示と非表示:
 * - 表示中に新しい画像を貼ったときは、フェードアウト → 差し替え → フェードイン の順に入れ替える
 *   (フェードアウトの時間だけ QTimer で待つ)。
 * - 表示・非表示の切り替え時に、シーンアイテムの「表示トランジション」「非表示トランジション」で
 *   フェードさせる(OBS 標準の機能。シーンアイテムを右クリックすると出てくる設定と同じもの)。
 * - フェードを設定するのは、シーンに初めて追加したときだけ。以降は配信者が「なし」や別のものに
 *   変えても、そのまま使う(貼るたびに設定し直すと、配信者の設定を勝手に戻してしまうため)。
 *
 * 初めて追加したあとにプラグインが変えるもの:
 * - 画像ファイル(貼った画像に差し替える)
 * - 表示サイズ(境界が「自動」のときだけ)
 * - 重なり順(「常に最前面に表示する」がオンのとき、貼るたびに最前面へ。既定オン)
 * - 表示/非表示(表示する・消す)
 * - 画像ファイルの参照を外す(配信者が今の画像を削除したとき。clear_clipboard_image())
 * 位置・境界・トランジションなど、それ以外の設定は変えない。
 * - 消すときは、現在のシーンだけでなく全シーンの Clipboard Image を非表示にする。
 *   表示中にシーンを切り替えても、元のシーンに画像が出しっぱなしにならないようにするため。
 *
 * 表示/非表示の検知:
 * - 全シーンの item_visible(表示/非表示)・item_add(追加)・item_remove(削除)通知を受け取る。
 *   プラグインのソース(is_managed_source())のものだけを扱い、他のソースの通知は名前と種類を比べるだけで捨てる。
 * - 通知は UI スレッドに回し、処理する時点の状態を見て判断する(handle_visibility_changed())。
 *   - どのシーンにも表示されていなければ、一時停止を解除してタイマーを止める(auto_hide_on_image_hidden())
 *   - 配信者が表示・追加したなら、貼ったときと同じくカウントを始める(auto_hide_restart())
 *   - 差し替え中(フェードアウト中)は「表示が続いている」扱いにして何もしない
 * - プラグイン自身の追加・表示の通知は捨てる(self_changing)。カウントは plugin-main.cpp で始めているため、
 *   二重に数えないようにする。
 * - 片付け(シーンコレクションの切り替え・終了)より前に回した通知は、片付けた後には処理しない(watch_generation)。
 * - 差し替えを待つ間にアイテム・ソース・シーンが削除されたら、差し替えを取り消す。
 *
 * スタジオモード:
 * - 「シーンを複製」がオンだと、プログラムに映るのはトランジションのたびに作られるシーンのコピー(非公開のシーン)。
 *   obs_frontend_get_current_scene() は元のシーンを返すので、元のシーンに表示した画像は次のトランジションまでプログラムに出ない
 * - 設定の studio_mode_show_on_program がオンなら、プログラムに映っているコピー(get_program_copy())にも表示・差し替えをする。
 *   既定はオフ(OBS のスタジオモードの操作感を変えるため)。OBS の「シーンを複製」の設定そのものは変えない
 * - 非表示は設定に関係なく、いつもコピーにも行う。トランジションでコピーに写った画像が、次のトランジションまで出たままにならないように
 * - 「表示中か」の判断にもコピーを含める(コピーに表示が残っている間は、自動で非表示にするタイマーを止めないように)
 * - コピーへの変更は次のトランジションで作り直されて消える(その時点の元のシーンの状態が写される)。コピーの通知は受け取らない
 * - コピーを扱う処理は、呼び出し元の分岐で必要なときだけ通す(関係のない場面で思わぬ不具合を起こさないため)
 *   - スタジオモードでないときは、どの処理も通さない(in_studio_mode())
 *   - 表示・差し替えと、今の画像を削除したときにコピーの画像ソースの参照を外す処理は、設定がオンのときだけ通す
 *   - 非表示と「表示中か」の判断は、設定がオフでもスタジオモードなら通す(トランジションでコピーに写った画像を残さないため)
 *
 * グループ:
 * - Clipboard Image を探すときは、グループの中も探す(collect_items())。表示・非表示・差し替えは、
 *   グループの外に置いたときと同じように動く
 * - 通知はシーンにだけ登録し、グループには登録しない。グループの中で手動で表示/非表示を切り替えても
 *   気づけないため、手動で表示したときはカウントが始まらない(既知の制限。#37 で検討する)
 * - グループの表示/非表示・位置・大きさは変えない。表示サイズはグループの拡大率に関係なく、キャンバスを基準に決める
 * - 「常に最前面に表示する」は、グループの中での最前面になる
 *
 * 別キャンバス(縦型配信プラグインなどが OBS のキャンバスの仕組みで作る、メイン以外の画面):
 * - どのプラグインのキャンバスでも同じように扱う。キャンバスのシーンは obs_canvas_enum_scenes()、
 *   今映っているシーンは、キャンバスのチャンネル 0(トランジションなら、その切り替え先)で取る
 * - 非表示・「表示中か」の判断・通知の受け付けは、メインと同じく全キャンバスの全シーンが対象(for_each_scene())
 * - 表示は、別キャンバスで今映っているシーンに、配信者が Clipboard Image(メインと同じソース)を置いたときだけ行う。
 *   置いていなければ追加しない(配信者の配置を崩さないため)。追加しないので、位置・フェードは設定しない
 * - 表示サイズ(境界が「自動」のとき)と最前面への移動は、メインと同じ決まりで行う。表示サイズはそのキャンバスの大きさで計算する
 * - メインで差し替え(フェードアウト → 差し替え → フェードイン)をするときは、別キャンバスの画像も一緒にフェードアウトさせ、
 *   差し替えた後に表示する。画像ファイルはメインと同じソースなので、差し替えはメインで 1 回行えば済む
 *
 * 今の画像を配信者が削除するとき(表示履歴のドックの「削除」):
 * - clear_clipboard_image() で、全シーンの Clipboard Image を即座に非表示にしてから、画像ファイルの参照を外す。
 *   ファイルを先に消しても、OBS は読み込んだ画像を持っているので画面に残ってしまうため
 *
 * 公開している関数の説明は image-display.hpp を参照。
 */

#include "image-display.hpp"
#include "auto-hide.hpp"
#include "clipboard-capture.hpp"
#include "notice.hpp"
#include "settings.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <graphics/vec2.h>

#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

/**
 * @brief プラグイン専用の画像ソースの名前。OBS のソース一覧にこの名前で表示される。
 *
 * @warning 既存のソースをこの名前で探すため、翻訳対象にしない(言語を変えると別のソース扱いになる)。
 *          変更すると、以前の設定で作られたソースを見つけられなくなる。
 */
constexpr const char *SOURCE_NAME = "Clipboard Image";

/// @brief OBS 標準の画像ソースの内部 ID。
constexpr const char *IMAGE_SOURCE_ID = "image_source";

/**
 * @brief シーンに初めて追加したときに設定する、フェードイン・フェードアウトの時間(ミリ秒)。
 * @note プラグインの設定には含めない。配信者は OBS 標準の表示/非表示トランジションの画面で変えられる。
 */
constexpr uint32_t FADE_DURATION_MS = 400;

/// @brief OBS 標準のフェードトランジションの内部 ID。
constexpr const char *FADE_TRANSITION_ID = "fade_transition";

/**
 * @brief 画像ソースに、表示する画像ファイルを設定する(前の画像と差し替わる)。
 *
 * @param source 対象の画像ソース。
 * @param path   表示する画像ファイルのパス(UTF-8)。
 */
void set_image_file(obs_source_t *source, const char *path)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", path);
	obs_source_update(source, settings);
	obs_data_release(settings);
}

/**
 * @brief 名前が、プラグインの画像ソースの名前の形なら、その番号を返す。
 *
 * @details 無印の「Clipboard Image」は 1、「Clipboard Image 2」は 2。番号は 2 以上で、先頭に 0 を付けない形だけを数える。
 *
 * @param name ソースの名前(nullptr でもよい)。
 * @return 番号。プラグインの名前の形でなければ 0。
 *
 * @note スレッド: どのスレッドから呼んでもよい(文字列を見るだけ)。
 */
int managed_number(const char *name)
{
	if (!name)
		return 0;
	const size_t base = std::strlen(SOURCE_NAME);
	if (std::strncmp(name, SOURCE_NAME, base) != 0)
		return 0;
	const char *rest = name + base;
	if (*rest == '\0')
		return 1;
	if (rest[0] != ' ' || rest[1] < '1' || rest[1] > '9')
		return 0;
	long number = 0;
	for (const char *p = rest + 1; *p; ++p) {
		/* 数字以外が混じる、または桁が多すぎる名前は、プラグインの名前として扱わない */
		if (*p < '0' || *p > '9' || number > 1000000)
			return 0;
		number = number * 10 + (*p - '0');
	}
	return number >= 2 ? static_cast<int>(number) : 0;
}

/// @brief 番号から、プラグインの画像ソースの名前を作る(1 なら無印の「Clipboard Image」)。
std::string managed_name(int number)
{
	return number <= 1 ? std::string(SOURCE_NAME) : std::string(SOURCE_NAME) + " " + std::to_string(number);
}

/// @brief ソースが OBS 標準の画像ソースかどうか。
bool is_image_source(obs_source_t *source)
{
	const char *id = source ? obs_source_get_unversioned_id(source) : nullptr;
	return id && std::strcmp(id, IMAGE_SOURCE_ID) == 0;
}

/**
 * @brief 今のアクティブの画像ソースの名前。シーンの通知(UI スレッド以外からも届く)で見分けるために覚えておく。
 * @details UI スレッドで active_source_name() を呼ぶたびに更新する。読み書きは active_name_mutex で守る。
 */
std::string active_name = SOURCE_NAME;
/// @brief active_name を守るロック。
std::mutex active_name_mutex;

/**
 * @brief プラグインの画像ソースのうち、一番大きい番号を返す。
 * @details 削除の途中のソースと、画像ソース以外(配信者が同じ名前で作ったテキストなど)は数えない。
 * @return 一番大きい番号。プラグインの画像ソースがなければ 0。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
int highest_managed_number()
{
	int highest = 0;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (is_image_source(source) && !obs_source_removed(source)) {
				int *value = static_cast<int *>(param);
				*value = std::max(*value, managed_number(obs_source_get_name(source)));
			}
			return true;
		},
		&highest);
	return highest;
}

/**
 * @brief アクティブの画像ソース(番号が一番大きいもの)の名前を求め、覚えておく。
 * @return アクティブの名前。プラグインの画像ソースがなければ無印の「Clipboard Image」(次に作る名前)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
std::string active_source_name()
{
	const std::string name = managed_name(highest_managed_number());
	const std::lock_guard<std::mutex> lock(active_name_mutex);
	active_name = name;
	return name;
}

/**
 * @brief ソースが、このプラグインのアクティブの 'Clipboard Image' かどうかを判断する。
 *
 * @details
 * 名前がアクティブの名前で、OBS 標準の画像ソースのものを、このプラグインのソースとして扱う。
 * 配信者が同じ名前で別の種類のソース(テキストなど)を作っていても、それは触らない。
 * 非アクティブの画像ソース(番号が一番大きくないもの)も、ここでは対象にしない(プラグインは触らないため)。
 * 削除の途中かどうかはここでは見ない。ソースを削除したときに届く通知(アイテムの削除)も、
 * このプラグインのソースのものとして受け取る必要があるため。使えるかどうかは find_managed_source() で判断する。
 *
 * プラグインのソースかどうかの判断は、すべてこの関数と find_managed_source() で行う。
 *
 * @param source 調べるソース(nullptr でもよい)。
 * @param name   アクティブの名前。省略すると、最後に active_source_name() で求めた名前(シーンの通知から呼ぶとき用)。
 * @return このプラグインのアクティブのソースなら true。
 *
 * @note スレッド: どのスレッドから呼んでもよい(シーンの通知の中からも呼ばれる)。
 */
bool is_managed_source(obs_source_t *source, const char *name = nullptr)
{
	if (!source || !is_image_source(source))
		return false;
	const char *source_name = obs_source_get_name(source);
	if (!source_name)
		return false;
	if (name)
		return std::strcmp(source_name, name) == 0;
	const std::lock_guard<std::mutex> lock(active_name_mutex);
	return active_name == source_name;
}

/**
 * @brief このプラグインのアクティブの 'Clipboard Image'(番号が一番大きい画像ソース)を探す。作りはしない。
 *
 * @return 見つかったソース(参照カウント付き。使い終わったら obs_source_release() で解放する)。
 * @retval nullptr ない、または削除の途中。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 * @see is_managed_source()(判断の決まり)、get_or_create_image_source()(なければ作る)
 */
obs_source_t *find_managed_source()
{
	const std::string name = active_source_name();
	obs_source_t *source = obs_get_source_by_name(name.c_str());
	/* 削除の途中(OBS が片付けるまでの一瞬)のソースは使わない。消えるソースに画像を出してしまうため */
	if (source && (!is_managed_source(source, name.c_str()) || obs_source_removed(source))) {
		obs_source_release(source);
		return nullptr;
	}
	return source;
}

/**
 * @brief 画像ソースに設定されている画像ファイルのパスを返す。
 * @param source 画像ソース。
 * @return 画像ファイルのパス(UTF-8)。設定されていなければ空。
 */
std::string image_file_of(obs_source_t *source)
{
	obs_data_t *settings = obs_source_get_settings(source);
	const char *file = obs_data_get_string(settings, "file");
	std::string path = file ? file : "";
	obs_data_release(settings);
	return path;
}

/**
 * @brief 画像ソースを作る。
 *
 * @details
 * base を渡したときは、base の画像ソースのプロパティ(「表示されていないときに画像を読み込まない」など)とフィルタを写す。
 * 画像ファイルだけは path にする。同じシーンで続けて別の画像を出すときに、設定が変わらないようにするため。
 * base がないときは、「表示されていないときに画像を読み込まない」をオンにして作る(OBS の既定はオフ)。
 * 表示するたびに別の画像に差し替えるので、非表示の間に画像を持っておいても使い道がないため。
 * 作った後は、配信者が変えた設定を上書きしない。
 *
 * @param name 作るソースの名前。
 * @param path 設定する画像ファイルのパス(UTF-8)。
 * @param base 設定を写す元の画像ソース。nullptr なら OBS の既定の設定で作る。
 * @return 画像ソース(参照カウント付き)。
 * @retval nullptr 作成に失敗した(お知らせも出す)。
 */
obs_source_t *create_image_source(const std::string &name, const char *path, obs_source_t *base = nullptr)
{
	obs_data_t *settings = obs_data_create();
	if (base) {
		obs_data_t *base_settings = obs_source_get_settings(base);
		obs_data_apply(settings, base_settings);
		obs_data_release(base_settings);
	} else {
		obs_data_set_bool(settings, "unload", true);
	}
	obs_data_set_string(settings, "file", path);
	obs_source_t *source = obs_source_create(IMAGE_SOURCE_ID, name.c_str(), settings, nullptr);
	obs_data_release(settings);

	if (source && base) {
		obs_source_copy_filters(source, base);
		obs_log(LOG_INFO, "display: copied settings and filters from \"%s\"", obs_source_get_name(base));
	}

	if (source) {
		obs_log(LOG_INFO, "display: created source \"%s\"", name.c_str());
		/* 作ったソースがアクティブになる(番号が一番大きい)。通知で見分けられるよう、すぐに覚え直す */
		active_source_name();
	} else {
		obs_log(LOG_WARNING, "display: failed to create source \"%s\"", name.c_str());
		notice_report(Notice::DisplayFailed);
	}
	return source;
}

/**
 * @brief 専用の画像ソースを取得する。なければ作る。
 *
 * @details
 * 新しく作るときは、表示する画像ファイルを設定した状態で作る。
 * 既存のソースの画像ファイルは、ここでは変えない。表示中の画像をフェードアウトしてから
 * 差し替える必要があるため、差し替えのタイミングは呼び出し元が決める。
 *
 * new_source が true なら、アクティブがあっても新しい画像ソースを作る。名前は、一番大きい番号 + 1(ほかのソースが
 * その名前を使っていれば、空いている次の番号)。プラグインの画像ソースが 1 つもなければ、無印の「Clipboard Image」を作る。
 * 新しい画像ソースには、base のプロパティとフィルタを写す(create_image_source())。
 *
 * @param path       新しく作るときに設定する画像ファイルのパス(UTF-8)。
 * @param new_source true なら、新しい画像ソースを作る(「画像を新しい 'Clipboard Image' に表示」)。
 * @param base       new_source が true のとき、設定を写す元の画像ソース(それまでのアクティブ)。nullptr なら写さない。
 * @return 画像ソース(参照カウント付き。使い終わったら obs_source_release() で解放する)。
 * @retval nullptr 作成に失敗した、同じ名前の別種類のソースがある、または削除の途中。
 */
obs_source_t *get_or_create_image_source(const char *path, bool new_source, obs_source_t *base = nullptr)
{
	const int highest = highest_managed_number();
	if (new_source && highest > 0) {
		/* 番号は増やす一方にする。名前が別の種類のソースで使われていれば、空いている次の番号にする */
		for (int number = highest + 1;; number++) {
			const std::string name = managed_name(number);
			obs_source_t *existing = obs_get_source_by_name(name.c_str());
			if (!existing)
				return create_image_source(name, path, base);
			obs_source_release(existing);
		}
	}

	obs_source_t *source = find_managed_source();
	if (source)
		return source;

	/*
	 * 同じ名前のソースがあるのにプラグインのソースとして使えない場合は、作らずに失敗扱いにする。
	 * - 配信者が同じ名前で別の種類のソース(テキストなど)を作っている: 書き換えると配信者の設定を壊すため
	 * - 削除の途中(OBS が片付けるまでの一瞬): 消えるソースに画像を出してしまうため
	 * OBS は同じ名前のソースを 2 つ作れないので、新しく作ることもできない。
	 */
	const std::string name = managed_name(highest);
	source = obs_get_source_by_name(name.c_str());
	if (source) {
		if (obs_source_removed(source)) {
			obs_log(LOG_WARNING, "display: \"%s\" is being removed; try again", name.c_str());
			notice_report(Notice::DisplayFailed);
		} else {
			obs_log(LOG_WARNING, "display: a source named \"%s\" exists but is not an image source (%s)",
				name.c_str(), obs_source_get_unversioned_id(source));
			notice_report(Notice::SourceNameConflict);
		}
		obs_source_release(source);
		return nullptr;
	}
	return create_image_source(name, path);
}

/**
 * @brief キャンバスの大きさを取得する。
 *
 * @param canvas      対象のキャンバス。nullptr ならメインのキャンバス(OBS の設定 → 映像の基本解像度)。
 * @param[out] width  キャンバスの幅(ピクセル)。
 * @param[out] height キャンバスの高さ(ピクセル)。
 * @return 取得できたら true。
 */
bool get_canvas_size(obs_canvas_t *canvas, float &width, float &height)
{
	obs_video_info ovi = {};
	const bool ok = canvas ? obs_canvas_get_video_info(canvas, &ovi) : obs_get_video_info(&ovi);
	if (!ok || ovi.base_width == 0 || ovi.base_height == 0) {
		obs_log(LOG_WARNING, "display: cannot get canvas size");
		return false;
	}
	width = static_cast<float>(ovi.base_width);
	height = static_cast<float>(ovi.base_height);
	return true;
}

/**
 * @brief シーンに初めて追加したシーンアイテムを、画面中央に置く。
 *
 * @details
 * 基準点(配置)を中央にしてから画面中央に置く。基準点が中央なので、
 * 後で表示サイズが変わっても、画像は画面中央を中心に大きくなったり小さくなったりする。
 * reset_bounds が true なら、境界は「自動」(OBS_BOUNDS_NONE)にする(理由はファイル冒頭の説明を参照)。
 * 前の画像ソースの見た目を写したときは false にし、写した境界を残す。
 *
 * @param item         配置するシーンアイテム。
 * @param reset_bounds true なら、境界を「自動」にする。
 */
void apply_default_position(obs_sceneitem_t *item, bool reset_bounds = true)
{
	float canvas_w = 0.0f;
	float canvas_h = 0.0f;
	if (!get_canvas_size(nullptr, canvas_w, canvas_h))
		return;

	vec2 pos;
	vec2_set(&pos, canvas_w / 2.0f, canvas_h / 2.0f);

	/* 複数の変更を 1 回の更新としてまとめ、途中の中途半端な状態が画面に出ないようにする */
	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_pos(item, &pos);
	if (reset_bounds)
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_defer_update_end(item);

	obs_log(LOG_INFO, "display: placed at center (%.0f, %.0f)", pos.x, pos.y);
}

/**
 * @brief 画像の大きさに合わせて、表示サイズ(「変換の編集」の「大きさ」)を決める。
 *
 * @details
 * 「最大サイズの割合」(設定画面で変更。既定 60%)は、キャンバスの面積に対する割合として扱う。
 * 最大の幅と高さは、画面の幅と高さにそれぞれ √(割合) をかけて出す(max_size_side_ratio())。
 * 画像がその幅・高さを超える場合だけ、収まるように縮小する。
 * 縦横比は保つ(幅と高さに同じ倍率を使う)。小さい画像は等倍のまま拡大しない。
 * OBS では表示サイズを倍率で設定するため、計算した倍率を丸めずに obs_sceneitem_set_scale() で設定する。
 *
 * 例: 画面 1920x1080、割合 60% → 最大 1487x837(√0.6 ≒ 0.775 倍。面積は画面の 60%)
 * - 3840x2160 の画像 → 約 0.387 倍(1487x837)
 * - 2000x500 の画像  → 約 0.744 倍(幅で決まる)
 * - 400x300 の画像   → 1 倍(そのまま)
 *
 * 画像や画面の大きさが取れない場合は、等倍(元の大きさのまま)にする。
 * 前の画像の倍率を引き継ぐと、表示される大きさが前の画像次第で変わってしまうため。
 *
 * 境界が「自動」以外のときは何もしない(表示サイズは境界の枠で決まる)。
 * OBS は境界があると倍率ではなく枠で大きさを決めるため、倍率を設定しても見た目は変わらない。
 * それでも設定すると、使われない倍率が裏で書き換わり(境界を「自動」に戻したときに思わぬ大きさになる)、
 * ログにも実際と違う大きさが出て調査の妨げになる。境界の種類はシーンアイテムの設定値として
 * 確実に取得できるので、それで判定する。
 *
 * @param item   対象のシーンアイテム。
 * @param image  表示する画像の情報(幅・高さを使う)。
 * @param canvas item が置かれているシーンのキャンバス。nullptr ならメインのキャンバス。
 */
void apply_display_size(obs_sceneitem_t *item, const SavedImage &image, obs_canvas_t *canvas = nullptr)
{
	const PluginSettings &settings = settings_get();
	if (obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE) {
		obs_log(LOG_INFO, "display: bounds type is not automatic; size is decided by the bounding box");
		return;
	}

	float factor = 1.0f;
	float canvas_w = 0.0f;
	float canvas_h = 0.0f;

	if (image.width <= 0 || image.height <= 0) {
		obs_log(LOG_WARNING, "display: unknown image size; showing at original size");
	} else if (get_canvas_size(canvas, canvas_w, canvas_h)) {
		const float ratio = static_cast<float>(max_size_side_ratio(settings.max_size_percent));
		const float max_w = canvas_w * ratio;
		const float max_h = canvas_h * ratio;
		const float fit =
			std::min(max_w / static_cast<float>(image.width), max_h / static_cast<float>(image.height));
		factor = std::min(1.0f, fit);
		obs_log(LOG_INFO, "display: size %.0fx%.0f (scale %.3f) for %dx%d image (max %.0fx%.0f)",
			image.width * factor, image.height * factor, factor, image.width, image.height, max_w, max_h);
	} else {
		obs_log(LOG_WARNING, "display: unknown canvas size; showing at original size");
	}

	vec2 scale;
	vec2_set(&scale, factor, factor);
	obs_sceneitem_set_scale(item, &scale);
}

/**
 * @brief シーンに追加したばかりのシーンアイテムに、表示・非表示トランジション(フェード)を設定する。
 *
 * @details
 * 呼ぶのはシーンに初めて追加したときだけ。以降は一切設定し直さない。
 * 配信者がトランジションを「なし」や別のもの(スライドなど)に変えたら、その設定をずっと使う。
 *
 * @param item 対象のシーンアイテム(追加したばかりのもの)。
 */
void set_initial_fade_transitions(obs_sceneitem_t *item)
{
	for (const bool show : {true, false}) {
		obs_source_t *fade = obs_source_create_private(FADE_TRANSITION_ID, nullptr, nullptr);
		if (!fade) {
			obs_log(LOG_WARNING, "display: failed to create fade transition (%s)", show ? "show" : "hide");
			continue; /* フェードなしでも表示・非表示はできるので、続ける */
		}
		/* obs_sceneitem_set_transition() は自分で参照を持つので、ここで作った分は解放してよい */
		obs_sceneitem_set_transition(item, show, fade);
		obs_sceneitem_set_transition_duration(item, show, FADE_DURATION_MS);
		obs_source_release(fade);
		obs_log(LOG_INFO, "display: fade %s transition set (%u ms)", show ? "show" : "hide", FADE_DURATION_MS);
	}
}

/**
 * @brief シーンアイテムを、非表示トランジションを使わずに即座に非表示にする。
 *
 * @details
 * 非表示トランジションを一時的に外してから非表示にし、終わったら元に戻す。
 * 配信者が設定したトランジションも、外したあと同じものを戻すので失われない。
 *
 * @param item 対象のシーンアイテム。
 * @return 非表示に切り替えたら true(もともと非表示なら false)。
 */
bool hide_item_instantly(obs_sceneitem_t *item)
{
	/* obs_sceneitem_get_transition() は参照を増やさないので、戻すまで消えないよう参照を持っておく */
	obs_source_t *transition = obs_source_get_ref(obs_sceneitem_get_transition(item, false));
	obs_sceneitem_set_transition(item, false, nullptr);
	const bool changed = obs_sceneitem_set_visible(item, false);
	obs_sceneitem_set_transition(item, false, transition);
	obs_source_release(transition);
	return changed;
}

/**
 * @brief 表示中のシーンアイテムを非表示にする。非表示のものには何もしない。
 *
 * @param item 対象のシーンアイテム。
 * @param fade true なら非表示トランジションでフェードアウトする。false なら即座に非表示にする(hide_item_instantly())。
 * @return 非表示に切り替えたら true(もともと非表示なら false)。
 */
bool hide_item(obs_sceneitem_t *item, bool fade)
{
	if (!obs_sceneitem_visible(item))
		return false;
	return fade ? obs_sceneitem_set_visible(item, false) : hide_item_instantly(item);
}

/**
 * @brief このスレッドで、プラグイン自身がシーンアイテムを追加・表示している最中なら true。
 *
 * @details
 * OBS の item_add / item_visible 通知は、obs_scene_add() / obs_sceneitem_set_visible() の中から
 * 同じスレッドで同期的に届く(OBS 本体の obs-scene.c で確認)。操作の直前に true、直後に false にすることで、
 * 通知が「プラグイン自身の操作」か「配信者の操作」かを on_item_visible() などで区別する。
 * プラグイン自身の表示は、呼び出し元(plugin-main.cpp)でカウントを始めているので、二重に数えないようにする。
 *
 * thread_local にしているのは、別のスレッド(ホットキーなど)で同時に起きた配信者の操作を、
 * プラグイン自身の操作と取り違えないようにするため。
 */
thread_local bool self_changing = false;

/**
 * @brief self_changing を、この変数が生きている間だけ true にする。
 *
 * @details 途中で return しても必ず false に戻るよう、変数の寿命で管理する。
 */
struct SelfChangeScope {
	SelfChangeScope() { self_changing = true; }
	~SelfChangeScope() { self_changing = false; }
	SelfChangeScope(const SelfChangeScope &) = delete;
	SelfChangeScope &operator=(const SelfChangeScope &) = delete;
};

/**
 * @brief ソースを非表示の状態でシーンに追加する。
 *
 * @details
 * obs_scene_add() で追加したアイテムは最初から表示状態になる。そのままだと初めて貼ったときだけ
 * フェードインせず、いきなり画面に出てしまう。そこで追加と非表示化を obs_scene_atomic_update() で
 * まとめて行い、その間に画面が描かれないようにする(OBS 本体がソースを追加するときと同じ方法)。
 *
 * @param scene  追加先のシーン。
 * @param source 追加するソース。
 * @return 追加したシーンアイテム(参照カウント付き。使い終わったら obs_sceneitem_release() で解放する)。
 * @retval nullptr 追加に失敗した。
 */
obs_sceneitem_t *add_hidden_scene_item(obs_scene_t *scene, obs_source_t *source)
{
	struct add_ctx {
		obs_source_t *source;
		obs_sceneitem_t *item;
	} ctx = {source, nullptr};

	/* 追加と非表示化で届く通知は、プラグイン自身の操作なので無視させる */
	const SelfChangeScope scope;
	obs_scene_atomic_update(
		scene,
		[](void *param, obs_scene_t *target) {
			auto *ctx = static_cast<add_ctx *>(param);
			ctx->item = obs_scene_add(target, ctx->source);
			if (!ctx->item)
				return;
			/* obs_scene_add() の戻り値は参照カウントを持たないので、後で解放できるよう揃える */
			obs_sceneitem_addref(ctx->item);
			obs_sceneitem_set_visible(ctx->item, false);
		},
		&ctx);

	return ctx.item;
}

/**
 * @brief シーンの中から、条件に合うソースが置かれているシーンアイテムをすべて集める。グループの中も探す。
 *
 * @details
 * 配信者が同じシーンに Clipboard Image を 2 つ以上置いている場合もあるため、
 * 非表示にするときや「どこかに表示されているか」を調べるときは、最初の 1 つだけでなく全部を見る。
 * 集めたあとで操作するのは、obs_scene_enum_items() の途中(シーンをロック中)に表示を切り替えないようにするため。
 *
 * グループの中に置かれたものも、シーンに置かれたものと同じように集める。
 * - 探さないと、貼るたびにグループの外へもう 1 つ追加され、自動で非表示にもならないため
 * - OBS ではグループの中にグループを入れられないので、中を探すのは 1 段だけで足りる
 * - グループの中を探すのは、シーンの探索中(シーンをロック中)に行う。OBS 本体も同じ順(シーン → グループ)でロックしている
 * - 通知(表示/非表示・追加・削除)はグループには登録しない。そのため、グループの中で手動で表示/非表示を
 *   切り替えたことには気づけない(既知の制限)。グループの表示/非表示や大きさ・位置も変えない
 *
 * OBS 31 で obs_scene_sceneitem_from_source() が非推奨になったため、
 * シーンアイテムを順に調べる obs_scene_enum_items() で探す。
 *
 * @param scene   探す対象のシーン。
 * @param matches ソース(obs_source_t *)を受け取り、集める対象なら true を返す関数。
 * @return 見つかったシーンアイテム(それぞれ参照カウント付き。release_items() で解放する)。
 *         並びは OBS のソース一覧の下から順(グループの中のものは、グループの位置に入る)。
 *
 * @see collect_items()(同じソース)、collect_managed_items()(名前と種類が Clipboard Image のソース)
 */
template<typename Pred> std::vector<obs_sceneitem_t *> collect_items_if(obs_scene_t *scene, Pred matches)
{
	struct search {
		Pred *matches;
		std::vector<obs_sceneitem_t *> found;

		/// @brief 1 つのアイテムを調べる。グループなら中のアイテムも同じ関数で調べる。
		static bool visit(obs_scene_t *, obs_sceneitem_t *item, void *param)
		{
			auto *ctx = static_cast<search *>(param);
			if ((*ctx->matches)(obs_sceneitem_get_source(item))) {
				obs_sceneitem_addref(item);
				ctx->found.push_back(item);
			} else if (obs_sceneitem_is_group(item)) {
				obs_sceneitem_group_enum_items(item, visit, param);
			}
			return true; /* 全部集めるので最後まで調べる */
		}
	} ctx = {&matches, {}};

	obs_scene_enum_items(scene, search::visit, &ctx);
	return ctx.found;
}

/**
 * @brief シーンの中から、指定したソースが置かれているシーンアイテムをすべて集める。グループの中も探す。
 *
 * @param scene  探す対象のシーン。
 * @param source 探すソース。
 * @return 見つかったシーンアイテム(collect_items_if() と同じ)。
 */
std::vector<obs_sceneitem_t *> collect_items(obs_scene_t *scene, obs_source_t *source)
{
	return collect_items_if(scene, [source](obs_source_t *candidate) { return candidate == source; });
}

/// @brief collect_items() で集めたシーンアイテムをすべて解放する。
void release_items(std::vector<obs_sceneitem_t *> &items)
{
	for (obs_sceneitem_t *item : items)
		obs_sceneitem_release(item);
	items.clear();
}

/**
 * @brief メイン以外のキャンバス(縦型配信プラグインなどが作るもの)を集める。
 *
 * @details 削除の印が付いたキャンバスは除く。obs_enum_canvases() はキャンバスの一覧をロックしたまま呼ぶので、
 *          その中では参照を取るだけにして、処理は呼び出し元でロックの外で行う。
 *
 * @return キャンバス(それぞれ参照カウント付き。release_canvases() で解放する)。
 */
std::vector<obs_canvas_t *> collect_other_canvases()
{
	std::vector<obs_canvas_t *> canvases;
	obs_enum_canvases(
		[](void *param, obs_canvas_t *canvas) {
			if ((obs_canvas_get_flags(canvas) & MAIN) == 0 && !obs_canvas_removed(canvas)) {
				obs_canvas_t *ref = obs_canvas_get_ref(canvas);
				if (ref)
					static_cast<std::vector<obs_canvas_t *> *>(param)->push_back(ref);
			}
			return true;
		},
		&canvases);
	return canvases;
}

/// @brief collect_other_canvases() で集めたキャンバスの参照を解放し、一覧を空にする。
void release_canvases(std::vector<obs_canvas_t *> &canvases)
{
	for (obs_canvas_t *canvas : canvases)
		obs_canvas_release(canvas);
	canvases.clear();
}

/**
 * @brief 出力に置かれたソースから、今映っているシーンを取り出す。
 *
 * @details トランジションなら、今映しているもの(トランジションの途中なら切り替え先)を返す。
 *
 * @param output 出力に置かれたソース(参照カウント付き)。この関数が解放する。
 * @return 今映っているソース(参照カウント付き。使い終わったら obs_source_release() で解放する)。
 * @retval nullptr output が nullptr、または何も映っていない。
 */
obs_source_t *take_active_source(obs_source_t *output)
{
	if (!output)
		return nullptr;
	obs_source_t *active = obs_source_get_type(output) == OBS_SOURCE_TYPE_TRANSITION
				       ? obs_transition_get_active_source(output)
				       : obs_source_get_ref(output);
	obs_source_release(output);
	return active;
}

/**
 * @brief 別キャンバスで今映っているシーンを返す。
 *
 * @details キャンバスのチャンネル 0 に置かれたもの(トランジションなら、その切り替え先)を見る。
 *          シーンでなければ(縦型配信プラグインが別の用途で使うキャンバスなど)扱わない。
 *
 * @param canvas 対象のキャンバス。
 * @return シーン(ソースとして。参照カウント付き。使い終わったら obs_source_release() で解放する)。
 * @retval nullptr シーンが映っていない。
 */
obs_source_t *get_canvas_active_scene(obs_canvas_t *canvas)
{
	obs_source_t *active = take_active_source(obs_canvas_get_channel(canvas, 0));
	if (active && !obs_scene_from_source(active)) {
		obs_source_release(active);
		return nullptr;
	}
	return active;
}

/**
 * @brief 全キャンバスの全シーンを順に処理する。
 *
 * @details
 * メインのキャンバスは、OBS の画面のシーン一覧(obs_frontend_get_scenes())を使う。
 * 別キャンバス(collect_other_canvases())は、キャンバスごとのシーン一覧(obs_canvas_enum_scenes())を使う。
 * obs_canvas_enum_scenes() はシーンの一覧をロックしたまま呼ぶので、その中では参照を取るだけにして、処理はロックの外で行う。
 *
 * @param fn シーン(ソースとして)ごとに呼ぶ処理。false を返すと、そこで打ち切る。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
template<typename Fn> void for_each_scene(Fn fn)
{
	std::vector<obs_source_t *> scenes;

	obs_frontend_source_list main_scenes = {};
	obs_frontend_get_scenes(&main_scenes);
	for (size_t i = 0; i < main_scenes.sources.num; i++)
		scenes.push_back(obs_source_get_ref(main_scenes.sources.array[i]));
	obs_frontend_source_list_free(&main_scenes);

	std::vector<obs_canvas_t *> canvases = collect_other_canvases();
	for (obs_canvas_t *canvas : canvases) {
		obs_canvas_enum_scenes(
			canvas,
			[](void *param, obs_source_t *scene_source) {
				static_cast<std::vector<obs_source_t *> *>(param)->push_back(
					obs_source_get_ref(scene_source));
				return true;
			},
			&scenes);
	}
	release_canvases(canvases);

	bool keep_going = true;
	for (obs_source_t *scene_source : scenes) {
		if (keep_going && scene_source)
			keep_going = fn(scene_source);
		obs_source_release(scene_source);
	}
}

/**
 * @brief 全キャンバスの全シーン(グループの中を含む)に置かれた、指定したソースのシーンアイテムを順に処理する。
 *
 * @details 全シーンを対象にする処理(表示中かの判断・非表示にする処理)で共通に使う。
 *          同じシーンに 2 つ以上置かれていれば、すべてを渡す。
 *
 * @param source 対象のソース。
 * @param fn     アイテムごとに呼ぶ処理。false を返すと、そこで打ち切る。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
template<typename Fn> void for_each_item_in_all_scenes(obs_source_t *source, Fn fn)
{
	for_each_scene([source, &fn](obs_source_t *scene_source) {
		obs_scene_t *scene = obs_scene_from_source(scene_source);
		if (!scene)
			return true;
		bool keep_going = true;
		std::vector<obs_sceneitem_t *> items = collect_items(scene, source);
		for (obs_sceneitem_t *item : items) {
			if (!fn(item)) {
				keep_going = false;
				break;
			}
		}
		release_items(items);
		return keep_going;
	});
}

/**
 * @brief シーンの中から、指定したソースが置かれているシーンアイテムを 1 つ探す。グループの中も探す。
 *
 * @details
 * collect_items() で集めたうちの最初の 1 つを返す(探し方とグループの扱いは collect_items() と同じ)。
 *
 * @param scene  探す対象のシーン。
 * @param source 探すソース。
 * @return 見つかったシーンアイテム(参照カウント付き。使い終わったら obs_sceneitem_release() で解放する)。
 * @retval nullptr シーン(グループの中を含む)に置かれていない。
 */
obs_sceneitem_t *find_scene_item(obs_scene_t *scene, obs_source_t *source)
{
	std::vector<obs_sceneitem_t *> items = collect_items(scene, source);
	if (items.empty())
		return nullptr;

	obs_sceneitem_t *first = items.front();
	items.erase(items.begin());
	release_items(items);
	return first;
}

/**
 * @brief スタジオモードかどうかを返す。
 *
 * @details
 * プログラムのコピーを扱う処理は、スタジオモードのときだけ意味がある。
 * 呼び出し元でこの関数を見て分岐し、スタジオモードでないときはコピーを扱う処理に入らないようにする
 * (コピーを探す処理そのものを通さないことで、関係のない場面での思わぬ不具合を防ぐため)。
 *
 * @return スタジオモードなら true。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
bool in_studio_mode()
{
	return obs_frontend_preview_program_mode_active();
}

/**
 * @brief スタジオモードで、プログラムに映っているシーンのコピーを返す。
 *
 * @details
 * スタジオモードで「シーンを複製」がオンだと、OBS はトランジションのたびにプログラムのシーンのコピー
 * (非公開のシーン)を作り、プログラムにはそのコピーを映す。一方 obs_frontend_get_current_scene() が返すのは
 * 元のシーンなので、元のシーンに表示した画像は、次のトランジションまでプログラムに出ない。
 * プログラムに映っているコピーにも同じ操作をするために、そのコピーを取り出す。
 *
 * 次をすべて満たすものをコピーとして扱う(OBS 本体の TransitionToScene() の作り方に合わせている):
 * - スタジオモード中
 * - 出力のトランジションが今映しているもの(トランジションの途中なら切り替え先)がシーンで、
 *   元のシーン(obs_frontend_get_current_scene() が返すシーン)とは別物
 * - 非公開のシーンで、名前が元のシーンと同じ
 *
 * 「シーンを複製」がオフなら、プログラムに映っているのは元のシーンそのものなので、nullptr を返す。
 *
 * @return コピーのシーン(参照カウント付き。使い終わったら obs_source_release() で解放する)。
 * @retval nullptr スタジオモードでない、「シーンを複製」がオフ、または取れなかった。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
obs_source_t *get_program_copy()
{
	if (!in_studio_mode())
		return nullptr;

	obs_source_t *active = take_active_source(obs_get_output_source(0));
	if (!active)
		return nullptr;

	obs_source_t *program_scene = obs_frontend_get_current_scene();
	const char *active_name = obs_source_get_name(active);
	const char *program_name = program_scene ? obs_source_get_name(program_scene) : nullptr;
	const bool is_copy = program_scene && active != program_scene && obs_scene_from_source(active) &&
			     obs_obj_is_private(active) && active_name && program_name &&
			     std::strcmp(active_name, program_name) == 0;
	obs_source_release(program_scene);
	if (!is_copy) {
		obs_source_release(active);
		return nullptr;
	}
	return active;
}

/**
 * @brief シーンの中から、Clipboard Image のシーンアイテムをすべて集める。グループの中も探す。
 *
 * @details
 * collect_items() と違い、ソースが同じものかではなく、名前と種類(is_managed_source())で見分ける。
 * スタジオモードの「ソースを複製」がオンだと、プログラムのコピーの中では画像ソースも非公開のコピーになり、
 * 元の Clipboard Image とは別物になるため。
 *
 * @param scene 探す対象のシーン。
 * @return 見つかったシーンアイテム(それぞれ参照カウント付き。release_items() で解放する)。
 */
std::vector<obs_sceneitem_t *> collect_managed_items(obs_scene_t *scene)
{
	const std::string name = active_source_name();
	return collect_items_if(scene, [&name](obs_source_t *candidate) {
		return is_managed_source(candidate, name.c_str());
	});
}

/**
 * @brief プログラムのコピーに置かれた Clipboard Image のシーンアイテムを、すべて集める。
 *
 * @return 見つかったシーンアイテム(それぞれ参照カウント付き。release_items() で解放する)。
 *         スタジオモードでない・「シーンを複製」がオフ・置かれていない場合は空。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
std::vector<obs_sceneitem_t *> collect_program_copy_items()
{
	std::vector<obs_sceneitem_t *> items;
	obs_source_t *copy = get_program_copy();
	if (copy) {
		items = collect_managed_items(obs_scene_from_source(copy));
		obs_source_release(copy);
	}
	return items;
}

/**
 * @brief シーンアイテムの位置・大きさ・切り抜き・トランジションなどを、別のシーンアイテムに写す。
 *
 * @details
 * 次の 2 つの場面で使う。OBS 本体がシーンを複製するとき(duplicate_item_data())と同じ項目を写す。
 * - プログラムのコピーに Clipboard Image がまだないとき(コピーが作られた後に元のシーンへ追加したとき)に、
 *   元のシーンと同じ見た目で置く
 * - 画像ソースを増やしたときに、新しい画像ソースのシーンアイテムに、それまでのアクティブの見た目を引き継ぐ
 * トランジションは、OBS 本体と同じく非公開のコピーを作って設定する(元のアイテムと共有しない)。
 *
 * @param dst 写し先のシーンアイテム。
 * @param src 写し元のシーンアイテム。
 */
void copy_item_layout(obs_sceneitem_t *dst, obs_sceneitem_t *src)
{
	obs_transform_info info;
	obs_sceneitem_get_info2(src, &info);
	obs_sceneitem_set_info2(dst, &info);

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(src, &crop);
	obs_sceneitem_set_crop(dst, &crop);

	obs_sceneitem_set_scale_filter(dst, obs_sceneitem_get_scale_filter(src));
	obs_sceneitem_set_blending_method(dst, obs_sceneitem_get_blending_method(src));
	obs_sceneitem_set_blending_mode(dst, obs_sceneitem_get_blending_mode(src));

	for (const bool show : {true, false}) {
		obs_source_t *transition = obs_sceneitem_get_transition(src, show);
		if (transition) {
			obs_source_t *copy = obs_source_duplicate(transition, obs_source_get_name(transition), true);
			obs_sceneitem_set_transition(dst, show, copy);
			obs_source_release(copy);
		}
		obs_sceneitem_set_transition_duration(dst, show, obs_sceneitem_get_transition_duration(src, show));
	}
}

/**
 * @brief プログラムのコピーにも、画像を表示する(スタジオモードで「シーンを複製」がオンのとき)。
 *
 * @details
 * 元のシーンに表示した後に呼ぶ。コピーがなければ何もしない(スタジオモードでない、または「シーンを複製」がオフ)。
 * 1. コピーに Clipboard Image がなければ、元のアイテムと同じ見た目で、非表示のまま追加する。
 *    元のアイテムがグループの中にある場合は追加しない(グループの中身は、コピーと元のシーンで共有されることがあり、
 *    追加すると二重に表示されるため)
 * 2. 画像ソースがコピーの中で別物になっていれば(「ソースを複製」がオン)、そのソースの画像ファイルも差し替える
 * 3. 表示サイズと重なり順を、元のシーンと同じ決まりで設定して、表示する(フェードイン)
 *
 * コピーは次のトランジションで作り直され、そのときは元のシーンの状態が写されるので、ここでの変更は残らない。
 *
 * @param original 元のシーンで表示したシーンアイテム。
 * @param image    表示する画像の情報。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void show_on_program_copy(obs_sceneitem_t *original, const SavedImage &image)
{
	obs_source_t *copy = get_program_copy();
	if (!copy)
		return;

	obs_scene_t *copy_scene = obs_scene_from_source(copy);
	std::vector<obs_sceneitem_t *> items = collect_managed_items(copy_scene);
	if (items.empty()) {
		obs_scene_t *original_scene = obs_sceneitem_get_scene(original);
		if (original_scene && obs_sceneitem_get_group(original_scene, original)) {
			obs_log(LOG_INFO, "display: not added to the program copy (the item is in a group)");
		} else {
			obs_sceneitem_t *added = add_hidden_scene_item(copy_scene, obs_sceneitem_get_source(original));
			if (added) {
				copy_item_layout(added, original);
				items.push_back(added);
				obs_log(LOG_INFO, "display: added to the program copy");
			}
		}
	}

	obs_source_t *original_source = obs_sceneitem_get_source(original);
	for (obs_sceneitem_t *item : items) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		if (source != original_source)
			set_image_file(source, image.path.c_str());
		apply_display_size(item, image);
		if (settings_get().always_on_top)
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
		obs_sceneitem_set_visible(item, true);
	}
	if (!items.empty())
		obs_log(LOG_INFO, "display: showing on the program copy (%zu item(s))", items.size());

	release_items(items);
	obs_source_release(copy);
}

/**
 * @brief プログラムのコピーに置かれた Clipboard Image のうち、表示中のものを非表示にする。
 *
 * @details
 * 「シーンを複製」がオンのとき、元のシーンを非表示にしても、プログラムに映るコピーの画像は残る。
 * トランジションでコピーに写った画像が、次のトランジションまで出たままにならないよう、非表示のたびにコピーも非表示にする。
 *
 * @param fade true なら非表示トランジションでフェードアウトする。false なら即座に非表示にする。
 * @return 非表示に切り替えたアイテムの数。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
int hide_on_program_copy(bool fade)
{
	std::vector<obs_sceneitem_t *> items = collect_program_copy_items();
	int hidden = 0;
	for (obs_sceneitem_t *item : items) {
		if (hide_item(item, fade))
			hidden++;
	}
	release_items(items);
	return hidden;
}

/**
 * @brief Clipboard Image が、いずれかのシーンで表示状態になっているかを調べる。
 *
 * @details
 * 自動で消す処理は全シーンを対象にするので、「表示中か」も全シーンで判断する。
 * 現在のシーンだけで判断すると、別のシーンに表示が残っているのにタイマーや一時停止を
 * 片付けてしまい、その画像が消えなくなるため。
 * スタジオモードのプログラムのコピー(get_program_copy())も同じ理由で含める。
 *
 * @return どこかのシーン(プログラムのコピーを含む)で表示状態なら true。Clipboard Image がなければ false。
 */
bool is_visible_in_any_scene()
{
	obs_source_t *source = find_managed_source();
	if (!source)
		return false;

	bool visible = false;
	for_each_item_in_all_scenes(source, [&visible](obs_sceneitem_t *item) {
		visible = obs_sceneitem_visible(item);
		return !visible; /* 1 つでも表示中なら打ち切る */
	});
	obs_source_release(source);

	/* スタジオモードのプログラムのコピーに表示が残っていれば、それも表示中として扱う(非表示にするまでタイマーを止めないため) */
	if (!visible && in_studio_mode()) {
		std::vector<obs_sceneitem_t *> items = collect_program_copy_items();
		visible = std::any_of(items.begin(), items.end(), obs_sceneitem_visible);
		release_items(items);
	}
	return visible;
}

/**
 * @brief 表示中の画像を差し替えるときの、待ち中の情報。
 *
 * @details
 * 差し替えは「フェードアウト → 画像を差し替え → フェードイン」の順に行う。
 * フェードアウトが終わるまで待つ間、ここに差し替え先の画像を持っておく。
 */
struct PendingSwap {
	/// @brief 差し替え中のシーンアイテム(参照カウント付き)。待ちがなければ nullptr。
	obs_sceneitem_t *item = nullptr;
	/**
	 * @brief item が置かれているシーン(弱い参照)。
	 * @details 待っている間にシーンが削除されると、item からシーンをたどれなくなる(解放済みになる)。
	 *          そのためシーンは別に弱い参照で持ち、差し替える前に残っているか確かめる。
	 */
	obs_weak_source_t *scene = nullptr;
	/// @brief フェードアウトが終わったら表示する画像。
	SavedImage image;
	/// @brief フェードアウトが終わったら、プログラムのコピーにも表示するか(設定の studio_mode_show_on_program)。
	bool show_on_program = false;
};

/// @brief 待ち中の差し替え。同時に 1 つだけ持つ。
PendingSwap pending_swap;

/// @brief フェードアウトが終わるのを待つタイマー。最初に使うときに作る。
QTimer *swap_timer = nullptr;

/// @brief フェードアウトの時間に足す余裕(ミリ秒)。フェードが終わり切る前に差し替えて、途中の画像がちらつくのを防ぐ。
constexpr int SWAP_MARGIN_MS = 50;

/**
 * @brief 画像ファイルを差し替え、表示サイズを決め直して、表示する(フェードイン)。
 *
 * @details 自動で消すカウントは呼び出し元(plugin-main.cpp)で始めるので、
 *          この表示は「配信者の操作」として数えないよう self_changing を立てておく。
 *
 * @param item  対象のシーンアイテム。
 * @param image 表示する画像の情報。
 */
void show_new_image(obs_sceneitem_t *item, const SavedImage &image)
{
	set_image_file(obs_sceneitem_get_source(item), image.path.c_str());
	apply_display_size(item, image);
	{
		const SelfChangeScope scope;
		obs_sceneitem_set_visible(item, true);
	}
	obs_log(LOG_INFO, "display: showing %s", image.path.c_str());
}

/**
 * @brief 別キャンバスで今映っているシーンに置かれた Clipboard Image を、順に処理する。
 *
 * @details 表示は「今映っているシーンに、配信者が置いたものだけ」に行う決まりなので、
 *          全シーンではなく今映っているシーンだけを見る。置いていなければ何もしない(追加しない)。
 *
 * @param source メインの Clipboard Image(別キャンバスにも同じソースが置かれる)。
 * @param fn     アイテムごとに呼ぶ処理。引数はアイテム、そのキャンバス、そのシーン(ソースとして)。
 *
 * @note スレッド: UI スレッドから呼ぶこと。
 */
template<typename Fn> void for_each_item_on_other_canvases(obs_source_t *source, Fn fn)
{
	std::vector<obs_canvas_t *> canvases = collect_other_canvases();
	for (obs_canvas_t *canvas : canvases) {
		obs_source_t *scene_source = get_canvas_active_scene(canvas);
		if (!scene_source)
			continue;
		std::vector<obs_sceneitem_t *> items = collect_items(obs_scene_from_source(scene_source), source);
		for (obs_sceneitem_t *item : items)
			fn(item, canvas, scene_source);
		release_items(items);
		obs_source_release(scene_source);
	}
	release_canvases(canvases);
}

/**
 * @brief 別キャンバスで今映っているシーンの Clipboard Image を表示する。
 *
 * @details
 * 画像ファイルはメインと同じソースなので、メインで設定した画像がそのまま出る。ここでは設定し直さない。
 * 表示サイズ(そのキャンバスの大きさで計算)と最前面への移動は、メインと同じ決まりで行う。
 * 自動で消すカウントは呼び出し元(plugin-main.cpp)で始めるので、self_changing を立てて表示する。
 *
 * @param source メインの Clipboard Image。
 * @param image  表示する画像の情報(大きさの計算に使う)。
 */
void show_on_other_canvases(obs_source_t *source, const SavedImage &image)
{
	for_each_item_on_other_canvases(source, [&image](obs_sceneitem_t *item, obs_canvas_t *canvas,
							 obs_source_t *scene_source) {
		if (settings_get().always_on_top)
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
		apply_display_size(item, image, canvas);
		{
			const SelfChangeScope scope;
			obs_sceneitem_set_visible(item, true);
		}
		obs_log(LOG_INFO, "display: also showing in scene \"%s\" of canvas \"%s\"",
			obs_source_get_name(scene_source), obs_canvas_get_name(canvas));
	});
}

/**
 * @brief 別キャンバスで今映っているシーンの Clipboard Image を、フェードアウトさせる。差し替えを始めるときに使う。
 *
 * @param source メインの Clipboard Image。
 */
void fade_out_on_other_canvases(obs_source_t *source)
{
	for_each_item_on_other_canvases(source, [](obs_sceneitem_t *item, obs_canvas_t *, obs_source_t *) {
		hide_item(item, true);
	});
}

/**
 * @brief 待ち中の差し替えを取り消す。待ちがなければ何もしない。
 */
void cancel_swap()
{
	if (swap_timer)
		swap_timer->stop();
	if (pending_swap.item) {
		obs_sceneitem_release(pending_swap.item);
		pending_swap.item = nullptr;
		obs_log(LOG_INFO, "display: pending swap canceled");
	}
	obs_weak_source_release(pending_swap.scene);
	pending_swap.scene = nullptr;
}

/**
 * @brief 差し替え中のシーンアイテムが、まだシーンに置かれているかを調べる。
 *
 * @details
 * フェードアウトを待つ間に、配信者がアイテム・ソース・シーンを削除することがある。
 * 削除されたアイテムを表示し直すと、消えたはずの画像が裏で動き続けるなどの不具合になるため、確かめてから使う。
 *
 * @return 置かれたままなら true。待ちがない・削除された場合は false。
 */
bool is_pending_swap_alive()
{
	if (!pending_swap.item)
		return false;

	obs_source_t *scene_source = obs_weak_source_get_source(pending_swap.scene);
	if (!scene_source)
		return false; /* シーンが削除された */

	bool alive = false;
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (scene && !obs_source_removed(scene_source)) {
		std::vector<obs_sceneitem_t *> items =
			collect_items(scene, obs_sceneitem_get_source(pending_swap.item));
		alive = std::find(items.begin(), items.end(), pending_swap.item) != items.end();
		release_items(items);
	}
	obs_source_release(scene_source);
	return alive;
}

/**
 * @brief フェードアウトが終わったときの処理。新しい画像に差し替えて、フェードインさせる。
 *
 * @details
 * 待っている間にアイテムが削除されていたら、差し替えずに取り消す。
 *
 * 差し替えた後に、古い画像の掃除を行う。掃除は、表示の操作が完了した時点で 1 回行う決まりで、
 * 差し替えがある場合は、ここが完了の時点になる(差し替えがない場合は、plugin-main.cpp の show_saved_image() が行う)。
 * 差し替えを待っている間は前の画像がまだ使用中なので、待ってから掃除すると、前の画像が上限の外にあれば一緒に削除できる。
 * 差し替えが取り消されたとき(アイテムが削除されたなど)は掃除しない。次に画像を表示したときに掃除される。
 *
 * @note 呼び出し元: Qt(swap_timer の timeout)。
 * @note スレッド: UI スレッド。
 */
void finish_swap()
{
	if (!pending_swap.item)
		return;

	if (!is_pending_swap_alive()) {
		obs_log(LOG_INFO, "display: swap target was removed");
		cancel_swap();
		return;
	}

	/* 先に待ちを空にしてから表示する(表示の通知を受けたとき「差し替え中」と判断させないため) */
	obs_sceneitem_t *item = pending_swap.item;
	const SavedImage image = pending_swap.image;
	const bool show_on_program = pending_swap.show_on_program;
	pending_swap.item = nullptr;
	obs_weak_source_release(pending_swap.scene);
	pending_swap.scene = nullptr;

	show_new_image(item, image);
	show_on_other_canvases(obs_sceneitem_get_source(item), image);
	if (show_on_program)
		show_on_program_copy(item, image);
	obs_sceneitem_release(item);

	/* 表示の操作がここで完了した。前の画像は使用中ではなくなったので、上限の外にあれば一緒に削除される */
	cleanup_saved_images(clipboard_images_in_use());
}

/**
 * @brief 表示中の画像を、フェードアウト → 差し替え → フェードイン で入れ替える。
 *
 * @details
 * フェードアウトにかかる時間(非表示トランジションの長さ)だけ待ってから差し替える。
 * 待っている間にもう一度貼られた場合は、待ち時間はそのままで、差し替え先だけを最新の画像にする
 * (最後に貼った画像が表示される。途中の画像が一瞬出たりはしない)。
 *
 * 別キャンバスで今映っているシーンの Clipboard Image も一緒にフェードアウトさせ、差し替えた後に表示する
 * (show_on_other_canvases())。
 * 設定の studio_mode_show_on_program がオンなら、プログラムのコピーも一緒にフェードアウトさせ、
 * 差し替えた後にコピーにも表示する(show_on_program_copy())。
 *
 * @param item         表示中のシーンアイテム。
 * @param scene_source item が置かれているシーン(ソースとして)。
 * @param image        新しく表示する画像の情報。
 */
void start_swap(obs_sceneitem_t *item, obs_source_t *scene_source, const SavedImage &image)
{
	if (pending_swap.item) {
		pending_swap.image = image;
		obs_log(LOG_INFO, "display: swap already in progress; next image updated");
		return;
	}

	if (!swap_timer) {
		swap_timer = new QTimer();
		swap_timer->setSingleShot(true);
		QObject::connect(swap_timer, &QTimer::timeout, finish_swap);
	}

	obs_sceneitem_addref(item);
	pending_swap.item = item;
	pending_swap.scene = obs_source_get_weak_source(scene_source);
	pending_swap.image = image;
	pending_swap.show_on_program = settings_get().studio_mode_show_on_program && in_studio_mode();

	/* 非表示トランジションがあればその長さだけ待つ。なければすぐ消えるので待たない */
	const bool has_fade = obs_sceneitem_get_transition(item, false) != nullptr;
	const int wait_ms =
		has_fade ? static_cast<int>(obs_sceneitem_get_transition_duration(item, false)) + SWAP_MARGIN_MS : 0;

	obs_sceneitem_set_visible(item, false); /* フェードアウト開始 */
	fade_out_on_other_canvases(obs_sceneitem_get_source(item));
	/* プログラムのコピーにも表示する設定なら、コピーの画像も一緒にフェードアウトさせる */
	if (pending_swap.show_on_program)
		hide_on_program_copy(true);
	swap_timer->start(wait_ms);
	obs_log(LOG_INFO, "display: swapping image (fade out %d ms)", wait_ms);
}

/**
 * @brief 通知の受け付けを片付けた回数。片付ける前に届いた古い通知を捨てるために使う。
 *
 * @details
 * 通知は UI スレッドに回してから処理するので、処理されるまでに少し間がある。
 * その間にシーンコレクションの切り替えや終了で片付けが行われると、古いコレクションの通知が
 * 新しいコレクションの状態に作用してしまう。通知を回すときにこの値を一緒に渡し、
 * 処理するときに値が変わっていたら捨てる。別スレッドから読むので atomic にしておく。
 */
std::atomic<uintptr_t> watch_generation{0};

/**
 * @brief 「表示/非表示が切り替わった」などの通知を受け取っているシーンの一覧(弱い参照)。
 *
 * @details
 * 弱い参照で持つのは、シーンが削除されたときにプラグインがシーンを生き残らせないようにするため。
 */
std::vector<obs_weak_source_t *> watched_scenes;

/**
 * @brief Clipboard Image の表示状態が変わったときの処理(UI スレッドで実行する)。
 *
 * @details
 * 通知の中身(どのアイテムか)ではなく、処理する時点の状態を見て判断する。
 * 通知が届くまでの間に状態が変わっていても(貼った・消した・削除した)、今の状態に合った動きになるため。
 * 1. 差し替え中のアイテムが削除されていたら、差し替えを取り消す
 * 2. 差し替え中なら何もしない(表示が続いている扱い。カウントは貼ったときに始めている)
 * 3. どのシーンにも表示されていない、またはアクティブの画像ソースが削除されたなら、一時停止を解除してタイマーを止める
 *    (一時停止は、画像がどこかに表示されている間だけ続ける。残ったままだと、次に表示した画像が非表示にならないため)。
 *    アクティブが削除されると、一つ前の画像ソースがアクティブに戻る。それが表示中でも、削除した画像のタイマーでは非表示にしない
 *    (表示したまま残す。タイマーが止まっているので、ドックには残り 0 秒の一時停止中と出る)
 * 4. 配信者が表示した(表示・追加の通知)なら、貼ったときと同じくカウントを始める
 *    (既にどこかに表示中でカウントしていた場合は、表示時間に戻る)
 *
 * @param shown          配信者が表示した通知(表示にした・表示状態で追加した)なら true。
 *                       非表示にした・削除した通知なら false。
 * @param active_removed 削除の通知で、通知のときのアクティブの画像ソースが、もうアクティブでないなら true。
 */
void handle_visibility_changed(bool shown, bool active_removed = false)
{
	if (pending_swap.item && !is_pending_swap_alive()) {
		obs_log(LOG_INFO, "display: swap target was removed");
		cancel_swap();
	}
	if (pending_swap.item)
		return;

	if (active_removed) {
		obs_log(LOG_INFO, "display: the active source was removed");
		auto_hide_on_image_hidden();
		return;
	}

	if (!is_visible_in_any_scene()) {
		obs_log(LOG_INFO, "display: Clipboard Image is not shown in any scene");
		auto_hide_on_image_hidden();
		return;
	}

	if (shown) {
		obs_log(LOG_INFO, "display: Clipboard Image was shown by the user");
		auto_hide_restart();
	}
}

/**
 * @brief UI スレッドで handle_visibility_changed() を呼ぶ。
 *
 * @param param 通知を回したときの watch_generation と shown をまとめた値(最下位ビットが shown)。
 */
void visibility_changed_task(void *param)
{
	const auto value = reinterpret_cast<uintptr_t>(param);
	if ((value >> 1) != (watch_generation.load() & (UINTPTR_MAX >> 1)))
		return; /* 片付ける前に届いた古い通知 */
	handle_visibility_changed((value & 1) != 0);
}

/**
 * @brief 通知を UI スレッドに回す。
 *
 * @details
 * 通知は UI スレッド以外(ホットキーのスレッド、ソースを削除したときの描画スレッド)からも届く。
 * 描画スレッドではシーンをロックしたまま届くので、ここではシーンに触らず、処理を UI スレッドに回すだけにする。
 * アイテムへの参照は渡さない(削除される途中のアイテムを生き残らせないため。処理は状態を見て判断する)。
 *
 * @param shown 配信者が表示した通知なら true。
 */
void queue_visibility_changed(bool shown)
{
	const uintptr_t value = (watch_generation.load() << 1) | (shown ? 1 : 0);
	obs_queue_task(OBS_TASK_UI, visibility_changed_task, reinterpret_cast<void *>(value), false);
}

/// @brief 削除の通知を UI スレッドに回すときに渡す内容。
struct RemovedNotice {
	uintptr_t generation; ///< 通知を回したときの watch_generation。
	std::string name;     ///< 削除されたアイテムのソースの名前(通知のときのアクティブの名前)。
};

/**
 * @brief UI スレッドで、削除の通知を handle_visibility_changed() に渡す。
 *
 * @details
 * アクティブの画像ソースそのものを削除したかは、通知のときのアクティブと今のアクティブを比べて判断する。
 * OBS がソースを片付け終わるのは削除の通知より後で、この処理がその前に動くか後に動くかは決まっていない。
 * - 片付けの前: アクティブは変わっていないが、どのシーンにも置かれていないので「非表示」になる
 * - 片付けの後: アクティブが一つ前の画像ソースに変わっている
 * どちらでも同じ動き(タイマーを止める)になるよう、後者も削除として扱う。
 *
 * @param param RemovedNotice(new で作ったもの。ここで解放する)。
 */
void item_removed_task(void *param)
{
	std::unique_ptr<RemovedNotice> notice(static_cast<RemovedNotice *>(param));
	if (notice->generation != watch_generation.load())
		return; /* 片付ける前に届いた古い通知 */
	handle_visibility_changed(false, active_source_name() != notice->name);
}

/**
 * @brief 通知の対象が Clipboard Image のシーンアイテムなら、そのアイテムを返す。
 *
 * @param cd 通知の内容("item": シーンアイテム)。
 * @return Clipboard Image のシーンアイテム(参照カウントは増やさない)。
 * @retval nullptr 別のソースのアイテム。
 */
obs_sceneitem_t *clipboard_item_from(calldata_t *cd)
{
	auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
	if (!item || !is_managed_source(obs_sceneitem_get_source(item)))
		return nullptr;
	return item;
}

/**
 * @brief OBS の item_visible 通知を受け取る。Clipboard Image の表示/非表示を UI スレッドに回す。
 *
 * @details
 * 目のアイコン・OBS 標準のホットキー・元に戻す・プラグイン自身、どの方法で切り替えても届く。
 * Clipboard Image のものだけを扱い(is_managed_source())、それ以外のソースの通知は何もしない。
 * プラグイン自身の操作(self_changing が true)の通知は捨てる。
 *
 * @param cd 通知の内容("item": シーンアイテム、"visible": 表示なら true)。
 *
 * @note 呼び出し元: OBS(watch_scene() で登録した通知)。
 * @note スレッド: 決まっていない(表示/非表示を切り替えた操作のスレッド)。
 */
void on_item_visible(void *, calldata_t *cd)
{
	if (self_changing || !clipboard_item_from(cd))
		return;
	queue_visibility_changed(calldata_bool(cd, "visible"));
}

/**
 * @brief OBS の item_add 通知を受け取る。配信者が Clipboard Image をシーンに追加したとき用。
 *
 * @details
 * 「既存のソースを追加」などで追加したアイテムは、表示状態で置かれ、item_visible 通知は来ない。
 * 表示状態で追加されたら、手動で表示したときと同じくカウントを始める(出しっぱなしにしないため)。
 * プラグイン自身の追加(非表示で追加する)は捨てる。
 *
 * @param cd 通知の内容("item": 追加されたシーンアイテム)。
 *
 * @note 呼び出し元: OBS(watch_scene() で登録した通知)。
 * @note スレッド: 決まっていない(追加した操作のスレッド)。
 */
void on_item_add(void *, calldata_t *cd)
{
	if (self_changing)
		return;
	obs_sceneitem_t *item = clipboard_item_from(cd);
	if (!item)
		return;
	queue_visibility_changed(obs_sceneitem_visible(item));
}

/**
 * @brief OBS の item_remove 通知を受け取る。Clipboard Image がシーンから削除されたとき用。
 *
 * @details
 * アイテムやソース・シーンを削除したときは、item_visible 通知は来ず、この通知だけが来る。
 * 削除で画面から消えたら、非表示と同じく一時停止を解除する(一時停止が残って、次の画像が消えなくなるのを防ぐ)。
 * ソースを削除したときは描画スレッドからシーンをロックしたまま届くので、UI スレッドに回すだけにする。
 * アクティブの画像ソースそのものを削除したかを UI スレッドで判断できるよう、ソースの名前を一緒に渡す(item_removed_task())。
 *
 * @param cd 通知の内容("item": 削除されたシーンアイテム)。
 *
 * @note 呼び出し元: OBS(watch_scene() で登録した通知)。
 * @note スレッド: 決まっていない(UI スレッド、またはソース削除時の描画スレッド)。
 */
void on_item_remove(void *, calldata_t *cd)
{
	obs_sceneitem_t *item = clipboard_item_from(cd);
	if (!item)
		return;
	auto *notice = new RemovedNotice{watch_generation.load(), obs_source_get_name(obs_sceneitem_get_source(item))};
	obs_queue_task(OBS_TASK_UI, item_removed_task, notice, false);
}

/// @brief watch_scene() で登録する通知と、その受け取り関数の組み合わせ。
struct SceneSignal {
	const char *name;
	signal_callback_t callback;
};

/// @brief シーンごとに登録する通知の一覧。
constexpr SceneSignal SCENE_SIGNALS[] = {
	{"item_visible", on_item_visible},
	{"item_add", on_item_add},
	{"item_remove", on_item_remove},
};

/**
 * @brief シーンの通知(表示/非表示・追加・削除)を受け取るよう登録する。登録済みなら何もしない。
 *
 * @param scene_source 対象のシーン(ソースとして)。
 */
void watch_scene(obs_source_t *scene_source)
{
	for (obs_weak_source_t *weak : watched_scenes) {
		if (obs_weak_source_references_source(weak, scene_source))
			return;
	}

	signal_handler_t *handler = obs_source_get_signal_handler(scene_source);
	for (const SceneSignal &signal : SCENE_SIGNALS)
		signal_handler_connect(handler, signal.name, signal.callback, nullptr);
	watched_scenes.push_back(obs_source_get_weak_source(scene_source));
	obs_log(LOG_INFO, "display: watching scene \"%s\"", obs_source_get_name(scene_source));
}

/**
 * @brief 登録した通知をすべて解除する。
 *
 * @details 既に削除されたシーンは、弱い参照から取り出せないので解除不要(通知の仕組みごと消えている)。
 */
void unwatch_all_scenes()
{
	for (obs_weak_source_t *weak : watched_scenes) {
		obs_source_t *scene_source = obs_weak_source_get_source(weak);
		if (scene_source) {
			signal_handler_t *handler = obs_source_get_signal_handler(scene_source);
			for (const SceneSignal &signal : SCENE_SIGNALS)
				signal_handler_disconnect(handler, signal.name, signal.callback, nullptr);
			obs_source_release(scene_source);
		}
		obs_weak_source_release(weak);
	}
	watched_scenes.clear();
}

/**
 * @brief 削除されたシーンの弱い参照を一覧から取り除く(シーンを何度も作り直しても一覧が増え続けないように)。
 */
void prune_watched_scenes()
{
	auto expired = [](obs_weak_source_t *weak) {
		if (!obs_weak_source_expired(weak))
			return false;
		obs_weak_source_release(weak);
		return true;
	};
	watched_scenes.erase(std::remove_if(watched_scenes.begin(), watched_scenes.end(), expired),
			     watched_scenes.end());
}

/**
 * @brief プラグインの画像ソース(無印と番号付き)をすべて集める。削除の途中のものは除く。
 *
 * @details obs_enum_sources() はソースの一覧に鍵をかけたまま呼ぶので、その中では参照を取るだけにする
 *          (削除などの操作は、集め終わってから行う)。
 *
 * @return 画像ソース(参照カウント付き。使い終わったら obs_source_release() で解放する)。番号の大きい順。
 */
std::vector<obs_source_t *> collect_managed_sources()
{
	std::vector<obs_source_t *> sources;
	obs_enum_sources(
		[](void *param, obs_source_t *candidate) {
			if (is_image_source(candidate) && !obs_source_removed(candidate) &&
			    managed_number(obs_source_get_name(candidate)) > 0) {
				obs_source_t *ref = obs_source_get_ref(candidate);
				if (ref)
					static_cast<std::vector<obs_source_t *> *>(param)->push_back(ref);
			}
			return true;
		},
		&sources);
	std::sort(sources.begin(), sources.end(), [](obs_source_t *a, obs_source_t *b) {
		return managed_number(obs_source_get_name(a)) > managed_number(obs_source_get_name(b));
	});
	return sources;
}

} // namespace

/**
 * @brief 画像ソースを増やしたときに、前のアクティブの自動非表示を最後まで行うための記録。
 *
 * @details
 * 前のアクティブは、増やした時点でアクティブではなくなり、プラグインは触らなくなる。
 * ただしカウント中に増やしたときだけは、残り時間がたったら一度だけ非表示にしてから手放す。
 * 次々に画像を出したとき、古い画像から順に消えていくようにするため(残したいときは、一時停止してから増やす)。
 * 一時停止中に増やしたときは、記録しない(前の画像ソースはそのまま残る)。
 */
struct RetiringSource {
	obs_weak_source_t *source; ///< 前のアクティブの画像ソース(弱い参照。配信者が削除したら消える)。
	QTimer *timer;             ///< 残り時間を数えるタイマー。
};

/// @brief 自動非表示を待っている、前のアクティブの画像ソースの一覧。
std::vector<RetiringSource> retiring_sources;

/**
 * @brief 前のアクティブの画像ソースを、全シーンで非表示にして手放す(retire_source() のタイマーが切れたとき)。
 *
 * @details
 * 非表示にするのは表示中のアイテムだけ(フェードあり)。別キャンバスと、スタジオモードのプログラムのコピーも含める
 * (アクティブを非表示にするときと同じ範囲)。配信者がすでに非表示にしていた・削除していたときは、何もしない。
 *
 * @param timer 切れたタイマー。
 */
void finish_retiring(QTimer *timer)
{
	auto it = std::find_if(retiring_sources.begin(), retiring_sources.end(),
			       [timer](const RetiringSource &entry) { return entry.timer == timer; });
	if (it == retiring_sources.end())
		return;

	obs_source_t *source = obs_weak_source_get_source(it->source);
	if (source && !obs_source_removed(source)) {
		int hidden = 0;
		for_each_item_in_all_scenes(source, [&hidden](obs_sceneitem_t *item) {
			if (hide_item(item, true))
				hidden++;
			return true;
		});
		fade_out_on_other_canvases(source);
		if (in_studio_mode()) {
			obs_source_t *copy = get_program_copy();
			if (copy) {
				std::vector<obs_sceneitem_t *> items =
					collect_items(obs_scene_from_source(copy), source);
				for (obs_sceneitem_t *item : items)
					hide_item(item, true);
				release_items(items);
				obs_source_release(copy);
			}
		}
		obs_log(LOG_INFO, "display: previous source \"%s\" hidden by its remaining time (%d item(s))",
			obs_source_get_name(source), hidden);
	}
	obs_source_release(source);

	obs_weak_source_release(it->source);
	it->timer->deleteLater();
	retiring_sources.erase(it);
}

/**
 * @brief 前のアクティブの画像ソースを、残り時間がたったら一度だけ非表示にするよう記録する。
 *
 * @param source       前のアクティブの画像ソース。
 * @param remaining_ms 自動で非表示になるまでの残り時間(ミリ秒)。
 */
void retire_source(obs_source_t *source, int remaining_ms)
{
	auto *timer = new QTimer();
	timer->setSingleShot(true);
	QObject::connect(timer, &QTimer::timeout, [timer]() { finish_retiring(timer); });
	retiring_sources.push_back({obs_source_get_weak_source(source), timer});
	timer->start(remaining_ms);
	obs_log(LOG_INFO, "display: previous source \"%s\" will be hidden in %d ms", obs_source_get_name(source),
		remaining_ms);
}

/// @brief 自動非表示を待っている前のアクティブを、すべて忘れる(非表示にはしない)。片付けのときに呼ぶ。
void clear_retiring_sources()
{
	for (RetiringSource &entry : retiring_sources) {
		delete entry.timer;
		obs_weak_source_release(entry.source);
	}
	retiring_sources.clear();
}

/**
 * @brief 画像ファイルを、現在のシーンに表示する。
 *
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 *
 * @note 呼び出し元: plugin-main.cpp の show_saved_image()。
 * @note スレッド: UI スレッド。
 */
bool show_image_on_current_scene(const SavedImage &image, bool new_source)
{
	/*
	 * 配信に出ているシーン(スタジオモードではプログラム側)を取得する。
	 * スタジオモードで「シーンを複製」がオンのときは、これはプログラムに映っているコピーではなく元のシーン。
	 * コピーにも表示するかは設定で決める(show_on_program_copy())
	 */
	obs_source_t *scene_source = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene) {
		obs_log(LOG_WARNING, "display: no current scene");
		notice_report(Notice::DisplayFailed);
		obs_source_release(scene_source);
		return false;
	}

	/*
	 * 新しい画像ソースを作るときは、それまでのアクティブ(prev)の設定を引き継ぐ。
	 * 同じシーンで続けて別の画像を出すのに、画像ソースを増やすたびに見た目が変わらないようにするため。
	 * プロパティとフィルタはソースが持つのでいつも写す。位置などはシーンアイテムが持つので、今のシーンに置かれているときだけ写す
	 */
	obs_source_t *prev = new_source ? find_managed_source() : nullptr;
	obs_sceneitem_t *prev_item = prev ? find_scene_item(scene, prev) : nullptr;
	/* 前のアクティブがカウント中か。アクティブが変わる前(新しい画像ソースを作る前)に問い合わせる */
	const AutoHideStatus prev_status = prev ? auto_hide_status() : AutoHideStatus();

	obs_source_t *source = get_or_create_image_source(image.path.c_str(), new_source, prev);
	/* 前のアクティブがカウント中なら、残り時間がたったら一度だけ非表示にする(一時停止中なら、そのまま残す) */
	if (source && prev && source != prev && prev_status.state == AutoHideState::Counting)
		retire_source(prev, prev_status.remaining_ms);
	obs_source_release(prev);
	if (!source) {
		obs_sceneitem_release(prev_item);
		obs_source_release(scene_source);
		return false;
	}

	/* 現在のシーンに専用ソースが置かれているか探す(グループの中も含む)。なければ追加して画面中央に置く */
	obs_sceneitem_t *item = find_scene_item(scene, source);
	if (item) {
		/*
		 * 既にある: 配信者が動かした位置を尊重し、位置は変えない。
		 * グループの中にあるかをログに残す(不具合調査用)。戻り値は参照カウントを持たないので解放しない
		 */
		obs_sceneitem_t *group = obs_sceneitem_get_group(scene, item);
		if (group)
			obs_log(LOG_INFO, "display: reusing item in group \"%s\" of scene \"%s\"",
				obs_source_get_name(obs_sceneitem_get_source(group)),
				obs_source_get_name(scene_source));
		else
			obs_log(LOG_INFO, "display: reusing item in scene \"%s\"", obs_source_get_name(scene_source));
	} else {
		/* 非表示で追加し、後でフェードインさせる(初回だけいきなり出るのを防ぐ) */
		item = add_hidden_scene_item(scene, source);
		if (!item) {
			obs_log(LOG_WARNING, "display: failed to add source to scene \"%s\"",
				obs_source_get_name(scene_source));
			notice_report(Notice::DisplayFailed);
			obs_sceneitem_release(prev_item);
			obs_source_release(source);
			obs_source_release(scene_source);
			return false;
		}
		/*
		 * 初めて追加したときだけの設定(以降は設定し直さない)。
		 * - 前の画像ソースが今のシーンにある: その見た目(回転・切り抜き・境界・トランジションなど)を写し、
		 *   位置だけは、初めて追加したときと同じく画面中央にする
		 * - ない: 画面中央に置き、表示・非表示のフェードを設定する
		 */
		if (prev_item) {
			copy_item_layout(item, prev_item);
			apply_default_position(item, false);
			obs_log(LOG_INFO, "display: copied layout from the previous source");
		} else {
			apply_default_position(item);
			set_initial_fade_transitions(item);
		}
		obs_log(LOG_INFO, "display: added to scene \"%s\"", obs_source_get_name(scene_source));
	}
	obs_sceneitem_release(prev_item);

	/*
	 * 通知の受け付けは起動完了時などに全シーン分まとめて登録している。念のため、表示するたびに登録し直す
	 * (別キャンバスのシーンは、OBS の画面のシーン一覧の変更の通知が届かないので、後から作られたシーンをここで拾う)
	 */
	image_display_watch_scenes();

	/*
	 * 後から追加されたソースに隠れないよう、最前面に移す(「常に最前面に表示する」がオンのとき。既定オン)。
	 * OBS では後から追加したソースが一番上に置かれるため、初回だけ最前面にしても、配信者が後で
	 * テロップや枠を追加するとその下に隠れてしまう。オフなら重なり順を一切変えない。
	 */
	if (settings_get().always_on_top)
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);

	/* 別のシーンで差し替えを待っている途中なら取り消す(そちらはフェードアウト済み。両方に出さないように) */
	if (pending_swap.item && pending_swap.item != item)
		cancel_swap();

	if (obs_sceneitem_visible(item) || pending_swap.item == item) {
		/* 表示中(または差し替え中): フェードアウト → 差し替え → フェードイン */
		start_swap(item, scene_source, image);
	} else {
		/* 非表示: 画像を設定してフェードイン */
		show_new_image(item, image);
		show_on_other_canvases(source, image);
		if (settings_get().studio_mode_show_on_program && in_studio_mode())
			show_on_program_copy(item, image);
	}

	obs_sceneitem_release(item);
	obs_source_release(source);
	obs_source_release(scene_source);
	return true;
}

/**
 * @brief 全シーンの Clipboard Image を非表示にする。
 *
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 *
 * @note 呼び出し元: auto-hide.cpp の on_hide_timer()(フェードあり)、plugin-main.cpp の on_hide_requested()(フェードあり)、
 *       plugin-main.cpp の on_frontend_event()(起動時などにフェードなし)、clear_clipboard_image()(フェードなし)。
 * @note スレッド: UI スレッド。
 */
void hide_clipboard_image(bool fade)
{
	/* 差し替えの途中なら取り消す(フェードアウト後に勝手に表示し直さないように) */
	cancel_swap();

	obs_source_t *source = find_managed_source();
	if (!source) {
		obs_log(LOG_INFO, "display: nothing to hide (no \"%s\")", active_source_name().c_str());
		return;
	}

	/* 全シーンの Clipboard Image のうち、表示中のものだけ非表示にする。fade なら非表示トランジションでフェードアウトする */
	int hidden = 0;
	for_each_item_in_all_scenes(source, [fade, &hidden](obs_sceneitem_t *item) {
		if (hide_item(item, fade))
			hidden++;
		return true;
	});
	obs_source_release(source);

	/* スタジオモードのプログラムのコピーも非表示にする(設定に関係なく。トランジションでコピーに写った画像を残さないため) */
	if (in_studio_mode()) {
		const int hidden_on_program = hide_on_program_copy(fade);
		if (hidden_on_program > 0)
			obs_log(LOG_INFO, "display: hidden %d item(s) on the program copy", hidden_on_program);
	}

	obs_log(LOG_INFO, "display: hidden %d item(s) (%s)", hidden, fade ? "fade" : "instant");
}

/**
 * @brief 全シーンの通知(表示/非表示・追加・削除)を受け取るよう登録する。
 *
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 *
 * @note 呼び出し元: plugin-main.cpp の on_frontend_event()(起動完了時・シーンコレクション切り替え時・シーン一覧の変更時)。
 * @note スレッド: UI スレッド。
 */
void image_display_watch_scenes()
{
	prune_watched_scenes();

	for_each_scene([](obs_source_t *scene_source) {
		watch_scene(scene_source);
		return true;
	});
}

/**
 * @brief 表示処理で使っているタイマーなどを片付ける。
 *
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 */
void image_display_shutdown()
{
	/* 片付ける前に UI スレッドへ回した通知を、片付けた後に処理しないようにする */
	watch_generation++;
	unwatch_all_scenes();
	cancel_swap();
	delete swap_timer;
	swap_timer = nullptr;
	clear_retiring_sources();
}

/**
 * @brief Clipboard Image がいずれかのシーンで表示中かどうかを返す。
 *
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 */
bool is_clipboard_image_showing()
{
	if (pending_swap.item)
		return true; /* 差し替え中: いったん非表示にしているが、すぐまた表示する */
	return is_visible_in_any_scene();
}

/**
 * @brief 'Clipboard Image' が今使っている画像ファイルのパスを返す。
 *
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 */
std::vector<std::string> clipboard_images_in_use()
{
	std::vector<std::string> paths;

	obs_source_t *source = find_managed_source();
	if (source) {
		const std::string file = image_file_of(source);
		if (!file.empty())
			paths.push_back(file);
		obs_source_release(source);
	}

	if (pending_swap.item && !pending_swap.image.path.empty())
		paths.push_back(pending_swap.image.path);

	/*
	 * 非アクティブの画像ソース(番号が一番大きくないもの)も、表示したまま残していることがある。
	 * プラグインは触らないが、参照している画像を掃除で消すと画面から消えるので、使用中に含めて残す。
	 * obs_enum_sources() はソースの一覧をロックしたまま呼ぶので、その中では参照を取るだけにする
	 */
	std::vector<obs_source_t *> managed;
	obs_enum_sources(
		[](void *param, obs_source_t *candidate) {
			if (is_image_source(candidate) && managed_number(obs_source_get_name(candidate)) > 0)
				static_cast<std::vector<obs_source_t *> *>(param)->push_back(
					obs_source_get_ref(candidate));
			return true;
		},
		&managed);
	for (obs_source_t *candidate : managed) {
		const std::string file = candidate ? image_file_of(candidate) : std::string();
		if (!file.empty() && std::find(paths.cbegin(), paths.cend(), file) == paths.cend())
			paths.push_back(file);
		obs_source_release(candidate);
	}

	/*
	 * スタジオモードで「ソースを複製」がオンだと、プログラムのコピーの中の Clipboard Image は別のソースになり、
	 * 元とは別の画像ファイルを参照していることがある(次のトランジションまで、前の画像を配信に映している)。
	 * そのファイルを消すと、OBS の画像ソースが読み直して、配信に出ている画像が消える。使用中に含めて残す
	 */
	std::vector<obs_sceneitem_t *> copy_items = collect_program_copy_items();
	for (obs_sceneitem_t *item : copy_items) {
		const std::string file = image_file_of(obs_sceneitem_get_source(item));
		if (!file.empty() && std::find(paths.cbegin(), paths.cend(), file) == paths.cend())
			paths.push_back(file);
	}
	release_items(copy_items);

	return paths;
}

/**
 * @brief フェードでの差し替えを待っている途中かどうかを返す。
 *
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 */
bool is_clipboard_image_swap_pending()
{
	return pending_swap.item != nullptr;
}

/**
 * @brief 今の画像('Clipboard Image' が参照している画像ファイル)のパスを返す。
 *
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 */
std::string current_clipboard_image()
{
	/* 差し替えの途中なら、この後に出る画像が「今の画像」 */
	if (pending_swap.item && !pending_swap.image.path.empty())
		return pending_swap.image.path;

	std::string path;
	obs_source_t *source = find_managed_source();
	if (source) {
		path = image_file_of(source);
		obs_source_release(source);
	}
	return path;
}

/**
 * @brief 今の画像を外す。全シーンの 'Clipboard Image' を即座に非表示にし、画像ファイルの参照を空にする。
 *
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 */
void clear_clipboard_image()
{
	/* 差し替えの途中なら取り消し、フェードアウトせずに即座に非表示にする(見せたくない画像を一瞬も残さないため) */
	hide_clipboard_image(false);

	obs_source_t *source = find_managed_source();
	if (!source)
		return;
	set_image_file(source, "");

	/*
	 * 「ソースを複製」がオンだと、プログラムのコピーの画像ソースは別物なので、そちらの参照も外す。
	 * コピーの画像ソースに画像を設定するのは、プログラムにもすぐ表示する設定のときだけ(show_on_program_copy())。
	 * 設定がオフなら、コピーの画像は次のトランジションで元のシーン(参照を外した状態)から作り直されるので、ここでは扱わない
	 */
	if (settings_get().studio_mode_show_on_program && in_studio_mode()) {
		std::vector<obs_sceneitem_t *> items = collect_program_copy_items();
		for (obs_sceneitem_t *item : items) {
			obs_source_t *copy_source = obs_sceneitem_get_source(item);
			if (copy_source != source)
				set_image_file(copy_source, "");
		}
		release_items(items);
	}
	obs_log(LOG_INFO, "display: cleared the image of \"%s\"", obs_source_get_name(source));
	obs_source_release(source);
}

/**
 * @brief プラグインの画像ソースを一覧にする。
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 */
std::vector<ClipboardSourceInfo> list_clipboard_sources()
{
	/* シーンは、メインのキャンバス(OBS のシーン一覧の順)、別キャンバスの順に集める。キャンバスの名前も覚えておく */
	struct SceneEntry {
		obs_source_t *scene;
		std::string canvas;
	};
	std::vector<SceneEntry> scenes;
	obs_frontend_source_list main_scenes = {};
	obs_frontend_get_scenes(&main_scenes);
	for (size_t i = 0; i < main_scenes.sources.num; i++)
		scenes.push_back({obs_source_get_ref(main_scenes.sources.array[i]), std::string()});
	obs_frontend_source_list_free(&main_scenes);

	std::vector<obs_canvas_t *> canvases = collect_other_canvases();
	for (obs_canvas_t *canvas : canvases) {
		const char *canvas_name = obs_canvas_get_name(canvas);
		std::vector<obs_source_t *> canvas_scenes;
		obs_canvas_enum_scenes(
			canvas,
			[](void *param, obs_source_t *scene_source) {
				static_cast<std::vector<obs_source_t *> *>(param)->push_back(
					obs_source_get_ref(scene_source));
				return true;
			},
			&canvas_scenes);
		for (obs_source_t *scene_source : canvas_scenes)
			scenes.push_back({scene_source, canvas_name ? canvas_name : ""});
	}
	release_canvases(canvases);

	std::vector<ClipboardSourceInfo> list;
	std::vector<obs_source_t *> sources = collect_managed_sources();
	for (obs_source_t *source : sources) {
		ClipboardSourceInfo info;
		info.name = obs_source_get_name(source);
		info.image_path = image_file_of(source);
		for (const SceneEntry &entry : scenes) {
			obs_scene_t *scene = entry.scene ? obs_scene_from_source(entry.scene) : nullptr;
			if (!scene)
				continue;
			std::vector<obs_sceneitem_t *> items = collect_items(scene, source);
			if (!items.empty()) {
				info.scenes.push_back(obs_source_get_name(entry.scene));
				info.canvases.push_back(entry.canvas);
			}
			release_items(items);
		}
		list.push_back(std::move(info));
		obs_source_release(source);
	}

	for (SceneEntry &entry : scenes)
		obs_source_release(entry.scene);
	return list;
}

/**
 * @brief プラグインの画像ソースを 1 つ、すべてのシーンから削除する。
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 */
bool remove_clipboard_source(const std::string &name)
{
	obs_source_t *source = obs_get_source_by_name(name.c_str());
	const bool managed = source && is_image_source(source) && !obs_source_removed(source) &&
			     managed_number(obs_source_get_name(source)) > 0;
	if (!managed) {
		obs_log(LOG_INFO, "display: source \"%s\" to remove was not found", name.c_str());
		obs_source_release(source);
		return false;
	}

	/* ソースを削除すると、置かれているすべてのシーン(グループの中・別キャンバスを含む)から一度に消える */
	obs_source_remove(source);
	obs_log(LOG_INFO, "display: removed source \"%s\" from all scenes", name.c_str());
	obs_source_release(source);
	return true;
}

/**
 * @brief プラグインの画像ソースを、すべてのシーンからすべて削除する。
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 */
int remove_all_clipboard_sources()
{
	/* 一覧を調べ終わってから削除する(調べている間は、ソースの一覧に鍵がかかっているため) */
	std::vector<obs_source_t *> sources = collect_managed_sources();
	int removed = 0;
	for (obs_source_t *source : sources) {
		if (!obs_source_removed(source)) {
			obs_source_remove(source);
			removed++;
		}
		obs_source_release(source);
	}
	obs_log(LOG_INFO, "display: removed %d source(s) from all scenes", removed);
	return removed;
}
