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
 *                                                 └→ request_thumbnail()  まだないサムネイルを裏で作る
 *   ドックの大きさの変化 ─→ update_layout()  1 行の枚数とサムネイルの大きさを決め直す
 *   サムネイルをクリック ─→ on_selected(path)  表示は plugin-main.cpp が行う
 *   一定時間ごと(ドックが見えている間)─→ update_current()  表示中の画像のカードを目立たせる
 *   ドックが見えるようになった ─→ showEvent()  見えていない間の変化に追いつく
 * @endcode
 *
 * 一覧の読み込み・サムネイルの作成・表示中の画像の問い合わせは、ドックが見えている間だけ行う。
 *
 * 並べ方: 保存している画像(最大 50 枚)を新しい順に並べ、多いときは縦にスクロールする。
 * 1 行の枚数はドックの幅から決め、サムネイルは THUMB_MIN_WIDTH〜THUMB_MAX_WIDTH の間で、
 * ドックの幅をすき間なく埋める大きさにする。ドックを広げると大きく見え、狭めると 1 行の枚数が減る。
 * 1 枚ごとに枠付きのカードに載せ(ThumbnailDelegate)、マウスを乗せると色を変えてクリックできることを示す。
 * 'Clipboard Image' で表示中の画像のカードは、太い枠と濃い背景にして、今どれが出ているかを示す。
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
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QFileSystemWatcher>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QHideEvent>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QThreadPool>
#include <QTimer>
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

/// @brief 一覧に並べる画像の最大枚数。保存の上限(50 枚、clipboard-capture.cpp)とそろえる。
constexpr int MAX_IMAGES = 50;

/**
 * @brief サムネイルの幅の最小値(ピクセル)。ドックが狭くても、何の画像か見分けられる大きさ。
 * @details 1 行の枚数は、この幅のサムネイルが何枚入るかで決める。
 *          そのため実際の幅は、この値から 2 倍未満の間になる(THUMB_MAX_WIDTH で頭打ち)。
 */
constexpr int THUMB_MIN_WIDTH = 120;

/// @brief サムネイルの幅の最大値(ピクセル)。ドックを 1 列のまま広げても、大きくなりすぎないように。
constexpr int THUMB_MAX_WIDTH = 240;

/// @brief サムネイルの縦横比(幅 ÷ 高さ)。配信画面と同じ 16:9 にする。
constexpr double THUMB_ASPECT = 16.0 / 9.0;

/// @brief カードどうしの間隔(ピクセル)。余白は 8 の倍数を基本にそろえる。
constexpr int CARD_GAP = 8;

/// @brief カードの枠と画像の間の余白(ピクセル)。カードの中で画像がひとまとまりに見えるよう、間隔より狭くする。
constexpr int CARD_PADDING = 4;

/// @brief カードの角の丸み(ピクセル)。
constexpr int CARD_RADIUS = 4;

/**
 * @brief フォルダの変化を受けてから一覧を読み直すまでの待ち時間(ミリ秒)。
 * @details 画像の保存と古い画像の掃除は続けて起きるので、まとめて 1 回で読み直すため。
 */
constexpr int REFRESH_DELAY_MS = 200;

/// @brief サムネイルを同時に作るスレッドの数。OBS の処理を邪魔しないよう少なくする。
constexpr int THUMBNAIL_THREADS = 2;

/// @brief 一覧の項目に、画像ファイルのパスを持たせるときの役割番号。
constexpr int PATH_ROLE = Qt::UserRole;

/// @brief 一覧の項目に、表示中の画像かどうか(bool)を持たせるときの役割番号。
constexpr int CURRENT_ROLE = Qt::UserRole + 1;

/**
 * @brief 表示中の画像を問い合わせる間隔(ミリ秒)。
 * @details 表示/非表示は、ホットキー・ドック・自動で非表示にするタイマー・目のアイコンなど多くの経路で変わる。
 *          経路ごとに知らせを受けると漏れが出やすいため、ドックの側から定期的に問い合わせる。
 *          問い合わせはシーンを一通り見るだけで軽く、ドックが見えている間しか行わない。
 *          間隔は、クリックしてから枠が移るまでの遅れが気にならない長さにする。
 */
constexpr int CURRENT_POLL_MS = 250;

/// @brief 表示中の画像のカードの枠の太さ(ピクセル)。マウスを乗せたとき(1 ピクセル)と見分けられる太さ。
constexpr qreal CURRENT_BORDER_WIDTH = 3.0;

/// @brief 表示中の画像のカードの背景の濃さ(0〜255)。マウスを乗せたとき(HOVER_BACKGROUND_ALPHA)より濃くする。
constexpr int CURRENT_BACKGROUND_ALPHA = 96;

/// @brief マウスを乗せたカードの背景の濃さ(0〜255)。
constexpr int HOVER_BACKGROUND_ALPHA = 48;

/// @brief 「表示中」のラベルの、カードの角からの距離(ピクセル)。太い枠に重ならない位置にする。
constexpr qreal BADGE_MARGIN = 6.0;

/// @brief 「表示中」のラベルの、文字の左右の余白(ピクセル)。
constexpr qreal BADGE_PADDING_X = 6.0;

/// @brief 「表示中」のラベルの、文字の上下の余白(ピクセル)。
constexpr qreal BADGE_PADDING_Y = 2.0;

/// @brief 「表示中」のラベルの文字の大きさ(一覧の文字に対する倍率)。カードの中で画像より目立ちすぎないよう、少し小さくする。
constexpr qreal BADGE_FONT_SCALE = 0.85;

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
 * THUMB_MAX_WIDTH の大きさの枠に収まるよう縮小する(縦横比は保つ、拡大はしない)。
 * ドックの幅に合わせた実際の大きさへの縮小は、表示するときに行う(ThumbnailDelegate)。
 * 最大の大きさで 1 回だけ作っておけば、ドックの大きさを変えても作り直さずに済むため。
 * 読み込むときに縮小を指定するので、JPEG などは元の大きさで読むより速い。
 * GIF アニメは最初の 1 コマだけを使い、サムネイルでは動かさない(一覧が動くと気が散り、全コマを読むと重くなるため)。
 *
 * @param path  画像ファイルのパス。
 * @param scale 画面の拡大率(高解像度の画面でぼやけないよう、その分大きく作る)。
 * @return サムネイル。読めなければ空の画像。
 *
 * @note スレッド: 裏のスレッドで呼ぶ(QImage は UI スレッド以外でも使える)。
 */
QImage make_thumbnail(const QString &path, qreal scale)
{
	QImageReader reader(path);
	reader.setAutoTransform(true);
	const QSize source = reader.size();
	const QSize box(qRound(THUMB_MAX_WIDTH * scale), qRound(THUMB_MAX_WIDTH / THUMB_ASPECT * scale));
	if (source.isValid() && (source.width() > box.width() || source.height() > box.height()))
		reader.setScaledSize(source.scaled(box, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));

	QImage image = reader.read();
	if (image.isNull())
		return QImage();
	image.setDevicePixelRatio(scale);
	return image;
}

/**
 * @brief サムネイルを、枠付きのカードに載せて描く。
 *
 * @details
 * 背景に並べただけだと 1 枚の範囲が分かりにくい(透明な画像や細長い画像で特に)ため、
 * 1 枚ごとにカードを敷いて枠を付ける。カードの見た目は 3 通り:
 * - 通常: 背景色のカードに、目立たない枠
 * - マウスを乗せたとき: 選択色の細い枠と薄い背景(クリックできることを示す)
 * - 表示中の画像: 選択色の太い枠と濃い背景に、左上の「表示中」のラベル(マウスを乗せても変えない。今出ている画像を見失わないように)
 *
 * 枠だけだと「選択した画像」に見え、自動で非表示になったときに「選択が外れた」と受け取られやすい。
 * ラベルで「画面に出ている」ことを示し、消えたときは「表示が終わった」と読めるようにする。
 *
 * 色はすべて Qt の配色(OBS のテーマ)から取るので、ダーク・ライトどちらのテーマでも浮かない。
 * 画像はカードの中央に、縦横比を保って描く。サムネイルの作成中は、カードだけを描く。
 */
class ThumbnailDelegate : public QStyledItemDelegate {
public:
	/**
	 * @brief 描き方を作る。
	 * @param parent 持ち主(一覧)。
	 */
	explicit ThumbnailDelegate(QObject *parent) : QStyledItemDelegate(parent), badge_text_(text("Dock.Showing")) {}

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);

		/* 隣のカードと間隔を空けるため、マス目より一回り小さく描く */
		const QRectF card = QRectF(option.rect)
					    .adjusted(CARD_GAP / 2.0 + 0.5, CARD_GAP / 2.0 + 0.5, -CARD_GAP / 2.0 - 0.5,
						      -CARD_GAP / 2.0 - 0.5);
		const bool hovered = option.state & QStyle::State_MouseOver;
		const bool current = index.data(CURRENT_ROLE).toBool();
		const QPalette &palette = option.palette;
		const QColor highlight = palette.color(QPalette::Highlight);

		QColor background = palette.color(QPalette::Base);
		QColor border = palette.color(QPalette::Mid);
		qreal border_width = 1.0;
		if (current) {
			border = highlight;
			background = highlight;
			background.setAlpha(CURRENT_BACKGROUND_ALPHA);
			border_width = CURRENT_BORDER_WIDTH;
		} else if (hovered) {
			border = highlight;
			background = highlight;
			background.setAlpha(HOVER_BACKGROUND_ALPHA);
		}
		/* 太い枠がカードの外へはみ出さないよう、太くした分だけ内側に描く */
		const qreal inset = (border_width - 1.0) / 2.0;
		painter->setPen(QPen(border, border_width));
		painter->setBrush(background);
		painter->drawRoundedRect(card.adjusted(inset, inset, -inset, -inset), CARD_RADIUS, CARD_RADIUS);

		const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
		if (!icon.isNull()) {
			const QRect area =
				card.toAlignedRect().adjusted(CARD_PADDING, CARD_PADDING, -CARD_PADDING, -CARD_PADDING);
			icon.paint(painter, area, Qt::AlignCenter);
		}
		if (current)
			paint_badge(painter, option, card);
		painter->restore();
	}

private:
	/**
	 * @brief カードの左上に「表示中」のラベルを描く。画像に重なっても読めるよう、選択色の地に文字を載せる。
	 * @param painter 描く先。
	 * @param option  一覧の文字の大きさと配色。
	 * @param card    カードの範囲。
	 */
	void paint_badge(QPainter *painter, const QStyleOptionViewItem &option, const QRectF &card) const
	{
		QFont font = option.font;
		font.setBold(true);
		font.setPointSizeF(font.pointSizeF() * BADGE_FONT_SCALE);
		const QFontMetricsF metrics(font);
		const QRectF badge(card.left() + BADGE_MARGIN, card.top() + BADGE_MARGIN,
				   metrics.horizontalAdvance(badge_text_) + BADGE_PADDING_X * 2,
				   metrics.height() + BADGE_PADDING_Y * 2);

		painter->setPen(Qt::NoPen);
		painter->setBrush(option.palette.color(QPalette::Highlight));
		painter->drawRoundedRect(badge, CARD_RADIUS - 1, CARD_RADIUS - 1);
		painter->setFont(font);
		painter->setPen(option.palette.color(QPalette::HighlightedText));
		painter->drawText(badge, Qt::AlignCenter, badge_text_);
	}

	/// @brief 「表示中」のラベルの文字。描くたびに翻訳を引かないよう、作るときに読んでおく。
	QString badge_text_;
};

/**
 * @brief 履歴ドックの中身(ウィジェット)。
 *
 * @details サムネイルの一覧と、画像がないときの案内の 2 つの部品からなる。
 *          OBS がドックとして包んで表示する(このウィジェットの持ち主は OBS)。
 */
class HistoryWidget : public QWidget {
public:
	/**
	 * @brief 部品を作り、保存フォルダの見張りを始める。
	 * @param on_selected サムネイルがクリックされたときに呼ぶ処理。
	 * @param current     表示中の画像を問い合わせる処理。
	 */
	HistoryWidget(HistorySelectedCallback on_selected, HistoryCurrentCallback current)
		: on_selected_(std::move(on_selected)),
		  current_(std::move(current))
	{
		thumbnail_pool_.setMaxThreadCount(THUMBNAIL_THREADS);

		/* 一覧: ドックの幅に合わせて折り返して並べ、多いときは縦にスクロールする */
		list_ = new QListWidget(this);
		list_->setViewMode(QListView::IconMode);
		list_->setFlow(QListView::LeftToRight);
		list_->setWrapping(true);
		list_->setResizeMode(QListView::Adjust);
		list_->setMovement(QListView::Static);
		list_->setUniformItemSizes(true);
		list_->setSelectionMode(QAbstractItemView::NoSelection);
		list_->setFrameShape(QFrame::NoFrame);
		list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		list_->setItemDelegate(new ThumbnailDelegate(list_));
		list_->setMouseTracking(true);
		/* マウスを乗せたカードの色を変えるため(ThumbnailDelegate)、乗せた位置を一覧に知らせる */
		list_->viewport()->setAttribute(Qt::WA_Hover);
		list_->viewport()->setCursor(Qt::PointingHandCursor);
		list_->installEventFilter(this);
		QObject::connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
			const QString path = item->data(PATH_ROLE).toString();
			obs_log(LOG_INFO, "history: clicked %s", path.toUtf8().constData());
			if (on_selected_)
				on_selected_(path.toUtf8().toStdString());
			/* 次の問い合わせを待たずに、クリックした画像へ枠を移す */
			update_current();
		});

		/* 画像がまだないときの案内 */
		empty_label_ = new QLabel(text("Dock.Empty"), this);
		empty_label_->setAlignment(Qt::AlignCenter);
		empty_label_->setWordWrap(true);

		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(list_, 1);
		layout->addWidget(empty_label_, 1);

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

		/*
		 * 表示中の画像を定期的に問い合わせる(CURRENT_POLL_MS の説明を参照)。
		 * 問い合わせと一覧の読み込みは、ドックが見えている間だけ行う(showEvent() で始め、hideEvent() で止める)
		 */
		current_timer_.setInterval(CURRENT_POLL_MS);
		QObject::connect(&current_timer_, &QTimer::timeout, this, [this]() { update_current(); });

		update_layout();
		needs_refresh_ = true;
	}

	~HistoryWidget() override { shutdown(); }

	/**
	 * @brief フォルダの見張りとサムネイルの作成を止める。作成中のサムネイルは終わるまで待つ。何度呼んでもよい。
	 */
	void shutdown()
	{
		stopped_ = true;
		refresh_timer_.stop();
		current_timer_.stop();
		if (!watcher_.directories().isEmpty())
			watcher_.removePaths(watcher_.directories());
		thumbnail_pool_.clear();
		thumbnail_pool_.waitForDone();
	}

protected:
	/**
	 * @brief ドックが見えるようになったら、見えていない間に溜まった変化に追いつき、表示中の画像の問い合わせを始める。
	 *
	 * @details ドックを閉じている・別のドックのタブの裏にあるときは、一覧の読み込み・サムネイルの作成・
	 *          表示中の画像の問い合わせをしない(配信中の OBS に、見えない画面のための処理をさせないため)。
	 *          保存フォルダの見張り自体は続ける(変化があったことだけを覚えておき、見えたときに読み直す)。
	 */
	void showEvent(QShowEvent *event) override
	{
		QWidget::showEvent(event);
		if (stopped_)
			return;
		if (needs_refresh_)
			refresh(); /* 表示中の画像の印も付け直される */
		else
			update_current(true);
		current_timer_.start();
	}

	/**
	 * @brief ドックが見えなくなったら、表示中の画像の問い合わせを止める。
	 */
	void hideEvent(QHideEvent *event) override
	{
		QWidget::hideEvent(event);
		current_timer_.stop();
	}

	/**
	 * @brief 一覧の大きさが変わったら、1 行の枚数とサムネイルの大きさを決め直す。
	 */
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (watched == list_ && event->type() == QEvent::Resize)
			update_layout();
		return QWidget::eventFilter(watched, event);
	}

private:
	/**
	 * @brief ドックの幅から、1 行の枚数とサムネイルの大きさを決める。
	 *
	 * @details
	 * 1. THUMB_MIN_WIDTH のカードが何枚入るかで、1 行の枚数を決める(最低 1 枚)
	 * 2. その枚数で幅をすき間なく分け、カードの幅にする(THUMB_MAX_WIDTH で頭打ち)
	 * 3. 高さは 16:9 の比で決める
	 *
	 * 縦のスクロールバーの幅は、出ていなくても先に差し引いておく。
	 * スクロールバーが出たり消えたりするたびに枚数が変わり、並びが揺れるのを防ぐため。
	 */
	void update_layout()
	{
		const int scrollbar = list_->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, list_);
		const int available = std::max(1, list_->width() - scrollbar - 1);

		const int min_cell = THUMB_MIN_WIDTH + CARD_PADDING * 2 + CARD_GAP;
		const int columns = std::max(1, available / min_cell);
		const int cell_width = std::min(available / columns, THUMB_MAX_WIDTH + CARD_PADDING * 2 + CARD_GAP);
		const int thumb_width = cell_width - CARD_PADDING * 2 - CARD_GAP;
		const int thumb_height = qRound(thumb_width / THUMB_ASPECT);
		const QSize cell(cell_width, thumb_height + CARD_PADDING * 2 + CARD_GAP);

		if (cell == list_->gridSize())
			return;
		list_->setIconSize(QSize(thumb_width, thumb_height));
		list_->setGridSize(cell);
		for (int i = 0; i < list_->count(); i++)
			list_->item(i)->setSizeHint(cell);
	}

	/**
	 * @brief 保存フォルダの画像を読み直し、一覧を作り直す。
	 *
	 * @details
	 * 新しい画像が増えていたら一番上までスクロールする(貼った画像がすぐ見えるように)。
	 * それ以外はスクロールの位置を保つ。一覧から消えた画像のサムネイルは捨てる。
	 * ドックが見えていなければ読み直さず、見えたときに読み直す(showEvent())。
	 */
	void refresh()
	{
		if (stopped_)
			return;
		if (!isVisible()) {
			needs_refresh_ = true;
			return;
		}
		needs_refresh_ = false;

		QStringList paths;
		for (const std::string &path : list_saved_images())
			paths.append(QString::fromUtf8(path.c_str()));
		while (paths.size() > MAX_IMAGES)
			paths.removeLast();
		if (paths == paths_)
			return;
		paths_ = paths;

		const QString newest = paths_.isEmpty() ? QString() : paths_.first();
		const bool has_new_image = newest != newest_;
		newest_ = newest;
		const int scroll = list_->verticalScrollBar()->value();

		/* 一覧にない画像のサムネイルは、もう使わないので捨てる */
		const QSet<QString> alive(paths_.cbegin(), paths_.cend());
		for (auto it = thumbnails_.begin(); it != thumbnails_.end();) {
			if (alive.contains(it.key()))
				++it;
			else
				it = thumbnails_.erase(it);
		}

		list_->clear();
		for (const QString &path : paths_) {
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
		if (has_new_image)
			list_->scrollToTop();
		else
			list_->verticalScrollBar()->setValue(scroll);

		/* 作り直した項目には印がないので、表示中の画像に付け直す */
		update_current(true);
	}

	/**
	 * @brief 表示中の画像を問い合わせ、そのカードに印を付ける(印は ThumbnailDelegate が描く)。
	 *
	 * @details
	 * 表示中の画像が前回と同じなら何もしない(ほとんどの問い合わせはここで終わる)。
	 * 変わっていたら、一覧の中から同じファイルを探して印を付け直す。
	 * 'Clipboard Image' に設定されたパスと一覧のパスは区切り文字の向きなどが違うことがあるため、
	 * 文字列ではなくファイルとして比べる。一覧にない画像(掃除で消えた・配信者が別の画像を設定した)なら、どれにも印を付けない。
	 * ドックが見えていない間は問い合わせない(見えたときに追いつく。showEvent())。
	 *
	 * @param force true なら、表示中の画像が前回と同じでも印を付け直す(一覧を作り直した後・ドックが見えたとき)。
	 */
	void update_current(bool force = false)
	{
		if (stopped_ || !current_ || !isVisible())
			return;

		const QString current = QString::fromUtf8(current_().c_str());
		if (!force && current == current_raw_)
			return;
		current_raw_ = current;

		const QFileInfo current_file(current);
		bool found = false;
		for (int i = 0; i < list_->count(); i++) {
			QListWidgetItem *item = list_->item(i);
			const bool is_current = !found && !current.isEmpty() &&
						QFileInfo(item->data(PATH_ROLE).toString()) == current_file;
			found = found || is_current;
			if (item->data(CURRENT_ROLE).toBool() != is_current)
				item->setData(CURRENT_ROLE, is_current);
		}
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
	/// @brief 表示中の画像を問い合わせる処理。
	HistoryCurrentCallback current_;
	/// @brief サムネイルの一覧。
	QListWidget *list_ = nullptr;
	/// @brief 画像がまだないときの案内。
	QLabel *empty_label_ = nullptr;

	/// @brief 一覧に出す画像のパス。先頭が一番新しい。
	QStringList paths_;
	/// @brief 前回読み直したときの一番新しい画像。増えたことに気づいて一番上までスクロールするため。
	QString newest_;
	/// @brief 作ったサムネイル。一覧に残っている画像の分だけ持つ。
	QHash<QString, QPixmap> thumbnails_;
	/// @brief 作成中のサムネイルのパス。同じ画像を二重に作らないため。
	QSet<QString> loading_;
	/// @brief 前回問い合わせたときの、表示中の画像のパス(表示していなければ空)。変わったときだけ印を付け直すため。
	QString current_raw_;
	/// @brief true なら、ドックが見えていない間に保存フォルダが変わった(または、まだ一度も読んでいない)。見えたときに読み直す。
	bool needs_refresh_ = false;
	/// @brief true なら止めた後。届いた通知やサムネイルを処理しない。
	bool stopped_ = false;

	/// @brief 保存フォルダの見張り。
	QFileSystemWatcher watcher_;
	/// @brief フォルダの変化をまとめて 1 回で読み直すためのタイマー。
	QTimer refresh_timer_;
	/// @brief 表示中の画像を定期的に問い合わせるタイマー。
	QTimer current_timer_;
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
void history_dock_register(HistorySelectedCallback on_selected, HistoryCurrentCallback current)
{
	if (dock_widget)
		return;

	auto *widget = new HistoryWidget(std::move(on_selected), std::move(current));
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
