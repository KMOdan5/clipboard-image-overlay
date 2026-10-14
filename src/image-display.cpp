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
 * @file image-display.cpp
 * @brief 保存した画像を、プラグイン専用の画像ソースとして現在のシーンに表示する。
 *
 * @details
 * 使う OBS の部品:
 * - 画像ソース(内部 ID: image_source)… 画像ファイルを表示する OBS 標準のソース。GIF のアニメーションも再生される
 * - シーンアイテム … シーンに置かれたソース。位置・大きさ・表示/非表示はここで持つ
 *
 * 専用ソースは名前「Clipboard Image」で 1 つだけ作り、毎回それを使い回す。
 * こうすることで、新しい画像を貼ると前の画像と差し替わり、画面に画像が溜まらない。
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
 * - 重なり順(「常に手前に表示する」がオンのとき、貼るたびに最前面へ。既定オン)
 * - 表示/非表示(表示する・消す)
 * 位置・境界・トランジションなど、それ以外の設定は変えない。
 * - 消すときは、現在のシーンだけでなく全シーンの Clipboard Image を非表示にする。
 *   表示中にシーンを切り替えても、元のシーンに画像が出しっぱなしにならないようにするため。
 *
 * 表示/非表示の検知(#7):
 * - 全シーンの item_visible(表示/非表示)・item_add(追加)・item_remove(削除)通知を受け取る。
 *   名前が Clipboard Image のものだけを扱い、他のソースの通知は名前を比べるだけで捨てる。
 * - 通知は UI スレッドに回し、処理する時点の状態を見て判断する(handle_visibility_changed())。
 *   - どのシーンにも表示されていなければ、一時停止を解除してタイマーを止める(auto_hide_on_image_hidden())
 *   - 配信者が表示・追加したなら、貼ったときと同じくカウントを始める(auto_hide_restart())
 *   - 差し替え中(フェードアウト中)は「表示が続いている」扱いにして何もしない
 * - プラグイン自身の追加・表示の通知は捨てる(self_changing)。カウントは plugin-main.cpp で始めているため、
 *   二重に数えないようにする。
 * - 片付け(シーンコレクションの切り替え・終了)より前に回した通知は、片付けた後には処理しない(watch_generation)。
 * - 差し替えを待つ間にアイテム・ソース・シーンが削除されたら、差し替えを取り消す。
 *
 * 公開している関数は show_image_on_current_scene()、hide_clipboard_image()、
 * image_display_watch_scenes()、is_clipboard_image_showing()、image_display_shutdown()(image-display.hpp)。
 */

#include "image-display.hpp"
#include "auto-hide.hpp"
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
 * @brief 専用の画像ソースを取得する。なければ作る。
 *
 * @details
 * 新しく作るときは、表示する画像ファイルを設定した状態で作る。
 * 既存のソースの画像ファイルは、ここでは変えない。表示中の画像をフェードアウトしてから
 * 差し替える必要があるため、差し替えのタイミングは呼び出し元が決める。
 *
 * @param path 新しく作るときに設定する画像ファイルのパス(UTF-8)。
 * @return 画像ソース(参照カウント付き。使い終わったら obs_source_release() で解放する)。
 * @retval nullptr 作成に失敗した、同じ名前の別種類のソースがある、または削除の途中。
 */
obs_source_t *get_or_create_image_source(const char *path)
{
	obs_source_t *source = obs_get_source_by_name(SOURCE_NAME);
	if (source) {
		/*
		 * 同じ名前のソースを配信者が自分で作っている可能性がある(テキストソースなど)。
		 * 種類が違うものを書き換えると配信者の設定を壊すので、触らずに失敗扱いにする。
		 */
		/* 削除の途中(OBS が片付けるまでの一瞬)のソースは使わない。消えるソースに画像を出してしまうため */
		if (obs_source_removed(source)) {
			obs_log(LOG_WARNING, "display: \"%s\" is being removed; try again", SOURCE_NAME);
			obs_source_release(source);
			return nullptr;
		}
		if (std::strcmp(obs_source_get_unversioned_id(source), IMAGE_SOURCE_ID) != 0) {
			obs_log(LOG_WARNING, "display: a source named \"%s\" exists but is not an image source (%s)",
				SOURCE_NAME, obs_source_get_unversioned_id(source));
			obs_source_release(source);
			return nullptr;
		}
		return source;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", path);
	source = obs_source_create(IMAGE_SOURCE_ID, SOURCE_NAME, settings, nullptr);
	obs_data_release(settings);

	if (source) {
		obs_log(LOG_INFO, "display: created source \"%s\"", SOURCE_NAME);
	} else {
		obs_log(LOG_WARNING, "display: failed to create source \"%s\"", SOURCE_NAME);
	}
	return source;
}

/**
 * @brief 画面(キャンバス)の大きさを取得する。
 *
 * @param[out] width  キャンバスの幅(ピクセル)。
 * @param[out] height キャンバスの高さ(ピクセル)。
 * @return 取得できたら true。
 */
bool get_canvas_size(float &width, float &height)
{
	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0) {
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
 * 境界は「自動」(OBS_BOUNDS_NONE)にする(理由はファイル冒頭の説明を参照)。
 *
 * @param item 配置するシーンアイテム。
 */
void apply_default_position(obs_sceneitem_t *item)
{
	float canvas_w = 0.0f;
	float canvas_h = 0.0f;
	if (!get_canvas_size(canvas_w, canvas_h))
		return;

	vec2 pos;
	vec2_set(&pos, canvas_w / 2.0f, canvas_h / 2.0f);

	/* 複数の変更を 1 回の更新としてまとめ、途中の中途半端な状態が画面に出ないようにする */
	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_defer_update_end(item);

	obs_log(LOG_INFO, "display: placed at center (%.0f, %.0f)", pos.x, pos.y);
}

/**
 * @brief 画像の大きさに合わせて、表示サイズ(「変換の編集」の「大きさ」)を決める。
 *
 * @details
 * 画像が画面の幅・高さの「最大サイズの割合」(設定画面で変更。既定 60%)を超える場合だけ、収まるように縮小する。
 * 縦横比は保つ(幅と高さに同じ倍率を使う)。小さい画像は等倍のまま拡大しない。
 * OBS では表示サイズを倍率で設定するため、計算した倍率を obs_sceneitem_set_scale() で設定する。
 *
 * 例: 画面 1920x1080、割合 60% → 最大 1152x648
 * - 3840x2160 の画像 → 0.3 倍(1152x648)
 * - 2000x500 の画像  → 0.576 倍(幅で決まる)
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
 *
 * @param item  対象のシーンアイテム。
 * @param image 表示する画像の情報(幅・高さを使う)。
 */
void apply_display_size(obs_sceneitem_t *item, const SavedImage &image)
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
	} else if (get_canvas_size(canvas_w, canvas_h)) {
		const float ratio = static_cast<float>(settings.max_size_percent) / 100.0f;
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
 * @brief シーンの中から、指定したソースが置かれているシーンアイテムを探す。
 *
 * @details
 * OBS 31 で obs_scene_sceneitem_from_source() が非推奨になったため、
 * シーンアイテムを順に調べる obs_scene_enum_items() で探す。
 *
 * @param scene  探す対象のシーン。
 * @param source 探すソース。
 * @return 見つかったシーンアイテム(参照カウント付き。使い終わったら obs_sceneitem_release() で解放する)。
 * @retval nullptr シーンに置かれていない。
 */
obs_sceneitem_t *find_scene_item(obs_scene_t *scene, obs_source_t *source)
{
	struct search {
		obs_source_t *source;
		obs_sceneitem_t *found;
	} ctx = {source, nullptr};

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *ctx = static_cast<search *>(param);
			if (obs_sceneitem_get_source(item) != ctx->source)
				return true; /* 違うソース: 次を調べる */
			obs_sceneitem_addref(item);
			ctx->found = item;
			return false; /* 見つかった: 探索を終える */
		},
		&ctx);

	return ctx.found;
}

/**
 * @brief シーンの中から、指定したソースが置かれているシーンアイテムをすべて集める。
 *
 * @details
 * 配信者が同じシーンに Clipboard Image を 2 つ以上置いている場合もあるため、
 * 非表示にするときや「どこかに表示されているか」を調べるときは、最初の 1 つだけでなく全部を見る。
 * 集めたあとで操作するのは、obs_scene_enum_items() の途中(シーンをロック中)に表示を切り替えないようにするため。
 *
 * @param scene  探す対象のシーン。
 * @param source 探すソース。
 * @return 見つかったシーンアイテム(それぞれ参照カウント付き。release_items() で解放する)。
 */
std::vector<obs_sceneitem_t *> collect_items(obs_scene_t *scene, obs_source_t *source)
{
	struct search {
		obs_source_t *source;
		std::vector<obs_sceneitem_t *> found;
	} ctx = {source, {}};

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *ctx = static_cast<search *>(param);
			if (obs_sceneitem_get_source(item) == ctx->source) {
				obs_sceneitem_addref(item);
				ctx->found.push_back(item);
			}
			return true; /* 全部集めるので最後まで調べる */
		},
		&ctx);

	return ctx.found;
}

/// @brief collect_items() で集めたシーンアイテムをすべて解放する。
void release_items(std::vector<obs_sceneitem_t *> &items)
{
	for (obs_sceneitem_t *item : items)
		obs_sceneitem_release(item);
	items.clear();
}

/**
 * @brief Clipboard Image が、いずれかのシーンで表示状態になっているかを調べる。
 *
 * @details
 * 自動で消す処理は全シーンを対象にするので、「表示中か」も全シーンで判断する。
 * 現在のシーンだけで判断すると、別のシーンに表示が残っているのにタイマーや一時停止を
 * 片付けてしまい、その画像が消えなくなるため。
 *
 * @return どこかのシーンで表示状態なら true。Clipboard Image がなければ false。
 */
bool is_visible_in_any_scene()
{
	obs_source_t *source = obs_get_source_by_name(SOURCE_NAME);
	if (!source)
		return false;

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	bool visible = false;
	for (size_t i = 0; i < scenes.sources.num && !visible; i++) {
		obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
		if (!scene)
			continue;
		std::vector<obs_sceneitem_t *> items = collect_items(scene, source);
		for (obs_sceneitem_t *item : items)
			visible = visible || obs_sceneitem_visible(item);
		release_items(items);
	}

	obs_frontend_source_list_free(&scenes);
	obs_source_release(source);
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
 * @details 待っている間にアイテムが削除されていたら、差し替えずに取り消す。
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
	pending_swap.item = nullptr;
	obs_weak_source_release(pending_swap.scene);
	pending_swap.scene = nullptr;

	show_new_image(item, image);
	obs_sceneitem_release(item);
}

/**
 * @brief 表示中の画像を、フェードアウト → 差し替え → フェードイン で入れ替える。
 *
 * @details
 * フェードアウトにかかる時間(非表示トランジションの長さ)だけ待ってから差し替える。
 * 待っている間にもう一度貼られた場合は、待ち時間はそのままで、差し替え先だけを最新の画像にする
 * (最後に貼った画像が表示される。途中の画像が一瞬出たりはしない)。
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

	/* 非表示トランジションがあればその長さだけ待つ。なければすぐ消えるので待たない */
	const bool has_fade = obs_sceneitem_get_transition(item, false) != nullptr;
	const int wait_ms =
		has_fade ? static_cast<int>(obs_sceneitem_get_transition_duration(item, false)) + SWAP_MARGIN_MS : 0;

	obs_sceneitem_set_visible(item, false); /* フェードアウト開始 */
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
 * 3. どのシーンにも表示されていなければ、一時停止を解除してタイマーを止める(#7 のルール 2)
 * 4. 配信者が表示した(表示・追加の通知)なら、貼ったときと同じくカウントを始める
 *    (既にどこかに表示中でカウントしていた場合は、7 秒に戻る)
 *
 * @param shown 配信者が表示した通知(表示にした・表示状態で追加した)なら true。
 *              非表示にした・削除した通知なら false。
 */
void handle_visibility_changed(bool shown)
{
	if (pending_swap.item && !is_pending_swap_alive()) {
		obs_log(LOG_INFO, "display: swap target was removed");
		cancel_swap();
	}
	if (pending_swap.item)
		return;

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
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	const char *name = source ? obs_source_get_name(source) : nullptr;
	if (!name || std::strcmp(name, SOURCE_NAME) != 0)
		return nullptr;
	return item;
}

/**
 * @brief OBS の item_visible 通知を受け取る。Clipboard Image の表示/非表示を UI スレッドに回す。
 *
 * @details
 * 目のアイコン・OBS 標準のホットキー・元に戻す・プラグイン自身、どの方法で切り替えても届く。
 * 名前が Clipboard Image のものだけを扱い、それ以外のソースの通知は名前を比べるだけで何もしない。
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
 *
 * @param cd 通知の内容("item": 削除されたシーンアイテム)。
 *
 * @note 呼び出し元: OBS(watch_scene() で登録した通知)。
 * @note スレッド: 決まっていない(UI スレッド、またはソース削除時の描画スレッド)。
 */
void on_item_remove(void *, calldata_t *cd)
{
	if (!clipboard_item_from(cd))
		return;
	queue_visibility_changed(false);
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

} // namespace

/**
 * @brief 画像ファイルを、現在のシーンに表示する。
 *
 * @details 仕様の詳細は image-display.hpp の宣言側を参照。
 *
 * @note 呼び出し元: plugin-main.cpp の on_show_requested()。
 * @note スレッド: UI スレッド。
 */
bool show_image_on_current_scene(const SavedImage &image)
{
	/* 配信に出ているシーン(スタジオモードでは「番組」側)を取得する */
	obs_source_t *scene_source = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene) {
		obs_log(LOG_WARNING, "display: no current scene");
		obs_source_release(scene_source);
		return false;
	}

	obs_source_t *source = get_or_create_image_source(image.path.c_str());
	if (!source) {
		obs_source_release(scene_source);
		return false;
	}

	/* 現在のシーンに専用ソースが置かれているか探す。なければ追加して画面中央に置く */
	obs_sceneitem_t *item = find_scene_item(scene, source);
	if (item) {
		/* 既にある: 配信者が動かした位置を尊重し、位置は変えない */
		obs_log(LOG_INFO, "display: reusing item in scene \"%s\"", obs_source_get_name(scene_source));
	} else {
		/* 非表示で追加し、後でフェードインさせる(初回だけいきなり出るのを防ぐ) */
		item = add_hidden_scene_item(scene, source);
		if (!item) {
			obs_log(LOG_WARNING, "display: failed to add source to scene \"%s\"",
				obs_source_get_name(scene_source));
			obs_source_release(source);
			obs_source_release(scene_source);
			return false;
		}
		/* 初めて追加したときだけの設定: 位置(画面中央)と、表示・非表示のフェード。以降は設定し直さない */
		apply_default_position(item);
		set_initial_fade_transitions(item);
		obs_log(LOG_INFO, "display: added to scene \"%s\"", obs_source_get_name(scene_source));
	}

	/* 通知の受け付けは起動完了時などに全シーン分まとめて登録している。念のため、このシーンも確かめておく */
	watch_scene(scene_source);

	/*
	 * 後から追加されたソースに隠れないよう、最前面に移す(「常に手前に表示する」がオンのとき。既定オン)。
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
 * @note 呼び出し元: auto-hide.cpp の on_hide_timer()(フェードあり)、
 *       plugin-main.cpp の on_frontend_event()(起動時などにフェードなし)。
 * @note スレッド: UI スレッド。
 */
void hide_clipboard_image(bool fade)
{
	/* 差し替えの途中なら取り消す(フェードアウト後に勝手に表示し直さないように) */
	cancel_swap();

	obs_source_t *source = obs_get_source_by_name(SOURCE_NAME);
	if (!source) {
		obs_log(LOG_INFO, "display: nothing to hide (no \"%s\")", SOURCE_NAME);
		return;
	}

	/* シーン一覧を取り、それぞれのシーンに置かれた Clipboard Image を非表示にする */
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	int hidden = 0;
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
		if (!scene)
			continue;

		/* 同じシーンに 2 つ以上置かれていることもあるので、全部を対象にする */
		std::vector<obs_sceneitem_t *> items = collect_items(scene, source);
		for (obs_sceneitem_t *item : items) {
			/* 表示中のものだけ非表示にする。fade なら非表示トランジションでフェードアウトする */
			if (!obs_sceneitem_visible(item))
				continue;
			const bool changed = fade ? obs_sceneitem_set_visible(item, false) : hide_item_instantly(item);
			if (changed)
				hidden++;
		}
		release_items(items);
	}

	obs_frontend_source_list_free(&scenes);
	obs_source_release(source);
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

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++)
		watch_scene(scenes.sources.array[i]);
	obs_frontend_source_list_free(&scenes);
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
