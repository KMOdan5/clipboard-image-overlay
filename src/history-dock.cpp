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
 * @file history-dock.cpp
 * @brief 履歴ドック(#15)。保存フォルダの画像をサムネイルで並べ、クリックでもう一度表示する。
 *
 * @details
 * 処理の流れ:
 * @code
 *   保存フォルダの変化(QFileSystemWatcher)─→ 少し待って refresh()  一覧を読み直す
 *                                                 └→ show_page()      今のページのサムネイルを並べる
 *                                                      └→ request_thumbnail()  まだないサムネイルを裏で作る
 *   サムネイルをクリック ─→ on_selected(path)  表示は plugin-main.cpp が行う
 *   ◀ ▶ ボタン ─→ show_page()
 * @endcode
 *
 * 画面は Qt のウィジェットを組み合わせて作る(.ui ファイルや Q_OBJECT は使わない。部品が少ないため)。
 * 表示する文字列はすべて data/locale の ini から読む。
 *
 * 関連ファイル:
 * - clipboard-capture.cpp … 保存フォルダと、履歴の一覧(list_saved_images())
 * - plugin-main.cpp … ドックの登録と、クリックされた画像の表示
 *
 * 公開している関数の説明は history-dock.hpp を参照。
 */

#include "history-dock.hpp"
#include "clipboard-capture.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

/**
 * @brief ドックの内部 ID。
 * @warning OBS がドックの位置や表示/非表示をこの ID で保存するため、変更すると配置が初期化される。
 */
constexpr const char *DOCK_ID = "clipboard_image_overlay_history";

/// @brief 1 ページに並べる画像の枚数。ドックに置いたときの選びやすさを見て調整する(#15)。
constexpr int IMAGES_PER_PAGE = 10;

/// @brief ページ数の上限。IMAGES_PER_PAGE × MAX_PAGES を、保存の上限(50 枚)とそろえる。
constexpr int MAX_PAGES = 5;

/// @brief サムネイルの幅(ピクセル)。16:9 の画像がちょうど収まる大きさにする。
constexpr int THUMB_WIDTH = 128;

/// @brief サムネイルの高さ(ピクセル)。
constexpr int THUMB_HEIGHT = 72;

/// @brief サムネイルどうしの間隔(ピクセル)。
constexpr int THUMB_SPACING = 4;

/**
 * @brief フォルダの変化を受けてから一覧を読み直すまでの待ち時間(ミリ秒)。
 * @details 画像の保存と古い画像の掃除は続けて起きるので、まとめて 1 回で読み直すため。
 */
constexpr int REFRESH_DELAY_MS = 200;

/// @brief サムネイルを同時に作るスレッドの数。OBS の処理を邪魔しないよう少なくする。
constexpr int THUMBNAIL_THREADS = 2;

/// @brief 一覧の項目に、画像ファイルのパスを持たせるときの役割番号。
constexpr int PATH_ROLE = Qt::UserRole;

/**
 * @brief 翻訳した文字列を Qt の文字列で返す。
 * @param key data/locale の ini のキー。
 * @return 翻訳した文字列。
 */
QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/**
 * @brief ファイル名(yyyyMMdd-HHmmss-zzz.拡張子)から、保存した日時の表示用の文字列を作る。
 *
 * @param path 画像ファイルのパス。
 * @return 日時(例: 10/20 14:03)。名前から読めなければファイル名をそのまま返す。
 */
QString saved_time_label(const QString &path)
{
	const QString name = QFileInfo(path).completeBaseName();
	const QDateTime time = QDateTime::fromString(name.left(15), QStringLiteral("yyyyMMdd-HHmmss"));
	if (!time.isValid())
		return QFileInfo(path).fileName();
	return time.toString(QStringLiteral("M/d HH:mm"));
}

/**
 * @brief 画像ファイルから、サムネイルの画像を作る。
 *
 * @details
 * 大きさは THUMB_WIDTH × THUMB_HEIGHT に収まるよう縮小し(縦横比は保つ、拡大はしない)、
 * 同じ大きさの透明な画像の中央に置く。一覧の並びがそろうようにするため。
 * 読み込むときに縮小を指定するので、JPEG などは元の大きさで読むより速い。
 * GIF は最初の 1 コマを使う。
 *
 * @param path  画像ファイルのパス。
 * @param scale 画面の拡大率(高解像度の画面でぼやけないよう、その分大きく作る)。
 * @return サムネイル。読めなければ空の画像。
 *
 * @note スレッド: 裏のスレッドで呼ぶ(QImage と QPainter は UI スレッド以外でも使える)。
 */
QImage make_thumbnail(const QString &path, qreal scale)
{
	QImageReader reader(path);
	reader.setAutoTransform(true);
	const QSize source = reader.size();
	const QSize box(qRound(THUMB_WIDTH * scale), qRound(THUMB_HEIGHT * scale));
	if (source.isValid()) {
		/* 小さい画像は拡大しない(ぼやけるため) */
		const QSize target = (source.width() <= box.width() && source.height() <= box.height())
					     ? source
					     : source.scaled(box, Qt::KeepAspectRatio);
		reader.setScaledSize(target.expandedTo(QSize(1, 1)));
	}
	const QImage image = reader.read();
	if (image.isNull())
		return QImage();

	QImage canvas(box, QImage::Format_ARGB32_Premultiplied);
	canvas.fill(Qt::transparent);
	QPainter painter(&canvas);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	const QSize drawn = image.size().boundedTo(box);
	painter.drawImage(QRect(QPoint((box.width() - drawn.width()) / 2, (box.height() - drawn.height()) / 2), drawn),
			  image);
	painter.end();
	canvas.setDevicePixelRatio(scale);
	return canvas;
}

/**
 * @brief 履歴ドックの中身(ウィジェット)。
 *
 * @details 一覧・画像がないときの案内・ページの切り替えの 3 つの部品からなる。
 *          OBS がドックとして包んで表示する(このウィジェットの持ち主は OBS)。
 */
class HistoryWidget : public QWidget {
public:
	/**
	 * @brief 部品を作り、保存フォルダの見張りを始める。
	 * @param on_selected サムネイルがクリックされたときに呼ぶ処理。
	 */
	explicit HistoryWidget(HistorySelectedCallback on_selected) : on_selected_(std::move(on_selected))
	{
		thumbnail_pool_.setMaxThreadCount(THUMBNAIL_THREADS);

		/* 一覧: ドックの幅に合わせてサムネイルを折り返して並べる */
		list_ = new QListWidget(this);
		list_->setViewMode(QListView::IconMode);
		list_->setFlow(QListView::LeftToRight);
		list_->setWrapping(true);
		list_->setResizeMode(QListView::Adjust);
		list_->setMovement(QListView::Static);
		list_->setUniformItemSizes(true);
		list_->setSelectionMode(QAbstractItemView::NoSelection);
		list_->setIconSize(QSize(THUMB_WIDTH, THUMB_HEIGHT));
		list_->setGridSize(QSize(THUMB_WIDTH + THUMB_SPACING * 2, THUMB_HEIGHT + THUMB_SPACING * 2));
		list_->setMouseTracking(true);
		QObject::connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
			const QString path = item->data(PATH_ROLE).toString();
			obs_log(LOG_INFO, "history: clicked %s", path.toUtf8().constData());
			if (on_selected_)
				on_selected_(path.toUtf8().toStdString());
		});

		/* 画像がまだないときの案内 */
		empty_label_ = new QLabel(text("Dock.Empty"), this);
		empty_label_->setAlignment(Qt::AlignCenter);
		empty_label_->setWordWrap(true);

		/* ページの切り替え: ◀ 1 / 5 ▶ */
		prev_button_ = new QToolButton(this);
		prev_button_->setArrowType(Qt::LeftArrow);
		prev_button_->setToolTip(text("Dock.PrevPage"));
		QObject::connect(prev_button_, &QToolButton::clicked, this, [this]() { show_page(page_ - 1); });
		next_button_ = new QToolButton(this);
		next_button_->setArrowType(Qt::RightArrow);
		next_button_->setToolTip(text("Dock.NextPage"));
		QObject::connect(next_button_, &QToolButton::clicked, this, [this]() { show_page(page_ + 1); });
		page_label_ = new QLabel(this);
		page_label_->setAlignment(Qt::AlignCenter);

		pager_ = new QWidget(this);
		auto *pager_layout = new QHBoxLayout(pager_);
		pager_layout->setContentsMargins(0, 0, 0, 0);
		pager_layout->addStretch();
		pager_layout->addWidget(prev_button_);
		pager_layout->addWidget(page_label_);
		pager_layout->addWidget(next_button_);
		pager_layout->addStretch();

		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(THUMB_SPACING, THUMB_SPACING, THUMB_SPACING, THUMB_SPACING);
		layout->addWidget(list_, 1);
		layout->addWidget(empty_label_, 1);
		layout->addWidget(pager_);

		/*
		 * 保存フォルダの変化を見張る。フォルダがまだないと見張れないので、ここで作っておく
		 * (最初の画像を保存するときにも作られるフォルダなので、先に作っても害はない)
		 */
		refresh_timer_.setSingleShot(true);
		refresh_timer_.setInterval(REFRESH_DELAY_MS);
		QObject::connect(&refresh_timer_, &QTimer::timeout, this, [this]() { refresh(); });
		const QString folder = QString::fromUtf8(saved_images_folder().c_str());
		if (!folder.isEmpty() && QDir().mkpath(folder) && watcher_.addPath(folder)) {
			QObject::connect(&watcher_, &QFileSystemWatcher::directoryChanged, this,
					 [this](const QString &) { refresh_timer_.start(); });
		} else {
			obs_log(LOG_WARNING, "history: cannot watch folder %s", folder.toUtf8().constData());
		}

		refresh();
	}

	~HistoryWidget() override { shutdown(); }

	/**
	 * @brief フォルダの見張りとサムネイルの作成を止める。作成中のサムネイルは終わるまで待つ。何度呼んでもよい。
	 */
	void shutdown()
	{
		stopped_ = true;
		refresh_timer_.stop();
		if (!watcher_.directories().isEmpty())
			watcher_.removePaths(watcher_.directories());
		thumbnail_pool_.clear();
		thumbnail_pool_.waitForDone();
	}

private:
	/**
	 * @brief 保存フォルダの画像を読み直し、一覧を作り直す。
	 *
	 * @details
	 * 新しい画像が増えていたら 1 ページ目に戻る(貼った画像がすぐ見えるように)。
	 * 一覧から消えた画像のサムネイルは捨てる。
	 */
	void refresh()
	{
		if (stopped_)
			return;

		paths_.clear();
		for (const std::string &path : list_saved_images())
			paths_.append(QString::fromUtf8(path.c_str()));
		while (paths_.size() > IMAGES_PER_PAGE * MAX_PAGES)
			paths_.removeLast();

		const QString newest = paths_.isEmpty() ? QString() : paths_.first();
		if (newest != newest_) {
			newest_ = newest;
			page_ = 0;
		}

		/* 一覧にない画像のサムネイルは、もう使わないので捨てる */
		const QSet<QString> alive(paths_.cbegin(), paths_.cend());
		for (auto it = thumbnails_.begin(); it != thumbnails_.end();) {
			if (alive.contains(it.key()))
				++it;
			else
				it = thumbnails_.erase(it);
		}

		show_page(page_);
	}

	/// @brief 今の一覧のページ数を返す(画像がなくても 1)。
	int page_count() const
	{
		const int pages = static_cast<int>((paths_.size() + IMAGES_PER_PAGE - 1) / IMAGES_PER_PAGE);
		return std::clamp(pages, 1, MAX_PAGES);
	}

	/**
	 * @brief 指定したページのサムネイルを並べる。範囲外のページは端のページにする。
	 * @param page 0 から始まるページ番号。
	 */
	void show_page(int page)
	{
		const int pages = page_count();
		page_ = std::clamp(page, 0, pages - 1);

		list_->clear();
		const qsizetype first = static_cast<qsizetype>(page_) * IMAGES_PER_PAGE;
		const qsizetype last = std::min(first + IMAGES_PER_PAGE, paths_.size());
		for (qsizetype i = first; i < last; i++) {
			const QString &path = paths_[i];
			auto *item = new QListWidgetItem(list_);
			item->setData(PATH_ROLE, path);
			item->setToolTip(saved_time_label(path));
			item->setSizeHint(list_->gridSize());
			if (thumbnails_.contains(path))
				item->setIcon(QIcon(thumbnails_.value(path)));
			else
				request_thumbnail(path);
		}

		const bool empty = paths_.isEmpty();
		list_->setVisible(!empty);
		empty_label_->setVisible(empty);
		pager_->setVisible(pages > 1);
		page_label_->setText(QStringLiteral("%1 / %2").arg(page_ + 1).arg(pages));
		prev_button_->setEnabled(page_ > 0);
		next_button_->setEnabled(page_ < pages - 1);
	}

	/**
	 * @brief サムネイルを裏のスレッドで作るよう依頼する。作成中・作成済みなら何もしない。
	 *
	 * @details
	 * 大きなスクリーンショットの読み込みには時間がかかるため、UI スレッドで作ると OBS の画面が止まる。
	 * できあがったら UI スレッドに戻して on_thumbnail_ready() を呼ぶ。
	 * その時点でドックが消えていれば何もしない(QPointer で確かめる)。
	 *
	 * @param path 画像ファイルのパス。
	 */
	void request_thumbnail(const QString &path)
	{
		if (stopped_ || loading_.contains(path))
			return;
		loading_.insert(path);

		const QPointer<HistoryWidget> self(this);
		const qreal scale = devicePixelRatioF();
		thumbnail_pool_.start([self, path, scale]() {
			const QImage thumbnail = make_thumbnail(path, scale);
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[self, path, thumbnail]() {
					if (self)
						self->on_thumbnail_ready(path, thumbnail);
				},
				Qt::QueuedConnection);
		});
	}

	/**
	 * @brief できあがったサムネイルを、一覧の該当する項目に付ける。
	 * @param path      画像ファイルのパス。
	 * @param thumbnail サムネイル(読めなかった場合は空)。
	 */
	void on_thumbnail_ready(const QString &path, const QImage &thumbnail)
	{
		loading_.remove(path);
		if (stopped_)
			return;
		if (thumbnail.isNull()) {
			/* 掃除で消えた直後などは読めないことがある。一覧から消えれば、それで片付く */
			obs_log(LOG_INFO, "history: cannot make thumbnail for %s", path.toUtf8().constData());
			return;
		}
		/* 作っている間に一覧から消えた画像なら、取っておかない */
		if (!paths_.contains(path))
			return;

		const QPixmap pixmap = QPixmap::fromImage(thumbnail);
		thumbnails_.insert(path, pixmap);
		for (int i = 0; i < list_->count(); i++) {
			QListWidgetItem *item = list_->item(i);
			if (item->data(PATH_ROLE).toString() == path)
				item->setIcon(QIcon(pixmap));
		}
	}

	/// @brief サムネイルがクリックされたときに呼ぶ処理。
	HistorySelectedCallback on_selected_;
	/// @brief サムネイルの一覧。
	QListWidget *list_ = nullptr;
	/// @brief 画像がまだないときの案内。
	QLabel *empty_label_ = nullptr;
	/// @brief ページの切り替え(◀ 1 / 5 ▶)をまとめた部品。1 ページしかないときは隠す。
	QWidget *pager_ = nullptr;
	/// @brief 前のページのボタン。
	QToolButton *prev_button_ = nullptr;
	/// @brief 次のページのボタン。
	QToolButton *next_button_ = nullptr;
	/// @brief ページ番号(1 / 5)。
	QLabel *page_label_ = nullptr;

	/// @brief 一覧に出す画像のパス。先頭が一番新しい。
	QStringList paths_;
	/// @brief 前回読み直したときの一番新しい画像。増えたことに気づいて 1 ページ目に戻すため。
	QString newest_;
	/// @brief 今のページ(0 から)。
	int page_ = 0;
	/// @brief 作ったサムネイル。一覧に残っている画像の分だけ持つ。
	QHash<QString, QPixmap> thumbnails_;
	/// @brief 作成中のサムネイルのパス。同じ画像を二重に作らないため。
	QSet<QString> loading_;
	/// @brief true なら止めた後。届いた通知やサムネイルを処理しない。
	bool stopped_ = false;

	/// @brief 保存フォルダの見張り。
	QFileSystemWatcher watcher_;
	/// @brief フォルダの変化をまとめて 1 回で読み直すためのタイマー。
	QTimer refresh_timer_;
	/**
	 * @brief サムネイルを作るスレッド。
	 * @details OBS 全体で共有のスレッドではなく専用のものを使う。ドックを消すときに、
	 *          このプラグインの処理が残っていないことを確かめて(待って)から消すため。
	 */
	QThreadPool thumbnail_pool_;
};

/// @brief 登録したドックの中身。OBS が先に消した場合は自動で空になる。
QPointer<HistoryWidget> dock_widget;

} // namespace

/**
 * @brief 履歴ドックを作り、OBS に登録する。
 *
 * @details 仕様の詳細は history-dock.hpp の宣言側を参照。
 */
void history_dock_register(HistorySelectedCallback on_selected)
{
	if (dock_widget)
		return;

	auto *widget = new HistoryWidget(std::move(on_selected));
	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), widget)) {
		obs_log(LOG_WARNING, "history: failed to add dock");
		delete widget;
		return;
	}
	dock_widget = widget;
	obs_log(LOG_INFO, "history: dock added");
}

/**
 * @brief 履歴ドックの裏の処理を止める。
 *
 * @details 仕様の詳細は history-dock.hpp の宣言側を参照。
 */
void history_dock_shutdown()
{
	if (dock_widget)
		dock_widget->shutdown();
}

/**
 * @brief 履歴ドックを OBS から外す。
 *
 * @details 仕様の詳細は history-dock.hpp の宣言側を参照。
 */
void history_dock_unregister()
{
	/* OBS が終了時に先にドックを片付けていれば、中身は既に消えている */
	if (!dock_widget)
		return;
	dock_widget->shutdown();
	obs_frontend_remove_dock(DOCK_ID);
	dock_widget = nullptr;
	obs_log(LOG_INFO, "history: dock removed");
}
