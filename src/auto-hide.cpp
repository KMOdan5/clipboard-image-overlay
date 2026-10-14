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
 * @brief 表示した画像を、一定時間後に自動で消すタイマー。
 *
 * @details
 * 処理の流れ:
 * @code
 *   画像を表示 → auto_hide_restart() → (AUTO_HIDE_SECONDS 秒後) → hide_clipboard_image()
 *                     ↑                                              (フェードアウトして非表示)
 *   表示中に再度ホットキー ─┘ 残り時間を最初からやり直す(延長)
 * @endcode
 *
 * タイマーは 1 つだけ持ち、使い回す。表示のたびに新しいタイマーを作ると、
 * 古いタイマーが後から発火して、延長したはずの画像が早く消えてしまうため。
 *
 * 関連ファイル:
 * - image-display.cpp … hide_clipboard_image()(実際に画像を消す処理)
 * - plugin-main.cpp … 画像を表示した後に auto_hide_restart() を呼ぶ。終了時に auto_hide_shutdown() を呼ぶ
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
 */
constexpr int AUTO_HIDE_SECONDS = 7;

/// @brief 自動で消すためのタイマー。最初に使うときに作り、auto_hide_shutdown() で片付ける。
QTimer *hide_timer = nullptr;

/**
 * @brief タイマーの時間が来たときの処理。画像を消す。
 *
 * @note 呼び出し元: Qt(hide_timer の timeout)。
 * @note スレッド: UI スレッド(タイマーを作ったスレッドで届く)。
 */
void on_hide_timer()
{
	obs_log(LOG_INFO, "auto-hide: %d seconds passed", AUTO_HIDE_SECONDS);
	hide_clipboard_image();
}

} // namespace

/**
 * @brief 自動で消すタイマーを開始する(延長も兼ねる)。
 *
 * @details 仕様の詳細は auto-hide.hpp の宣言側を参照。
 */
void auto_hide_restart()
{
	if (!hide_timer) {
		hide_timer = new QTimer();
		/* 1 回だけ発火させる。繰り返すと、消した後もまた消す処理が走り続けるため */
		hide_timer->setSingleShot(true);
		QObject::connect(hide_timer, &QTimer::timeout, on_hide_timer);
	}

	/* start() は動いている途中でも呼べて、その場合は残り時間を最初からやり直す。これが「延長」になる */
	hide_timer->start(AUTO_HIDE_SECONDS * 1000);
	obs_log(LOG_INFO, "auto-hide: will hide in %d seconds", AUTO_HIDE_SECONDS);
}

/**
 * @brief タイマーを止めて片付ける。
 *
 * @details 仕様の詳細は auto-hide.hpp の宣言側を参照。
 */
void auto_hide_shutdown()
{
	if (!hide_timer)
		return;

	hide_timer->stop();
	delete hide_timer;
	hide_timer = nullptr;
	obs_log(LOG_INFO, "auto-hide: timer stopped");
}
