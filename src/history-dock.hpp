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
 * @file history-dock.hpp
 * @brief 表示履歴のドックの公開窓口。最近表示した画像をサムネイルで並べ、クリックでもう一度表示する。
 *
 * @details 実装は history-dock.cpp。呼び出し元は plugin-main.cpp。
 */

#pragma once

#include <functional>
#include <string>

/**
 * @brief 履歴ドックでサムネイルがクリックされたときに呼ぶ処理。
 * @details 引数はクリックされた画像ファイルのパス(UTF-8)。UI スレッドで呼ばれる。
 */
using HistorySelectedCallback = std::function<void(const std::string &path)>;

/**
 * @brief 今の画像を、履歴ドックが問い合わせる処理。
 * @details 'Clipboard Image' が参照している画像ファイル(最後に表示した画像)のパス(UTF-8)を返す。
 *          表示中か非表示かは問わない。'Clipboard Image' がなければ空を返す。UI スレッドで呼ばれる。
 */
using HistoryCurrentCallback = std::function<std::string()>;

/**
 * @brief 履歴ドックを作り、OBS の「ドック」メニューに登録する。
 *
 * @details
 * 保存フォルダの画像(clipboard-capture.cpp の list_saved_images())を、新しい順にサムネイルで並べる。
 * - 最大 50 枚(保存の上限と同じ)。多いときは縦にスクロールする。一番新しい画像が左上に出る
 * - 1 行の枚数とサムネイルの大きさは、ドックの幅に合わせて変わる(広げると大きく、狭めると 1 行の枚数が減る)
 * - 1 枚ごとに枠付きのカードに載せ、マウスを乗せると色が変わる
 * - 今の画像('Clipboard Image' が参照している画像)のカードは、太い枠と濃い背景で目立たせる。
 *   表示中か非表示かに関係なく目立たせる(どれを選んでいるかを示す)。今の画像は current に問い合わせる
 *   (ホットキー・ドック・目のアイコンなど、どこから表示/非表示にしても追いつくよう、ドックが見えている間、定期的に問い合わせる)
 * - サムネイルを 1 回クリックすると on_selected を呼ぶ。表示の処理は呼び出し元(plugin-main.cpp)が行う
 * - 保存フォルダの変化(新しい画像・掃除で消えた画像)を見張り、一覧に自動で反映する。
 *   新しい画像が増えたときは一番上までスクロールする
 * - サムネイルは OBS の画面を止めないよう、裏のスレッドで作る。作ったものは使い回す
 * - GIF アニメのサムネイルは最初の 1 コマで、動かさない
 * - 一覧の読み込み・サムネイルの作成・今の画像の問い合わせは、ドックが見えている間だけ行う。
 *   見えていない間の変化は、見えたときにまとめて反映する
 *
 * 登録に失敗しても、ほかの機能(ホットキー・メニュー)は使えるので、警告をログに出して続ける。
 *
 * @param on_selected サムネイルがクリックされたときに呼ぶ処理。
 * @param current     今の画像を問い合わせる処理。
 *
 * @note 呼び出し元: plugin-main.cpp の obs_module_load()。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void history_dock_register(HistorySelectedCallback on_selected, HistoryCurrentCallback current);

/**
 * @brief 履歴ドックの裏の処理(フォルダの見張り・今の画像の問い合わせ・サムネイルの作成)を止める。何度呼んでもよい。
 *
 * @details
 * OBS の終了時に呼ぶ。終了の途中でフォルダの変化やサムネイルの完成が届き、
 * 片付け中の画面を触るのを防ぐため。作成中のサムネイルがあれば、終わるまで待つ(1 枚ずつで短い)。
 *
 * @note 呼び出し元: plugin-main.cpp(終了時のイベント)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void history_dock_shutdown();

/**
 * @brief 履歴ドックを OBS から外す。
 *
 * @details OBS が先にドックを片付けていた場合(終了時など)は何もしない。
 *
 * @note 呼び出し元: plugin-main.cpp の obs_module_unload()。
 */
void history_dock_unregister();
