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
 * @file auto-hide.cpp
 * @brief 表示した画像を一定時間後に自動で消すタイマーと、その一時停止。
 *
 * @details
 * 状態の移り変わり:
 * @code
 *   [非表示] ──貼る/手動で表示──→ [カウント中] ──時間切れ/手動で非表示──→ [非表示]
 *                        │    ↑ 貼る: 残り時間を 7 秒に戻す(延長)
 *       一時停止ホットキー │    │ 一時停止ホットキー(残り時間から再開)
 *                        ↓    │
 *                     [一時停止中] ──手動で非表示──→ [非表示](一時停止は解除)
 *                          ↑ 貼る: 一時停止のまま、残り時間を 7 秒に戻す
 * @endcode
 *
 * タイマーは 1 つだけ持ち、使い回す。表示のたびに新しいタイマーを作ると、
 * 古いタイマーが後から発火して、延長したはずの画像が早く消えてしまうため。
 *
 * 状態はすべてこのファイル内の変数で持ち、保存しない。OBS を終了すると消え、
 * 起動時は「非表示・一時停止なし・タイマーなし」から始まる。
 *
 * 関連ファイル:
 * - image-display.cpp … hide_clipboard_image()(画像を消す)、非表示の検知(auto_hide_on_image_hidden() を呼ぶ)、
 *   手動の表示の検知(auto_hide_restart() を呼ぶ)
 * - plugin-main.cpp … 貼ったときに auto_hide_restart()、一時停止ホットキーで auto_hide_toggle_pause() を呼ぶ
 */

#include "auto-hide.hpp"
#include "image-display.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QTimer>

namespace {

/**
 * @brief 画像を表示してから自動で消すまでの秒数。
 * @note MVP では固定(雑談で見せる想定の 5〜10 秒の間)。#8 の設定画面で変更できるようにする。
 *       0 なら自動で消さない(手動で消すまで表示し続ける)。
 */
constexpr int AUTO_HIDE_SECONDS = 7;

/// @brief 自動で消すためのタイマー。最初に使うときに作り、auto_hide_shutdown() で片付ける。
QTimer *hide_timer = nullptr;

/// @brief 一時停止中なら true。
bool paused = false;

/// @brief 一時停止中に覚えておく、消えるまでの残り時間(ミリ秒)。
int remaining_ms = 0;

/**
 * @brief タイマーの時間が来たときの処理。画像を消す。
 *
 * @note 呼び出し元: Qt(hide_timer の timeout)。
 * @note スレッド: UI スレッド(タイマーを作ったスレッドで届く)。
 */
void on_hide_timer()
{
	obs_log(LOG_INFO, "auto-hide: time is up");
	hide_clipboard_image(true);
}

/**
 * @brief タイマーを用意する(まだなければ作る)。
 * @return タイマー。
 */
QTimer *ensure_timer()
{
	if (!hide_timer) {
		hide_timer = new QTimer();
		/* 1 回だけ発火させる。繰り返すと、消した後もまた消す処理が走り続けるため */
		hide_timer->setSingleShot(true);
		QObject::connect(hide_timer, &QTimer::timeout, on_hide_timer);
	}
	return hide_timer;
}

/// @brief 一時停止を解除し、残り時間を忘れる。
void clear_pause()
{
	paused = false;
	remaining_ms = 0;
}

} // namespace

/**
 * @brief 画像を貼ったときに呼ぶ。残り時間を表示時間に戻す。
 *
 * @details 仕様の詳細は auto-hide.hpp の宣言側を参照。
 */
void auto_hide_restart()
{
	/* 表示時間 0 は「自動で消さない」(#8 で設定できるようにする) */
	if (AUTO_HIDE_SECONDS <= 0) {
		if (hide_timer)
			hide_timer->stop();
		obs_log(LOG_INFO, "auto-hide: disabled (stays until hidden manually)");
		return;
	}

	const int full_ms = AUTO_HIDE_SECONDS * 1000;

	if (paused) {
		/* 一時停止のまま、残り時間だけ表示時間に戻す */
		remaining_ms = full_ms;
		obs_log(LOG_INFO, "auto-hide: paused; remaining time reset to %d seconds", AUTO_HIDE_SECONDS);
		return;
	}

	/* start() は動いている途中でも呼べて、その場合は最初からやり直す。これが「延長」になる */
	ensure_timer()->start(full_ms);
	obs_log(LOG_INFO, "auto-hide: will hide in %d seconds", AUTO_HIDE_SECONDS);
}

/**
 * @brief 一時停止と再開を切り替える。
 *
 * @details 仕様の詳細は auto-hide.hpp の宣言側を参照。
 */
void auto_hide_toggle_pause()
{
	if (AUTO_HIDE_SECONDS <= 0) {
		obs_log(LOG_INFO, "auto-hide: pause ignored (auto-hide is disabled)");
		return;
	}

	/* 一時停止は表示中だけ。表示されていなければ何もしない(残っていた一時停止は解除する) */
	if (!is_clipboard_image_showing()) {
		clear_pause();
		obs_log(LOG_INFO, "auto-hide: pause ignored (no image shown)");
		return;
	}

	if (paused) {
		/* 再開: 覚えておいた残り時間から数え直す */
		const int resume_ms = remaining_ms;
		clear_pause();
		ensure_timer()->start(resume_ms);
		obs_log(LOG_INFO, "auto-hide: resumed (%d ms left)", resume_ms);
		return;
	}

	if (!hide_timer || !hide_timer->isActive()) {
		obs_log(LOG_INFO, "auto-hide: pause ignored (timer is not running)");
		return;
	}

	/* 一時停止: 残り時間を覚えて止める */
	remaining_ms = hide_timer->remainingTime();
	hide_timer->stop();
	paused = true;
	obs_log(LOG_INFO, "auto-hide: paused (%d ms left)", remaining_ms);
}

/**
 * @brief 画像が非表示になったときに呼ぶ。一時停止を解除し、タイマーを止める。
 *
 * @details 仕様の詳細は auto-hide.hpp の宣言側を参照。
 */
void auto_hide_on_image_hidden()
{
	if (paused)
		obs_log(LOG_INFO, "auto-hide: image hidden; pause released");
	clear_pause();
	if (hide_timer)
		hide_timer->stop();
}

/**
 * @brief タイマーを止めて片付け、一時停止も解除する。
 *
 * @details 仕様の詳細は auto-hide.hpp の宣言側を参照。
 */
void auto_hide_shutdown()
{
	clear_pause();
	if (!hide_timer)
		return;

	hide_timer->stop();
	delete hide_timer;
	hide_timer = nullptr;
	obs_log(LOG_INFO, "auto-hide: timer stopped");
}
