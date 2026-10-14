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
 * 公開している関数は show_image_on_current_scene() だけ(image-display.hpp)。
 */

#include "image-display.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <graphics/vec2.h>

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

/// @brief 画像を収める枠の大きさ(画面の幅・高さに対する割合)。#8 の設定画面で変更できるようにする予定。
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
 * @brief シーンアイテムを既定の位置・大きさに置く。
 *
 * @details
 * 枠(バウンディングボックス)を画面中央に置き、画像をその中に収める。
 * 枠の種類は「最大サイズのみ」(OBS_BOUNDS_MAX_ONLY)にしている。
 * 大きい画像は枠に収まるよう縮小し、小さい画像は拡大せずそのままの大きさで表示するため
 * (小さなアイコンなどを無理に引き伸ばすと、ぼやけて見づらくなる)。
 *
 * @param item 配置するシーンアイテム。
 */
void apply_default_layout(obs_sceneitem_t *item)
{
	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi)) {
		obs_log(LOG_WARNING, "display: cannot get canvas size; default layout skipped");
		return;
	}

	const float canvas_w = static_cast<float>(ovi.base_width);
	const float canvas_h = static_cast<float>(ovi.base_height);

	vec2 pos;
	vec2 bounds;
	vec2 scale;
	vec2_set(&pos, canvas_w / 2.0f, canvas_h / 2.0f);
	vec2_set(&bounds, canvas_w * MAX_SIZE_RATIO, canvas_h * MAX_SIZE_RATIO);
	vec2_set(&scale, 1.0f, 1.0f);

	/* 複数の変更を 1 回の更新としてまとめ、途中の中途半端な状態が画面に出ないようにする */
	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_MAX_ONLY);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_defer_update_end(item);

	obs_log(LOG_INFO, "display: default layout applied (center, max %.0fx%.0f)", bounds.x, bounds.y);
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
bool show_image_on_current_scene(const std::string &path)
{
	/* 配信に出ているシーン(スタジオモードでは「番組」側)を取得する */
	obs_source_t *scene_source = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene) {
		obs_log(LOG_WARNING, "display: no current scene");
		obs_source_release(scene_source);
		return false;
	}

	obs_source_t *source = get_or_create_image_source(path.c_str());
	if (!source) {
		obs_source_release(scene_source);
		return false;
	}

	/* 現在のシーンに専用ソースが置かれているか探す。なければ追加して既定の位置に置く */
	obs_sceneitem_t *item = obs_scene_sceneitem_from_source(scene, source);
	if (item) {
		/* 既にある: 配信者が調整した位置・大きさを尊重し、配置は変えない */
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
		apply_default_layout(item);
		obs_log(LOG_INFO, "display: added to scene \"%s\"", obs_source_get_name(scene_source));
	}

	/* 他のソースに隠れないよう最前面に移し、表示状態にする */
	obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	obs_sceneitem_set_visible(item, true);
	obs_log(LOG_INFO, "display: showing %s", path.c_str());

	obs_sceneitem_release(item);
	obs_source_release(source);
	obs_source_release(scene_source);
	return true;
}
