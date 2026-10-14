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

/*
 * 【このファイルの役割】
 * クリップボードから画像を取り出し、プラグインの画像フォルダに保存する。
 *
 * 保存の方針:
 *   - コピーしたファイル … 原則、元ファイルを拡張子ごとそのままコピーする
 *                          (GIF のアニメーション、画質、ファイルサイズを保つため)
 *     ただし次の場合だけ PNG に変換する(OBS で確実に表示できるようにするため)
 *       - OBS の画像ソースが表示できない形式(webp・tiff・ico など)
 *       - 回転情報(EXIF)付きの写真(OBS はそのまま表示すると横倒しになる)
 *   - 画像データ       … 元のファイルがないので PNG で保存する
 *
 * 保存先: %APPDATA%\obs-studio\plugin_config\obs-clipboard-image\images\
 * ファイル名: yyyyMMdd-HHmmss-zzz.<拡張子>(保存した日時)
 *
 * 公開している関数は save_clipboard_image() だけ(clipboard-capture.hpp)。
 * それ以外はこのファイル内だけで使う補助関数。
 */

#include "clipboard-capture.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QUrl>

namespace {

/* プラグイン設定フォルダの中に作る、画像の保存用フォルダ名 */
constexpr const char *IMAGE_FOLDER = "images";

/*
 * OBS の画像ソースが表示できる形式(小文字の拡張子)。
 * ここに含まれるファイルは変換せずにそのまま保存する。
 *
 * webp を含めていない理由: OBS はアニメーション webp を読み込めず、
 * そのまま渡すと何も表示されないため。PNG(静止画)に変換して確実に表示させる。
 * 将来 OBS が新しい形式に対応したら、ここに拡張子を追加する。
 */
const QStringList OBS_IMAGE_FORMATS = {
	QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
	QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tga"),
};

/*
 * 画像の保存先フォルダのパスを返す。
 * 例: %APPDATA%\obs-studio\plugin_config\obs-clipboard-image\images
 * フォルダの作成はしない(new_image_path() で行う)。
 */
QString image_folder()
{
	char *path = obs_module_config_path(IMAGE_FOLDER);
	QString folder = QString::fromUtf8(path ? path : "");
	/* obs_module_config_path() が確保した文字列は、呼び出し側で解放する決まり */
	bfree(path);
	return folder;
}

/*
 * 新しく保存するファイルのパスを決める。
 *
 * 引数  : extension … 拡張子(ドットなし。例: "png")
 * 戻り値: 保存先フォルダ + 日時のファイル名。フォルダが作れなければ空文字
 * 補足  : 保存先フォルダがなければここで作る
 */
QString new_image_path(const QString &extension)
{
	const QString folder = image_folder();
	if (folder.isEmpty() || !QDir().mkpath(folder)) {
		obs_log(LOG_WARNING, "save: cannot create folder %s", folder.toUtf8().constData());
		return QString();
	}

	/* ミリ秒まで入れて、連続で保存してもファイル名が重ならないようにする */
	const QString name = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) +
			     QStringLiteral(".") + extension;
	return QDir(folder).filePath(name);
}

/*
 * 保存成功時の後処理。ログを出し、呼び出し元に返すパスを作る。
 *
 * 引数  : path … 保存したファイルのパス
 *         size … 画像の幅と高さ(ログ用)
 *         how  … 保存のしかた。"copied original"(そのままコピー)か "encoded png"(PNG に変換)
 * 戻り値: OS 標準の区切り文字(Windows なら \)にした UTF-8 のパス
 */
std::optional<std::string> saved(const QString &path, const QSize &size, const char *how)
{
	const QString native = QDir::toNativeSeparators(path);
	obs_log(LOG_INFO, "save: %dx%d (%s) -> %s", size.width(), size.height(), how, native.toUtf8().constData());
	return native.toUtf8().toStdString();
}

/*
 * 画像を PNG ファイルとして保存する。
 *
 * 呼び出し元: save_local_file()(変換が必要なファイル)
 *             save_clipboard_image()(画像データ)
 * 戻り値    : 保存したパス。失敗したら std::nullopt
 */
std::optional<std::string> save_as_png(const QImage &image)
{
	const QString path = new_image_path(QStringLiteral("png"));
	if (path.isEmpty())
		return std::nullopt;

	if (!image.save(path, "PNG")) {
		obs_log(LOG_WARNING, "save: failed to write %s", path.toUtf8().constData());
		return std::nullopt;
	}
	return saved(path, image.size(), "encoded png");
}

/*
 * コピーされたファイル 1 つを保存する。
 *
 * 呼び出し元: save_clipboard_image()
 * 引数      : source … コピーされたファイルのパス
 * 戻り値    : 保存したパス。画像ファイルでない・読めない・保存失敗なら std::nullopt
 *
 * 判断の流れ:
 *   1. Qt で画像として読めるか確認 → 読めなければ対象外(exe など)
 *   2. OBS で表示できる形式で、回転も不要 → 元ファイルをそのままコピー
 *   3. それ以外 → 画像を読み込んで PNG に変換
 */
std::optional<std::string> save_local_file(const QString &source)
{
	QImageReader reader(source);
	/* EXIF の回転情報を反映して読み込む(手順 3 で PNG に変換するときに効く) */
	reader.setAutoTransform(true);

	/* 1. 画像として読めるか(ファイルの中身で判定。拡張子だけでは判断しない) */
	if (!reader.canRead()) {
		obs_log(LOG_INFO, "clipboard: skipped copied file %s (%s)", source.toUtf8().constData(),
			reader.errorString().toUtf8().constData());
		return std::nullopt;
	}

	/*
	 * 2. そのまま保存してよいかを判定する。
	 *    format(中身から判定した形式)と extension(ファイル名の拡張子)の両方を見るのは、
	 *    拡張子と中身が食い違うファイル(例: 中身が webp なのに .png)を OBS に渡さないため。
	 */
	const QString format = QString::fromLatin1(reader.format()).toLower();
	const QString extension = QFileInfo(source).suffix().toLower();
	const bool obs_can_display = OBS_IMAGE_FORMATS.contains(format) && OBS_IMAGE_FORMATS.contains(extension);
	const bool needs_rotation = reader.transformation() != QImageIOHandler::TransformationNone;
	obs_log(LOG_INFO, "clipboard: read copied file %s (format: %s)", source.toUtf8().constData(),
		format.toUtf8().constData());

	if (obs_can_display && !needs_rotation) {
		const QString path = new_image_path(extension);
		if (path.isEmpty())
			return std::nullopt;
		if (!QFile::copy(source, path)) {
			obs_log(LOG_WARNING, "save: failed to copy to %s", path.toUtf8().constData());
			return std::nullopt;
		}
		return saved(path, reader.size(), "copied original");
	}

	/* 3. OBS で表示できない形式、または回転が必要な写真は PNG に変換する */
	const QImage image = reader.read();
	if (image.isNull()) {
		obs_log(LOG_INFO, "clipboard: failed to decode %s (%s)", source.toUtf8().constData(),
			reader.errorString().toUtf8().constData());
		return std::nullopt;
	}
	return save_as_png(image);
}

} // namespace

/*
 * クリップボードの画像を保存する(公開関数。詳しい仕様は clipboard-capture.hpp)。
 *
 * 呼び出し元: plugin-main.cpp の on_show_requested()
 * スレッド  : UI スレッド
 */
std::optional<std::string> save_clipboard_image()
{
	const QClipboard *clipboard = QGuiApplication::clipboard();
	const QMimeData *mime = clipboard ? clipboard->mimeData() : nullptr;
	if (!mime) {
		obs_log(LOG_INFO, "clipboard: unavailable");
		return std::nullopt;
	}

	/*
	 * 優先 1: コピーされたファイル。
	 * 元ファイルのほうが画質・透過・アニメーションを保てるので、画像データより先に見る。
	 * 複数ファイルをコピーした場合は、最初に保存できた 1 つだけを使う。
	 * Web 上の URL など、ローカルにないものは対象外。
	 */
	if (mime->hasUrls()) {
		for (const QUrl &url : mime->urls()) {
			if (!url.isLocalFile())
				continue;
			if (auto path = save_local_file(url.toLocalFile()))
				return path;
		}
	}

	/*
	 * 優先 2: 画像データ(ブラウザの「画像をコピー」、Win+Shift+S など)。
	 * ブラウザで GIF をコピーした場合も、ここには 1 コマ分の静止画しか入らない(OS・ブラウザの仕様)。
	 */
	if (mime->hasImage()) {
		const QImage image = qvariant_cast<QImage>(mime->imageData());
		if (!image.isNull()) {
			obs_log(LOG_INFO, "clipboard: read image data");
			return save_as_png(image);
		}
	}

	/* 画像が見つからない場合: 原因調査のため、クリップボードにあった形式名をログに残す */
	obs_log(LOG_INFO, "clipboard: no image found (formats: %s)",
		mime->formats().join(QStringLiteral(", ")).toUtf8().constData());
	return std::nullopt;
}
