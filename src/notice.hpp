// SPDX-FileCopyrightText: 2026 KMOdan5
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file notice.hpp
 * @brief 画像を表示できなかった理由を、配信者に知らせる仕組みの公開窓口。
 *
 * @details 実装は notice.cpp。
 *
 * 保存・表示の処理(clipboard-capture.cpp・image-display.cpp・plugin-main.cpp)と、
 * お知らせを出す画面(history-dock.cpp)の間に入る。保存・表示の処理は、失敗のログを出した直後に
 * notice_report() を呼ぶだけにし、どこに・どう出すかはここと画面の側で決める。
 *
 * どの操作(ホットキー・メニュー・表示履歴のドック・「前の画像を表示」)から表示しても、
 * 最後は同じ保存・表示の処理を通るので、操作の種類に関係なくお知らせが出る。
 *
 * お知らせの処理が失敗しても、保存・表示の処理には影響させない(どの関数も例外を外に出さず、結果も返さない)。
 * 失敗したときは、お知らせが出ないだけになる。
 */

#pragma once

/**
 * @brief お知らせの種類。画像を表示できなかった理由ごとに 1 つ。
 *
 * @details 文言は data/locale の ini の Notice.* にある。
 */
enum class Notice {
	/// @brief クリップボードに画像がない、またはコピー・ドラッグ&ドロップしたファイルを画像として読み込めない。
	ImageNotRecognized,
	/// @brief 'Clipboard Image' という名前の、画像ではないソースがある。
	SourceNameConflict,
	/// @brief シーンの読み込み・切り替え・終了の途中に、画像を表示しようとした。
	ScenesLoading,
	/// @brief 保存フォルダに画像を保存できない。
	SaveFailed,
	/// @brief まれな表示の失敗('Clipboard Image' の削除の途中、ソースを作れない・シーンに置けない、など)。
	DisplayFailed,
	/// @brief 表示履歴の画像のファイルを読み込めない(消えた・壊れた)。
	ImageNotFound,
};

/**
 * @brief 画像を表示する操作を始めるときに呼ぶ。前の操作のお知らせの記録を空にする。
 *
 * @details
 * 1 回の操作の中では、最初に報告されたお知らせだけを出す(notice_report())。
 * 例: コピーしたファイルの保存に失敗すると、続けてほかの候補を探し、最後に「画像がない」の報告も届く。
 * このとき本当の原因は保存の失敗なので、先に届いたほうを出す。
 *
 * @note 呼び出し元: plugin-main.cpp の accept_user_action()(画像を表示する操作のとき)。
 * @note スレッド: UI スレッド。
 */
void notice_begin_operation() noexcept;

/**
 * @brief 画像を表示できなかった理由を知らせる。
 *
 * @details
 * - 表示履歴のドックが見えているときだけ、ドックの一番上にお知らせを出す。
 *   ドックが閉じている・別のドックのタブの裏にあるときは、何もしない(覚えておいて後で出すこともしない)
 * - 1 回の操作の中で 2 回目以降の報告は無視する(notice_begin_operation())
 * - UI スレッド以外から呼ばれたときも、何もしない(画面は UI スレッドからしか触れないため)
 *
 * 失敗のログは呼び出し元が出す。ここではログを出さない(同じ内容が二重に残らないように)。
 *
 * @param notice お知らせの種類。
 *
 * @note 呼び出し元: clipboard-capture.cpp・image-display.cpp・plugin-main.cpp の、失敗のログを出す箇所。
 * @note スレッド: UI スレッド。
 */
void notice_report(Notice notice) noexcept;

/**
 * @brief 画像を表示できたときに呼ぶ。出ているお知らせを消す。
 *
 * @details 古いお知らせが、表示できた後も残って見えないようにするため。
 *
 * @note 呼び出し元: plugin-main.cpp の show_saved_image()。
 * @note スレッド: UI スレッド。
 */
void notice_clear() noexcept;
