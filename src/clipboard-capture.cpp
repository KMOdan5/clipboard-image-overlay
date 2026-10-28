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
 * @file clipboard-capture.cpp
 * @brief クリップボードから画像を取り出し、プラグインの画像フォルダに保存する。
 *
 * @details
 * 保存の方針:
 * - コピーしたファイル … 原則、元ファイルを拡張子ごとそのままコピーする
 *   (GIF のアニメーション、画質、ファイルサイズを保つため)。
 *   ただし次の場合だけ PNG に変換する(OBS で確実に表示できるようにするため)。
 *   - OBS の画像ソースが表示できない形式(webp・tiff・ico など)
 *   - 回転情報(EXIF)付きの写真(OBS はそのまま表示すると横倒しになる)
 * - 画像データ … 元のファイルがないので PNG で保存する。
 *
 * 保存先(Windows): %APPDATA%/obs-studio/plugin_config/clipboard-image-overlay/images/
 *
 * ファイル名: yyyyMMdd-HHmmss-zzz.拡張子(保存した日時)
 *
 * 古い画像の掃除: 新しい順に MAX_SAVED_IMAGES 枚だけ残し、それより古いものを削除する
 * (cleanup_saved_images())。ファイル名の形と保存フォルダを知っているのがこのファイルなので、ここで行う。
 *
 * 表示履歴: 保存フォルダの画像を、そのまま履歴として使う。list_saved_images() で新しい順に並べ、
 * read_saved_image() で表示に必要な大きさを読む。一覧に出すのも、掃除と同じくプラグインが付けた名前のファイルだけ。
 * 配信者が表示履歴から選んで削除するときは、trash_saved_image() で OS のごみ箱に送る(元に戻せるように)。
 *
 * 公開している関数は save_clipboard_image()・cleanup_saved_images()・saved_images_folder()・
 * list_saved_images()・read_saved_image()・trash_saved_image()(clipboard-capture.hpp)。
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
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace {

/// @brief プラグイン設定フォルダの中に作る、画像の保存用フォルダ名。
constexpr const char *IMAGE_FOLDER = "images";

/**
 * @brief 保存フォルダに残す画像の枚数。これより古い画像は cleanup_saved_images() で削除する。
 * @note 今は固定。枚数・容量の上限を設定画面で変えられるようにするかは #23 で検討する。
 */
constexpr int MAX_SAVED_IMAGES = 50;

/**
 * @brief OBS の画像ソースが表示できる形式(小文字の拡張子)。
 *
 * @details
 * ここに含まれるファイルは変換せずにそのまま保存する。
 * webp を含めていない理由: OBS はアニメーション webp を読み込めず、
 * そのまま渡すと何も表示されないため。PNG(静止画)に変換して確実に表示させる。
 *
 * @note 将来 OBS が新しい形式に対応したら、ここに拡張子を追加する。
 */
const QStringList OBS_IMAGE_FORMATS = {
	QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
	QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tga"),
};

/**
 * @brief このプラグインが付けた名前(yyyyMMdd-HHmmss-zzz.拡張子)かどうかを判定する正規表現。
 *
 * @details 古い画像の掃除・表示履歴の一覧・表示履歴からの削除で同じ条件を使う。
 *          配信者が保存フォルダに置いたファイルを、消したり一覧に出したりしないため。
 */
const QRegularExpression &saved_name_pattern()
{
	static const QRegularExpression pattern(QStringLiteral("^\\d{8}-\\d{6}-\\d{3}\\.(png|jpg|jpeg|gif|bmp|tga)$"),
						QRegularExpression::CaseInsensitiveOption);
	return pattern;
}

/**
 * @brief 保存フォルダにある、このプラグインが付けた名前の画像を、新しい順に返す。
 *
 * @details 名前は日時なので、名前の降順に並べると新しい順になる。
 *
 * @param dir 保存フォルダ。
 * @return ファイル名(フォルダを含まない)の一覧。
 */
QStringList saved_names_newest_first(const QDir &dir)
{
	QStringList names;
	for (const QString &name : dir.entryList(QDir::Files, QDir::Name | QDir::Reversed)) {
		if (saved_name_pattern().match(name).hasMatch())
			names.append(name);
	}
	return names;
}

/**
 * @brief 画像の保存先フォルダのパスを返す。
 *
 * @details 例(Windows): %APPDATA%/obs-studio/plugin_config/clipboard-image-overlay/images
 *
 * @return 保存先フォルダのパス。取得できなければ空文字。
 * @note フォルダの作成はしない(new_image_path() で行う)。
 */
QString image_folder()
{
	char *path = obs_module_config_path(IMAGE_FOLDER);
	QString folder = QString::fromUtf8(path ? path : "");
	/* obs_module_config_path() が確保した文字列は、呼び出し側で解放する決まり */
	bfree(path);
	return folder;
}

/**
 * @brief 新しく保存するファイルのパスを決める。保存先フォルダがなければここで作る。
 *
 * @param extension 拡張子(ドットなし。例: "png")。
 * @return 保存先フォルダ + 日時のファイル名。フォルダが作れなければ空文字。
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

/**
 * @brief 保存成功時の後処理。ログを出し、呼び出し元に返すパスを作る。
 *
 * @param path 保存したファイルのパス。
 * @param size 画像の幅と高さ(ログ用)。
 * @param how  保存のしかた。"copied original"(そのままコピー)か "encoded png"(PNG に変換)。
 * @return 保存した画像の情報。パスは OS 標準の区切り文字(Windows なら \\)にした UTF-8。
 */
std::optional<SavedImage> saved(const QString &path, const QSize &size, const char *how)
{
	const QString native = QDir::toNativeSeparators(path);
	obs_log(LOG_INFO, "save: %dx%d (%s) -> %s", size.width(), size.height(), how, native.toUtf8().constData());
	return SavedImage{native.toUtf8().toStdString(), size.width(), size.height()};
}

/**
 * @brief 画像を PNG ファイルとして保存する。
 *
 * @param image 保存する画像。
 * @return 保存した画像の情報。
 * @retval std::nullopt 保存に失敗した。
 *
 * @see save_local_file()(変換が必要なファイル)、save_clipboard_image()(画像データ)… 呼び出し元
 */
std::optional<SavedImage> save_as_png(const QImage &image)
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

/**
 * @brief コピーされたファイル 1 つを保存する。
 *
 * @details
 * 判断の流れ:
 * 1. Qt で画像として読めるか確認 → 読めなければ対象外(exe など)
 * 2. OBS で表示できる形式で、回転も不要 → 元ファイルをそのままコピー
 * 3. それ以外 → 画像を読み込んで PNG に変換
 *
 * @param source コピーされたファイルのパス。
 * @return 保存した画像の情報。
 * @retval std::nullopt 画像ファイルでない・読めない・保存に失敗した。
 *
 * @see save_clipboard_image()(呼び出し元)
 */
std::optional<SavedImage> save_local_file(const QString &source)
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

/**
 * @brief クリップボードの画像を保存する。
 *
 * @details 仕様の詳細は clipboard-capture.hpp の宣言側を参照。
 *
 * @note 呼び出し元: plugin-main.cpp の on_show_requested()。
 * @note スレッド: UI スレッド。
 */
std::optional<SavedImage> save_clipboard_image()
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

/**
 * @brief 保存フォルダの古い画像を削除し、新しい順に MAX_SAVED_IMAGES 枚だけ残す。
 *
 * @details 仕様の詳細は clipboard-capture.hpp の宣言側を参照。
 */
void cleanup_saved_images(const std::vector<std::string> &in_use)
{
	const QString folder = image_folder();
	const QDir dir(folder);
	if (folder.isEmpty() || !dir.exists())
		return;

	/* このプラグインが付けた名前の形のファイルだけを対象にする(配信者が置いたファイルを消さないため) */
	const QStringList names = saved_names_newest_first(dir);
	if (names.size() <= MAX_SAVED_IMAGES)
		return;

	/* 表示中・差し替え待ちの画像は、古くても消さない(消すと OBS の表示が崩れるため) */
	QList<QFileInfo> keep;
	for (const std::string &path : in_use)
		keep.append(QFileInfo(QString::fromStdString(path)));

	int removed = 0;
	int skipped = 0;
	int failed = 0;
	for (qsizetype i = MAX_SAVED_IMAGES; i < names.size(); i++) {
		const QFileInfo info(dir.filePath(names[i]));
		if (std::any_of(keep.cbegin(), keep.cend(), [&info](const QFileInfo &k) { return k == info; })) {
			skipped++;
			continue;
		}
		if (QFile::remove(info.filePath())) {
			removed++;
		} else {
			failed++;
			obs_log(LOG_WARNING, "cleanup: failed to remove %s", info.filePath().toUtf8().constData());
		}
	}

	/* 使用中で残しただけのときは、貼るたびに同じ行が出ないようログを省く */
	if (removed > 0 || failed > 0)
		obs_log(LOG_INFO, "cleanup: removed %d old image(s), kept newest %d (in use: %d, failed: %d)", removed,
			MAX_SAVED_IMAGES, skipped, failed);
}

/**
 * @brief 保存フォルダのパスを返す。
 *
 * @details 仕様の詳細は clipboard-capture.hpp の宣言側を参照。
 */
std::string saved_images_folder()
{
	const QString folder = image_folder();
	if (folder.isEmpty())
		return std::string();
	return QDir::toNativeSeparators(folder).toUtf8().toStdString();
}

/**
 * @brief 保存した画像のパスを、新しい順に返す。
 *
 * @details 仕様の詳細は clipboard-capture.hpp の宣言側を参照。
 */
std::vector<std::string> list_saved_images()
{
	std::vector<std::string> paths;
	const QString folder = image_folder();
	const QDir dir(folder);
	if (folder.isEmpty() || !dir.exists())
		return paths;

	for (const QString &name : saved_names_newest_first(dir)) {
		paths.push_back(QDir::toNativeSeparators(dir.filePath(name)).toUtf8().toStdString());
		/* 掃除が終わる前に呼ばれても、上限より多くは返さない */
		if (static_cast<int>(paths.size()) >= MAX_SAVED_IMAGES)
			break;
	}
	return paths;
}

/**
 * @brief 保存した画像の情報(パスと幅・高さ)を読む。
 *
 * @details 仕様の詳細は clipboard-capture.hpp の宣言側を参照。
 */
std::optional<SavedImage> read_saved_image(const std::string &path)
{
	const QString file = QString::fromUtf8(path.c_str());
	QImageReader reader(file);
	/* 保存するときと同じく、回転情報を反映した大きさにする */
	reader.setAutoTransform(true);
	const QSize size = reader.canRead() ? reader.size() : QSize();
	if (!size.isValid()) {
		obs_log(LOG_WARNING, "history: cannot read %s (%s)", path.c_str(),
			reader.errorString().toUtf8().constData());
		return std::nullopt;
	}
	return SavedImage{path, size.width(), size.height()};
}

/**
 * @brief 保存した画像を、OS のごみ箱に送る。
 *
 * @details 仕様の詳細は clipboard-capture.hpp の宣言側を参照。
 */
bool trash_saved_image(const std::string &path)
{
	const QString folder = image_folder();
	const QFileInfo info(QString::fromUtf8(path.c_str()));

	/* 保存フォルダの直下にある、プラグインが付けた名前のファイルだけを対象にする(ほかのファイルを消さないため) */
	if (folder.isEmpty() || QFileInfo(info.absolutePath()) != QFileInfo(folder) ||
	    !saved_name_pattern().match(info.fileName()).hasMatch()) {
		obs_log(LOG_WARNING, "delete: refused %s (not a saved image)", path.c_str());
		return false;
	}

	/* 掃除などで既に消えていれば、消す物はない */
	if (!info.exists()) {
		obs_log(LOG_INFO, "delete: %s is already gone", path.c_str());
		return true;
	}

	/* ごみ箱に送れなければ、完全には消さない(確認なしで消すので、元に戻せない削除はしない) */
	QFile file(info.absoluteFilePath());
	if (!file.moveToTrash()) {
		obs_log(LOG_WARNING, "delete: failed to move %s to trash (%s); not deleted", path.c_str(),
			file.errorString().toUtf8().constData());
		return false;
	}
	obs_log(LOG_INFO, "delete: moved %s to trash", path.c_str());
	return true;
}
