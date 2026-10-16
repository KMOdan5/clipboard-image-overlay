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
 * 項目(OBS の設定画面と同じく、見出し付きの枠の中に、左の列に項目名、右の列に入力欄と説明文):
 * - 枠「非表示のタイミング」
 *   - 表示時間(秒、0〜600。0 なら手動でのみ非表示)
 * - 枠「自動調整」(表示するたびにプラグインが自動で行うこと)
 *   - 最大サイズ(キャンバスに対する割合、10〜100%。横にその大きさのピクセル数を薄い文字で表示。
 *     境界設定が「自動」のときのみ有効と明記)
 *   - 常に最前面に表示する(オン/オフ)
 * 枠の中の説明文は、項目と区別できるよう薄い文字で表示する。
 * 説明文は入力欄のすぐ下に詰めて置き、項目どうしの間は広く空ける(説明文がどの項目のものか、位置で分かるように)。
 * 枠の外の案内(フェードの変え方・反映のタイミング)は画面全体への案内なので、通常の濃さで表示する。
 * その下(ボタンの上)に、バージョン(例: テスト版は v0.1.0-beta、完成版は v0.1.0)と、マニュアル・不具合・ご要望のフォームへのリンクを薄い文字で表示する。
 * 不具合・ご要望のリンクは、プラグインの版・OBS のバージョン・OS を入力した状態でフォームを開く。
 * リンク先は OBS の言語設定に合わせて変わる(日本語は日本語のマニュアル、それ以外は英語の README)。
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
