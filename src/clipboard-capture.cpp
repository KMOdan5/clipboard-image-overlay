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

constexpr const char *IMAGE_FOLDER = "images";

/*
 * Formats OBS's image source can display. A copied file in one of these is
 * saved unchanged. WebP is left out because OBS cannot load animated WebP.
 */
const QStringList OBS_IMAGE_FORMATS = {
	QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
	QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tga"),
};

/* Folder for saved images, e.g. %APPDATA%\obs-studio\plugin_config\obs-clipboard-image\images */
QString image_folder()
{
	char *path = obs_module_config_path(IMAGE_FOLDER);
	QString folder = QString::fromUtf8(path ? path : "");
	bfree(path);
	return folder;
}

/* Returns a new, timestamped path in the image folder, or an empty string on failure. */
QString new_image_path(const QString &extension)
{
	const QString folder = image_folder();
	if (folder.isEmpty() || !QDir().mkpath(folder)) {
		obs_log(LOG_WARNING, "save: cannot create folder %s", folder.toUtf8().constData());
		return QString();
	}

	const QString name = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) +
			     QStringLiteral(".") + extension;
	return QDir(folder).filePath(name);
}

std::optional<std::string> saved(const QString &path, const QSize &size, const char *how)
{
	const QString native = QDir::toNativeSeparators(path);
	obs_log(LOG_INFO, "save: %dx%d (%s) -> %s", size.width(), size.height(), how, native.toUtf8().constData());
	return native.toUtf8().toStdString();
}

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
 * Saves one copied local file.
 *
 * Rule: keep the original file and extension, so GIF animation, quality and
 * file size are preserved.
 * Exceptions, re-encoded as PNG so the image always shows in OBS:
 *   - formats OBS's image source cannot display (see OBS_IMAGE_FORMATS)
 *   - photos with an EXIF orientation, which OBS would show unrotated
 *
 * Returns std::nullopt if the file is not an image.
 */
std::optional<std::string> save_local_file(const QString &source)
{
	QImageReader reader(source);
	reader.setAutoTransform(true);
	if (!reader.canRead()) {
		obs_log(LOG_INFO, "clipboard: skipped copied file %s (%s)", source.toUtf8().constData(),
			reader.errorString().toUtf8().constData());
		return std::nullopt;
	}

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

	const QImage image = reader.read();
	if (image.isNull()) {
		obs_log(LOG_INFO, "clipboard: failed to decode %s (%s)", source.toUtf8().constData(),
			reader.errorString().toUtf8().constData());
		return std::nullopt;
	}
	return save_as_png(image);
}

} // namespace

std::optional<std::string> save_clipboard_image()
{
	const QClipboard *clipboard = QGuiApplication::clipboard();
	const QMimeData *mime = clipboard ? clipboard->mimeData() : nullptr;
	if (!mime) {
		obs_log(LOG_INFO, "clipboard: unavailable");
		return std::nullopt;
	}

	/* Prefer a copied file: it keeps the original quality, transparency and animation. */
	if (mime->hasUrls()) {
		for (const QUrl &url : mime->urls()) {
			if (!url.isLocalFile())
				continue;
			if (auto path = save_local_file(url.toLocalFile()))
				return path;
		}
	}

	if (mime->hasImage()) {
		const QImage image = qvariant_cast<QImage>(mime->imageData());
		if (!image.isNull()) {
			obs_log(LOG_INFO, "clipboard: read image data");
			return save_as_png(image);
		}
	}

	obs_log(LOG_INFO, "clipboard: no image found (formats: %s)",
		mime->formats().join(QStringLiteral(", ")).toUtf8().constData());
	return std::nullopt;
}
