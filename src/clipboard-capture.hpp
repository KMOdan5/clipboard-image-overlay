// SPDX-FileCopyrightText: 2026 KMOdan5
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file clipboard-capture.hpp
 * @brief クリップボードの画像を取り出して保存する機能の公開窓口。
 *
 * @details 実装は clipboard-capture.cpp。呼び出し元は plugin-main.cpp
 *          (on_show_requested()・起動完了時の掃除・表示履歴の画像の表示・「前の画像を表示」ホットキー)と、
 *          history-dock.cpp(表示履歴の一覧)。表示履歴からの削除(trash_saved_image())も plugin-main.cpp から呼ぶ。
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

/**
 * @brief 保存した画像の情報。save_clipboard_image() の結果として返す。
 */
struct SavedImage {
	/// @brief 保存したファイルのパス(UTF-8、OS 標準の区切り文字)。
	std::string path;
	/// @brief 画像の幅(ピクセル)。回転情報を反映した後の値。
	int width = 0;
	/// @brief 画像の高さ(ピクセル)。回転情報を反映した後の値。
	int height = 0;
};

/**
 * @brief クリップボードの画像を、プラグインの画像フォルダに保存する。
 *
 * @details
 * 対応する入力(優先順):
 * 1. コピーした画像ファイル(エクスプローラーで Ctrl+C)
 *    … 原則は元ファイルをそのままコピー。OBS で表示できない場合だけ PNG に変換。
 * 2. 画像データ(ブラウザの「画像をコピー」、Win+Shift+S)… PNG で保存。
 *
 * 保存先(Windows): %APPDATA%/obs-studio/plugin_config/clipboard-image-overlay/images/
 *
 * クリップボードの中身が直前に保存した画像と同じなら、新しく保存せずに直前の画像を返す
 * (クリップボードを変えずに表示を繰り返しても、同じ画像のファイルが増えないように)。
 * - 比べる相手は、直前に保存した 1 枚だけ。過去の画像から同じものを探すことはしない
 * - 画像データはピクセルを比べる。コピーしたファイルは、パス・大きさ・更新日時を比べる(中身は比べない)
 * - 直前の画像が削除や掃除で消えていたら、新しく保存する
 * - 使い回すのは直前の画像なので、表示履歴の並び(新しい順)は変わらない
 *
 * @return 保存した画像の情報(パスと幅・高さ)。表示するときの大きさの計算に使う。
 * @retval std::nullopt 画像がない、または保存に失敗した。理由は OBS のログに出る。
 *
 * @note スレッド: 必ず UI スレッドから呼ぶこと(Qt のクリップボードの制約)。
 */
std::optional<SavedImage> save_clipboard_image();

/**
 * @brief 画像ファイルを、プラグインの画像フォルダに保存する。表示履歴のドックにドラッグ&ドロップしたファイル用。
 *
 * @details
 * コピーした画像ファイルを保存するときと同じ処理を使う(save_clipboard_image() の 1)。
 * - 原則は元ファイルをそのままコピー。OBS で表示できない場合だけ PNG に変換する
 * - 直前に保存した画像の元と同じファイル(パス・大きさ・更新日時が同じ)なら、新しく保存せずに直前の画像を返す
 *
 * 保存に失敗したときは、お知らせ(SaveFailed)をここで出す。画像として読めなかったときのお知らせは、呼び出し元が出す
 * (複数のファイルをまとめて保存するとき、1 つずつ出さないように)。
 *
 * @param path 画像ファイルのパス(UTF-8)。
 * @return 保存した画像の情報(使い回したときは直前の画像の情報)。
 * @retval std::nullopt 画像として読めない、または保存に失敗した。理由は OBS のログに出る。
 *
 * @note 呼び出し元: plugin-main.cpp の on_images_dropped()。
 * @note スレッド: UI スレッドから呼ぶこと(直前に保存した画像の記録を、UI スレッドだけで読み書きするため)。
 */
std::optional<SavedImage> save_image_file(const std::string &path);

/**
 * @brief 保存フォルダの古い画像を削除し、新しい順に設定の枚数だけ残す。
 *
 * @details
 * 画像は貼るたびに保存フォルダにたまっていくので、上限を超えた古いものを削除する。
 * - 対象は保存フォルダの中で、このプラグインが付けた名前の形(yyyyMMdd-HHmmss-zzz.拡張子)のファイルだけ。
 *   配信者が自分で置いたファイルなど、それ以外は触らない
 * - 新しい・古いはファイル名の日時で判断する(ファイルの更新日時は、コピー元のものが残ることがあるため)
 * - in_use に含まれるファイル(表示中・差し替え待ちの画像)は、古くても削除しない
 * - 削除した枚数と、削除できなかったファイルはログに出す。削除に失敗しても処理は続ける
 *
 * 残す枚数は、設定の「保存する枚数」(settings.hpp の max_saved_images。既定 50 枚)。
 * 設定を変えても、その場では削除しない。次にここが呼ばれたとき(画像を表示したあと・OBS の起動完了時)に反映する。
 *
 * @param in_use 削除してはいけないファイルのパス(UTF-8)。
 *
 * @note 呼び出し元: plugin-main.cpp(OBS の起動完了時と、画像を表示したあと)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
void cleanup_saved_images(const std::vector<std::string> &in_use);

/**
 * @brief 画像の保存フォルダのパスを返す。
 *
 * @details 表示履歴のドックが、フォルダの変化(新しい画像・掃除で消えた画像)を見張るためと、
 *          設定画面の「保存フォルダを開く」で開くために使う。
 *          フォルダは作らない(まだない場合もある)。
 *
 * @return 保存フォルダのパス(UTF-8、OS 標準の区切り文字)。取得できなければ空。
 *
 * @note 呼び出し元: history-dock.cpp・settings-dialog.cpp。
 */
std::string saved_images_folder();

/**
 * @brief 保存した画像のパスを、新しい順に返す。
 *
 * @details
 * 保存フォルダの画像を、そのまま表示履歴として使う。
 * - 対象は cleanup_saved_images() と同じく、このプラグインが付けた名前の形のファイルだけ
 * - 新しい・古いはファイル名の日時で判断する
 * - 返すのは、設定の「保存する枚数」まで(保存の上限と同じ)。掃除の前に呼ばれても、それより多くは返さない。
 *   上限を下げた直後は、まだ削除されていない古い画像がフォルダに残っているが、それは返さない
 *
 * @return 画像ファイルのパス(UTF-8、OS 標準の区切り文字)。先頭が一番新しい。フォルダがなければ空。
 *
 * @note 呼び出し元: history-dock.cpp(一覧)、plugin-main.cpp(「前の画像を表示」ホットキー・「表示履歴を全て削除」)。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
std::vector<std::string> list_saved_images();

/**
 * @brief 保存した画像の情報(パスと幅・高さ)を読む。
 *
 * @details 表示履歴から表示するとき、show_image_on_current_scene() に渡す大きさを知るために使う。
 *          画像の中身は読まず、大きさだけを読む(回転情報は反映する)。
 *
 * @param path 画像ファイルのパス(UTF-8)。list_saved_images() が返したもの。
 * @return 画像の情報。
 * @retval std::nullopt ファイルがない(掃除などで消えた)・画像として読めない。理由は OBS のログに出る。
 *
 * @note 呼び出し元: plugin-main.cpp の show_history_image()。
 */
std::optional<SavedImage> read_saved_image(const std::string &path);

/**
 * @brief 保存した画像を、OS のごみ箱に送る。配信者が表示履歴から削除したとき(1 枚ずつ・全て)に使う。
 *
 * @details
 * - 対象は保存フォルダの直下にある、このプラグインが付けた名前のファイルだけ。それ以外は削除せずに false を返す
 * - 完全には削除せず、ごみ箱に送る(元に戻せるように。1 枚ずつの削除は確認の画面を出さないため)。
 *   ごみ箱に送れないとき(ごみ箱がない場所など)は、削除せずに false を返す
 * - 既にファイルがなければ、何もせずに true を返す
 * - 'Clipboard Image' の参照はここでは外さない。今の画像を削除するときは、先に呼び出し元で外す(clear_clipboard_image())
 *
 * 結果はログに出す。保存フォルダが変わるので、表示履歴のドックの一覧には自動で反映される。
 *
 * @param path 画像ファイルのパス(UTF-8)。list_saved_images() が返したもの。
 * @return ごみ箱に送れた(または既になかった)ら true。
 * @retval false 対象外のファイル、またはごみ箱に送れなかった。理由は OBS のログに出る。
 *
 * @note 呼び出し元: plugin-main.cpp の delete_history_image()・delete_all_history_images()。
 * @note スレッド: UI スレッドから呼ぶこと。
 */
bool trash_saved_image(const std::string &path);
