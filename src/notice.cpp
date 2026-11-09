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
 * @file notice.cpp
 * @brief 画像を表示できなかった理由を、表示履歴のドックに伝える。
 *
 * @details
 * 処理の流れ:
 * @code
 *   画像を表示する操作の始まり ─→ notice_begin_operation()  前の操作の記録を空にする
 *   失敗のログを出した直後 ─→ notice_report(種類)  1 回の操作で最初の 1 つだけ
 *        └→ history_dock_show_notice(文言)  ドックが見えていれば出す(history-dock.cpp)
 *   表示できた ─→ notice_clear() ─→ history_dock_clear_notice()
 * @endcode
 *
 * どの関数も例外を外に出さない。お知らせが出せなくても、保存・表示の処理は続けられるようにするため。
 *
 * 公開している関数の説明は notice.hpp を参照。
 */

#include "notice.hpp"
#include "history-dock.hpp"

#include <obs-module.h>

#include <QCoreApplication>
#include <QThread>

namespace {

/// @brief true なら、今の操作でお知らせを出した。2 つ目以降を出さないため。UI スレッドだけで読み書きする。
bool reported = false;

/**
 * @brief 今のスレッドが UI スレッドかを返す。
 * @details OBS の終了中など、アプリの本体がもうない場合も false を返す。
 */
bool on_ui_thread()
{
	const QCoreApplication *app = QCoreApplication::instance();
	return app && QThread::currentThread() == app->thread();
}

/**
 * @brief お知らせの種類に対応する、ini の文言のキーを返す。
 * @details 文言は「状況」と「次にすること」を改行でつなぐ(ini では、バックスラッシュと n で改行を書く)。
 */
const char *text_key(Notice notice)
{
	switch (notice) {
	case Notice::ImageNotRecognized:
		return "Notice.ImageNotRecognized";
	case Notice::SourceNameConflict:
		return "Notice.SourceNameConflict";
	case Notice::ScenesLoading:
		return "Notice.ScenesLoading";
	case Notice::SaveFailed:
		return "Notice.SaveFailed";
	case Notice::DisplayFailed:
		return "Notice.DisplayFailed";
	case Notice::ImageNotFound:
		return "Notice.ImageNotFound";
	}
	return nullptr;
}

} // namespace

/**
 * @brief 前の操作のお知らせの記録を空にする。
 *
 * @details 仕様の詳細は notice.hpp の宣言側を参照。
 */
void notice_begin_operation() noexcept
{
	if (on_ui_thread())
		reported = false;
}

/**
 * @brief 画像を表示できなかった理由を、ドックに出す。
 *
 * @details 仕様の詳細は notice.hpp の宣言側を参照。
 */
void notice_report(Notice notice) noexcept
{
	/* 失敗しても、お知らせが出ないだけにする。呼び出し元(保存・表示の処理)を止めないため */
	try {
		if (!on_ui_thread() || reported)
			return;
		const char *key = text_key(notice);
		if (!key)
			return;
		reported = true;
		history_dock_show_notice(obs_module_text(key));
	} catch (...) {
		/* お知らせが出ないだけにする */
	}
}

/**
 * @brief 出ているお知らせを消す。
 *
 * @details 仕様の詳細は notice.hpp の宣言側を参照。
 */
void notice_clear() noexcept
{
	try {
		if (!on_ui_thread())
			return;
		history_dock_clear_notice();
	} catch (...) {
		/* お知らせが残るだけにする */
	}
}
