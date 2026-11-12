// SPDX-FileCopyrightText: 2026 KMOdan5
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file settings.hpp
 * @brief プラグインの設定値(表示時間・最大サイズなど)の公開窓口。
 *
 * @details
 * 実装は settings.cpp。設定画面(settings-dialog.cpp)で変更し、
 * 自動で消す処理(auto-hide.cpp)と表示処理(image-display.cpp)が読む。
 *
 * 保存先: プラグインの設定フォルダの settings.json
 * (Windows: %APPDATA%/obs-studio/plugin_config/clipboard-image-overlay/settings.json)。
 * シーンコレクションではなくプラグイン全体の設定なので、どのシーンコレクションでも同じ値を使う。
 *
 * 変更は、次に画像を表示したときから反映する(表示中のタイマーや画像の大きさは変えない)。
 */

#pragma once

/// @brief 表示時間(秒)の下限。0 は「自動で消さない」。
constexpr int DISPLAY_SECONDS_MIN = 0;
/// @brief 表示時間(秒)の上限。入力欄に 3 桁まで入れられる、と直感的に分かる値にする。
constexpr int DISPLAY_SECONDS_MAX = 999;
/// @brief 最大サイズ(画面に対する割合、%)の下限。
constexpr int MAX_SIZE_PERCENT_MIN = 10;
/// @brief 最大サイズ(画面に対する割合、%)の上限。
constexpr int MAX_SIZE_PERCENT_MAX = 100;

/**
 * @brief プラグインの設定値。初期値が既定値。
 */
struct PluginSettings {
	/**
	 * @brief 画像を表示してから自動で非表示にするまでの秒数。
	 * @details 0 なら自動で非表示にせず、表示し続ける(一時停止ホットキーも何もしない)。
	 */
	int display_seconds = 10;

	/**
	 * @brief 画像の最大の大きさ(キャンバスの面積に対する割合、%)。これを超える画像は縮小する。
	 * @details
	 * 最大の幅と高さは、キャンバスの幅と高さにそれぞれ √(割合) をかけて出す(max_size_side_ratio())。
	 * Clipboard Image の境界が「自動」のときだけ使う。大きさを自分で決めたい配信者は、境界の種類を
	 * 「内側に合わせる」などに変える(その場合プラグインは大きさを変えない)。
	 * 縮小のオン/オフは設けない(境界の種類で足りるため)。
	 */
	int max_size_percent = 60;

	/**
	 * @brief 貼るたびに Clipboard Image を最前面(一番手前)に移すかどうか。
	 * @details オフなら重なり順を一切変えない(重なり順を自分で管理したい配信者向け)。
	 */
	bool always_on_top = true;

	/**
	 * @brief スタジオモードで「シーンを複製」がオンのとき、表示した画像をプログラムにすぐ表示するかどうか。
	 * @details
	 * 「シーンを複製」がオンだと、プログラムに映るのはトランジションしたときに作られたシーンのコピーなので、
	 * 元のシーンに表示した画像は、次のトランジションまでプログラムに出ない。オンなら、コピーにも同じように表示する。
	 * OBS の「シーンを複製」の設定そのものは変えない。既定はオフ(OBS のスタジオモードの操作感を変えるため)。
	 * 非表示はこの設定に関係なく、いつもコピーにも行う(トランジションでプログラムに出た画像が残らないように)。
	 */
	bool studio_mode_show_on_program = false;
};

/**
 * @brief 「最大サイズの割合」から、キャンバスの幅と高さにかける割合を出す。
 *
 * @details
 * 最大サイズの割合は、キャンバスの面積に対する割合として扱う。キャンバスと同じ縦横比の枠で、
 * 面積が「キャンバスの面積 × 割合」になるように、幅と高さにはそれぞれ √(割合) をかける。
 * 枠はキャンバスと同じ形でキャンバスより小さいので、中心に置いた画像が画面からはみ出すことはない。
 *
 * 例: 割合 60% → √0.6 ≒ 0.775。1920x1080 のキャンバスなら最大 1487x837(面積はキャンバスの 60%)
 *
 * 小数点以下は丸めずに返す。呼び出し元も倍率を丸めずに使う(丸めると、最大サイズを 1 ピクセル超えることがあるため)。
 * 画面やログに出すピクセル数だけ、四捨五入して整数にする。
 *
 * @param percent 最大サイズの割合(%)。
 * @return キャンバスの幅と高さにかける割合(0〜1)。
 *
 * @note 呼び出し元: image-display.cpp の apply_display_size()(表示のたび)、settings-dialog.cpp の update_pixel_label()(設定画面のピクセル数の表示)。
 * @note スレッド: どこからでもよい(計算だけ)。
 */
double max_size_side_ratio(int percent);

/**
 * @brief 今の設定値を返す。
 *
 * @return 設定値。settings_load() の前は既定値。
 *
 * @note スレッド: UI スレッドから呼ぶこと(設定画面と同じスレッドで読み書きするため、ロックはしない)。
 */
const PluginSettings &settings_get();

/**
 * @brief 設定ファイルを読み込む。ファイルがない・壊れている項目は既定値を使う。
 *
 * @details 範囲外の値(ファイルを手で書き換えた場合など)は、範囲内に収めて使う。
 *
 * @note 呼び出し元: plugin-main.cpp の obs_module_load()。
 * @note スレッド: UI スレッド。
 */
void settings_load();

/**
 * @brief 設定値を変更し、設定ファイルに保存する。
 *
 * @details 範囲外の値は範囲内に収める。保存に失敗しても、今回の起動中は変更後の値を使う。
 *
 * @param settings 新しい設定値。
 * @return 保存できたら true。
 * @retval false 保存に失敗した(理由は OBS のログに出る)。
 *
 * @note 呼び出し元: settings-dialog.cpp(「OK」を押したとき)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
bool settings_save(const PluginSettings &settings);
