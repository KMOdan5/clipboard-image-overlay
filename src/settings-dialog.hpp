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
 * @brief 設定画面を開く。「OK」を押すと設定を保存し、「キャンセル」なら何も変えない。
 *
 * @details
 * 項目:
 * - 表示時間(秒、0〜600。0 なら自動で消さない)
 * - 大きい画像を画面の [N]% に収める(オン/オフと割合。境界の種類が「自動」のときだけ働くと明記)
 * - 常に手前に表示する(オン/オフ)
 *
 * フェードの長さはプラグインの設定に含めず、OBS 標準の表示/非表示トランジションで変えるよう案内する。
 * 「既定に戻す」で、画面上の値を既定値に戻せる(「OK」を押すまで保存しない)。
 * 閉じるまで OBS の画面は操作できない(モーダル)。
 *
 * @note 呼び出し元: plugin-main.cpp の on_settings_menu_clicked()。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void open_settings_dialog();
