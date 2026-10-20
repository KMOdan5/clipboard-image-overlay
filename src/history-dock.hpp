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
 * @brief 履歴ドック(#15)の公開窓口。最近表示した画像をサムネイルで並べ、クリックでもう一度表示する。
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
 * @brief 履歴ドックを作り、OBS の「ドック」メニューに登録する。
 *
 * @details
 * 保存フォルダの画像(clipboard-capture.cpp の list_saved_images())を、新しい順にサムネイルで並べる。
 * - 1 ページに 10 枚、最大 5 ページ(保存の上限 50 枚と同じ)。直近の 10 枚が 1 ページ目に出る
 * - サムネイルを 1 回クリックすると on_selected を呼ぶ。表示の処理は呼び出し元(plugin-main.cpp)が行う
 * - 保存フォルダの変化(新しい画像・掃除で消えた画像)を見張り、一覧に自動で反映する。
 *   新しい画像が増えたときは 1 ページ目に戻る
 * - サムネイルは OBS の画面を止めないよう、裏のスレッドで作る。作ったものは使い回す
 *
 * 登録に失敗しても、ほかの機能(ホットキー・メニュー)は使えるので、警告をログに出して続ける。
 *
 * @param on_selected サムネイルがクリックされたときに呼ぶ処理。
 *
 * @note 呼び出し元: plugin-main.cpp の obs_module_load()。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void history_dock_register(HistorySelectedCallback on_selected);

/**
 * @brief 履歴ドックの裏の処理(フォルダの見張り・サムネイルの作成)を止める。何度呼んでもよい。
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
