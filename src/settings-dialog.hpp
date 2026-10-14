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
 * @file settings-dialog.hpp
 * @brief 設定画面(ダイアログ)の公開窓口。
 *
 * @details 実装は settings-dialog.cpp。呼び出し元は plugin-main.cpp(「ツール」メニュー)。
 */

#pragma once

/**
 * @brief 設定画面を開く。既に開いていれば手前に出す。
 *
 * @details
 * 項目:
 * - 表示時間(秒、0〜600。0 なら自動で消さない)
 * - 大きい画像を画面の [N]% に収める(オン/オフと割合。境界の種類が「自動」のときだけ働くと明記)
 * - 常に手前に表示する(オン/オフ)
 *
 * ボタン(並びは OBS の設定画面に合わせる):
 * - OK: 保存して閉じる
 * - キャンセル(Esc キー・右上の×も同じ): 保存せずに閉じる
 * - 適用: 保存して開いたまま(設定を試しながら画像を貼れる)
 * - 既定値: 画面上の値だけ既定値に戻す(OK か適用を押すまで保存しない)
 *
 * フェードの長さはプラグインの設定に含めず、OBS 標準の表示/非表示トランジションで変えるよう案内する。
 * 開いたまま OBS を操作できる(モードレス)。同時に開くのは 1 つだけ。
 *
 * @note 呼び出し元: plugin-main.cpp の on_settings_menu_clicked()。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void open_settings_dialog();

/**
 * @brief 設定画面が開いていれば、保存せずに閉じる。開いていなければ何もしない。
 *
 * @details OBS の終了時に呼ぶ。開いたままの画面が、片付けの途中の OBS を触らないようにするため。
 *
 * @note 呼び出し元: plugin-main.cpp(終了時・プラグイン解放時)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void close_settings_dialog();
