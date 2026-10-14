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
 * - 代わりに貼るたびに画像の幅・高さを見て拡大率を設定し、大きい画像を
 *   画面の幅・高さの MAX_SIZE_RATIO(60%)に収める。小さい画像は等倍(拡大しない)。
 * - 配信者が自分で境界を設定している場合は、その設定に任せて拡大率は変えない。
 *
 * 公開している関数は show_image_on_current_scene() だけ(image-display.hpp)。
 */

#include "image-display.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <graphics/vec2.h>

#include <algorithm>
#include <cstring>

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
 * @brief 画像の最大の大きさ(画面の幅・高さに対する割合)。これを超える画像だけ縮小する。
 * @note MVP では固定。#8 の設定画面で変更できるようにする。
 */
constexpr float MAX_SIZE_RATIO = 0.6f;

/**
 * @brief 専用の画像ソースを取得する。なければ作る。表示する画像ファイルも設定する。
 *
 * @param path 表示する画像ファイルのパス(UTF-8)。
 * @return 画像ソース(参照カウント付き。使い終わったら obs_source_release() で解放する)。
 * @retval nullptr 作成に失敗した、または同じ名前の別種類のソースがある。
 */
obs_source_t *get_or_create_image_source(const char *path)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", path);

	obs_source_t *source = obs_get_source_by_name(SOURCE_NAME);
	if (source) {
		/*
		 * 同じ名前のソースを配信者が自分で作っている可能性がある(テキストソースなど)。
		 * 種類が違うものを書き換えると配信者の設定を壊すので、触らずに失敗扱いにする。
		 */
		if (std::strcmp(obs_source_get_unversioned_id(source), IMAGE_SOURCE_ID) != 0) {
			obs_log(LOG_WARNING, "display: a source named \"%s\" exists but is not an image source (%s)",
				SOURCE_NAME, obs_source_get_unversioned_id(source));
			obs_source_release(source);
			obs_data_release(settings);
			return nullptr;
		}

		/* 既存のソース: 表示する画像ファイルだけ差し替える(前の画像は消える) */
		obs_source_update(source, settings);
		obs_log(LOG_INFO, "display: updated source \"%s\"", SOURCE_NAME);
	} else {
		source = obs_source_create(IMAGE_SOURCE_ID, SOURCE_NAME, settings, nullptr);
		if (source)
			obs_log(LOG_INFO, "display: created source \"%s\"", SOURCE_NAME);
		else
			obs_log(LOG_WARNING, "display: failed to create source \"%s\"", SOURCE_NAME);
	}

	obs_data_release(settings);
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
 * 後で拡大率が変わっても画像は画面中央を中心に大きくなったり小さくなったりする。
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
 * @brief 画像の大きさに合わせて、シーンアイテムの拡大率を決める。
 *
 * @details
 * 画像が画面の幅・高さの MAX_SIZE_RATIO を超える場合だけ、収まるように縮小する。
 * 縦横比は保つ(幅と高さで同じ拡大率を使う)。小さい画像は等倍のまま拡大しない。
 *
 * 例: 画面 1920x1080、割合 60% → 最大 1152x648
 * - 3840x2160 の画像 → 0.3 倍(1152x648)
 * - 2000x500 の画像  → 0.576 倍(幅で決まる)
 * - 400x300 の画像   → 1 倍(そのまま)
 *
 * 配信者が境界(自動以外)を設定している場合は、境界が大きさを決めるので何もしない。
 *
 * @param item  対象のシーンアイテム。
 * @param image 表示する画像の情報(幅・高さを使う)。
 */
void apply_fit_scale(obs_sceneitem_t *item, const SavedImage &image)
{
	if (obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE) {
		obs_log(LOG_INFO, "display: bounds set by user; scale left as is");
		return;
	}
	if (image.width <= 0 || image.height <= 0) {
		obs_log(LOG_WARNING, "display: unknown image size; scale left as is");
		return;
	}

	float canvas_w = 0.0f;
	float canvas_h = 0.0f;
	if (!get_canvas_size(canvas_w, canvas_h))
		return;

	const float max_w = canvas_w * MAX_SIZE_RATIO;
	const float max_h = canvas_h * MAX_SIZE_RATIO;
	const float fit = std::min(max_w / static_cast<float>(image.width), max_h / static_cast<float>(image.height));
	const float factor = std::min(1.0f, fit);

	vec2 scale;
	vec2_set(&scale, factor, factor);
	obs_sceneitem_set_scale(item, &scale);

	obs_log(LOG_INFO, "display: scale %.3f for %dx%d (max %.0fx%.0f)", factor, image.width, image.height, max_w,
		max_h);
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
		item = obs_scene_add(scene, source);
		if (!item) {
			obs_log(LOG_WARNING, "display: failed to add source to scene \"%s\"",
				obs_source_get_name(scene_source));
			obs_source_release(source);
			obs_source_release(scene_source);
			return false;
		}
		/* obs_scene_add() の戻り値は参照カウントを持たないので、後で解放できるよう揃える */
		obs_sceneitem_addref(item);
		apply_default_position(item);
		obs_log(LOG_INFO, "display: added to scene \"%s\"", obs_source_get_name(scene_source));
	}

	/* 画像ごとに大きさが違うので、貼るたびに拡大率を決め直す */
	apply_fit_scale(item, image);

	/* 他のソースに隠れないよう最前面に移し、表示状態にする */
	obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	obs_sceneitem_set_visible(item, true);
	obs_log(LOG_INFO, "display: showing %s", image.path.c_str());

	obs_sceneitem_release(item);
	obs_source_release(source);
	obs_source_release(scene_source);
	return true;
}
