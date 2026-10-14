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
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QUrl>

namespace {

constexpr const char *IMAGE_FOLDER = "images";

/* Folder for saved images, e.g. %APPDATA%\obs-studio\plugin_config\obs-clipboard-image\images */
QString image_folder()
{
	char *path = obs_module_config_path(IMAGE_FOLDER);
	QString folder = QString::fromUtf8(path ? path : "");
	bfree(path);
	return folder;
}

/* Loads the first copied local file that Qt can read as an image. */
QImage image_from_local_files(const QMimeData *mime)
{
	if (!mime->hasUrls())
		return QImage();

	for (const QUrl &url : mime->urls()) {
		if (!url.isLocalFile())
			continue;

		const QString path = url.toLocalFile();
		QImageReader reader(path);
		reader.setAutoTransform(true);
		QImage image = reader.read();
		if (!image.isNull()) {
			obs_log(LOG_INFO, "clipboard: read copied file %s", path.toUtf8().constData());
			return image;
		}

		obs_log(LOG_INFO, "clipboard: skipped copied file %s (%s)", path.toUtf8().constData(),
			reader.errorString().toUtf8().constData());
	}

	return QImage();
}

QImage image_from_clipboard()
{
	const QClipboard *clipboard = QGuiApplication::clipboard();
	const QMimeData *mime = clipboard ? clipboard->mimeData() : nullptr;
	if (!mime) {
		obs_log(LOG_INFO, "clipboard: unavailable");
		return QImage();
	}

	/* Prefer a copied file: it keeps the original quality and transparency. */
	QImage image = image_from_local_files(mime);
	if (!image.isNull())
		return image;

	if (mime->hasImage()) {
		image = qvariant_cast<QImage>(mime->imageData());
		if (!image.isNull()) {
			obs_log(LOG_INFO, "clipboard: read image data");
			return image;
		}
	}

	obs_log(LOG_INFO, "clipboard: no image found (formats: %s)",
		mime->formats().join(QStringLiteral(", ")).toUtf8().constData());
	return QImage();
}

} // namespace

std::optional<std::string> save_clipboard_image()
{
	const QImage image = image_from_clipboard();
	if (image.isNull())
		return std::nullopt;

	const QString folder = image_folder();
	if (folder.isEmpty() || !QDir().mkpath(folder)) {
		obs_log(LOG_WARNING, "save: cannot create folder %s", folder.toUtf8().constData());
		return std::nullopt;
	}

	const QString name =
		QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) + QStringLiteral(".png");
	const QString path = QDir(folder).filePath(name);

	if (!image.save(path, "PNG")) {
		obs_log(LOG_WARNING, "save: failed to write %s", path.toUtf8().constData());
		return std::nullopt;
	}

	const QString native = QDir::toNativeSeparators(path);
	obs_log(LOG_INFO, "save: %dx%d -> %s", image.width(), image.height(), native.toUtf8().constData());
	return native.toUtf8().toStdString();
}
