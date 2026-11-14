// SPDX-FileCopyrightText: 2026 KMOdan5
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file settings-dialog.cpp
 * @brief 設定画面(ダイアログ)。
 *
 * @details
 * Qt のウィジェットを組み合わせて作る(.ui ファイルや Q_OBJECT は使わない。項目が少ないため)。
 * 表示する文字列はすべて data/locale の ini から読む。
 * 項目は「一般」と「表示履歴」の 2 つのタブに分ける。
 * 「OK」「適用」で settings_save()(settings.cpp)を呼び、変更は次に画像を表示したときから反映される。
 * 「表示履歴」のタブの「保存フォルダを開く」は、OK や適用を待たずにその場で動く(open_image_folder())。
 * 画面の一番下に、バージョンと、マニュアル・不具合・ご要望のフォームへのリンクを表示する(make_about_label())。
 * 開いたまま OBS を操作できる画面(モードレス)で、同時に開くのは 1 つだけ。
 *
 * 公開している関数の説明は settings-dialog.hpp を参照。
 */

#include "settings-dialog.hpp"
#include "clipboard-capture.hpp"
#include "settings.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QCheckBox>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QSysInfo>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <initializer_list>

namespace {

/// @brief ダイアログの最小の幅(ピクセル)。説明文が細かく折り返されて読みにくくならないように。
constexpr int DIALOG_MIN_WIDTH = 480;

/*
 * 余白は 8 の倍数を基本にそろえる(余白の大小の関係が見た目で分かりやすいため)。
 * 入力欄と説明文の間だけは、ひとまとまりに見せるため 8 より小さくする。
 */

/// @brief 部品どうし・項目名と入力欄の間の余白(ピクセル)。
constexpr int BASE_SPACING = 8;

/// @brief 画面の縁の余白(ピクセル)。
constexpr int DIALOG_MARGIN = 16;

/// @brief 入力欄とその説明文の間の余白(ピクセル)。説明文が上の入力欄のものだと分かるよう詰める。
constexpr int FIELD_NOTE_SPACING = 4;

/// @brief 枠の中の項目どうしの間の余白(ピクセル)。説明文が次の項目のものに見えないよう広く空ける。
constexpr int ROW_SPACING = 16;

/**
 * @brief まとまりどうし(タブ・枠・タブの外の案内・バージョンとリンク・ボタン)の間に足す余白(ピクセル)。
 * @details 部品どうしの余白(BASE_SPACING)に足すので、間は合わせて 16px になる。
 */
constexpr int SECTION_SPACING = 8;

/**
 * @brief タブの中の縁の余白(ピクセル)。
 * @details タブの枠の線があるので、画面の縁(DIALOG_MARGIN)より詰める。画面が縦に伸びすぎないようにするため。
 */
constexpr int TAB_MARGIN = 8;

/// @brief 「一般」のタブの番号。既定値のボタンで、どのタブの値を戻すかを決めるため。
constexpr int GENERAL_TAB_INDEX = 0;

/// @brief 割合の入力欄の objectName。開き直したときに findChild() で探すため。
constexpr const char *PERCENT_SPIN_NAME = "maxSizePercent";

/// @brief 割合に対応するピクセル数を表示するラベルの objectName。
constexpr const char *PIXELS_LABEL_NAME = "maxSizePixels";

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
 * @brief 補足の数字などを、ほかの文字より薄い色で表示するようにする。
 *
 * @details
 * OBS のテーマには、薄い文字の見た目(text-muted という class)が用意されているので、それを付ける。
 * テーマにない場合に備えて、Qt の配色(入力欄の薄い案内文字の色)も合わせて設定しておく。
 *
 * @param label 対象のラベル。
 */
void make_muted(QLabel *label)
{
	label->setProperty("class", QStringLiteral("text-muted"));
	QPalette palette = label->palette();
	palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
	label->setPalette(palette);
}

/**
 * @brief 不具合・ご要望のフォームを開く URL を作る。
 *
 * @details
 * ini の Settings.ReportIssueUrl は、Google フォームの「事前入力した URL」の値の部分を %1〜%3 にしたもの。
 * %1 にプラグインの版(画面の表示と同じ「v0.1.0-beta」)、%2 に配信ソフトとバージョン(「OBS Studio 32.0.1」)、
 * %3 に OS(Qt が返す名前。例: 「Windows 11 Version 24H2」)を入れる。ツール名は ini の URL に入れてある。
 * 利用者が環境を調べて書く手間をなくし、報告に必要な情報を確実に集めるため。
 * - 値はフォームの入力欄に入るだけで、利用者は送信前に確認・書き換えができる。プラグイン自身は何も送信しない
 * - 値には URL で使えない文字(空白など)が入るので、パーセントエンコードする
 * - %1〜%3 は 1 回の arg() でまとめて置き換える(入れた値の中の % が、続く置き換えの対象にならないように)
 * - 質問の番号(entry.〜)はフォームごとに決まっているため、言語ごとのフォームに合わせて ini に書く
 *
 * @return フォームの URL。
 *
 * @note 呼び出し元: make_about_label()。
 */
QString report_url()
{
	auto encode = [](const QString &value) {
		return QString::fromLatin1(QUrl::toPercentEncoding(value));
	};
	const QString version = text("Settings.Version").arg(QString::fromUtf8(PLUGIN_DISPLAY_VERSION));
	/* フォームに入れる値で、画面に出す文言ではないため ini には置かない */
	const QString streaming_app = QStringLiteral("OBS Studio %1").arg(QString::fromUtf8(obs_get_version_string()));
	const QString os = QSysInfo::prettyProductName();
	return text("Settings.ReportIssueUrl").arg(encode(version), encode(streaming_app), encode(os));
}

/**
 * @brief 画面の一番下に置く、バージョンとリンクの 1 行を作る。
 *
 * @details
 * 例: 「v0.1.0 · マニュアル · 不具合・ご要望」。困ったときにその場でマニュアルや報告先を開けるようにし、
 * 不具合の報告で使っている版や環境を伝えてもらいやすくするため。
 * - 文字は枠の中の説明文と同じ薄い文字にし、リンクは OBS の標準の見た目(テーマのリンク色)のままにする
 * - リンク先は言語ごとに ini で決める(日本語は日本語のマニュアル、英語は英語の README)
 * - 不具合・ご要望のリンクは、使っている環境を入力した状態でフォームを開く(report_url())
 * - クリックすると既定のブラウザで開く。プラグイン自身が通信するわけではない
 * - 版は PLUGIN_DISPLAY_VERSION。テスト版は「v0.1.0-beta」、完成版は「v0.1.0」と表示する(buildspec.json の beta で切り替え)
 *   配布版(タグのビルド)以外の GitHub Actions のビルドでは「v0.1.0-beta+a80965dc8」のように、後ろにコミットのハッシュが付く
 * - 文字はマウスで選んでコピーできる(ドラッグして Ctrl+C、または右クリックのメニュー)。
 *   DM などで問い合わせを受けたとき、版を正確に伝えてもらうため。見た目は変えない
 *
 * @return 作ったラベル(レイアウトに追加すると、ダイアログが解放する)。
 */
QLabel *make_about_label()
{
	/* ini の文字列をそのまま HTML に入れると、記号が書式として解釈されることがあるため、文字として扱う */
	auto link = [](const QString &url, const char *text_key) {
		return QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), text(text_key).toHtmlEscaped());
	};
	const QString version = text("Settings.Version").arg(QString::fromUtf8(PLUGIN_DISPLAY_VERSION)).toHtmlEscaped();
	const QStringList parts = {version, link(text("Settings.ManualUrl"), "Settings.Manual"),
				   link(report_url(), "Settings.ReportIssue")};

	auto *label = new QLabel(parts.join(QStringLiteral(" \u00b7 ")));
	label->setTextFormat(Qt::RichText);
	label->setOpenExternalLinks(true);
	/* リンクを開く動きはそのままに、文字の選択とコピーもできるようにする */
	label->setTextInteractionFlags(Qt::TextBrowserInteraction);
	make_muted(label);
	return label;
}

/**
 * @brief 説明文のラベルを作る。
 *
 * @details
 * 折り返して表示し、項目名や入力欄と区別できるよう薄い文字にする。
 * 文字は上に寄せる。折り返す文章は、レイアウトが見積もる高さが実際の行数より大きくなることがあり、
 * 既定の上下中央寄せだと、余った高さの分だけ文字が下にずれて入力欄から離れて見えるため。
 *
 * @param key 説明文の ini のキー。
 * @return 作ったラベル(レイアウトに追加すると、ダイアログが解放する)。
 */
QLabel *make_note(const char *key)
{
	auto *note = new QLabel(text(key));
	note->setWordWrap(true);
	note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	make_muted(note);
	return note;
}

/**
 * @brief 入力欄(右の列)と、その下の説明文をひとまとめにする。
 *
 * @param control 入力欄、または入力欄を並べたレイアウトを持つウィジェット。
 * @param note_key 説明文の ini のキー。
 * @param spacing 入力欄と説明文の間の余白(ピクセル)。チェックボックスは checkbox_note_spacing() の値を渡す。
 * @return まとめたウィジェット(QFormLayout の右の列に置く)。
 */
QWidget *make_field(QWidget *control, const char *note_key, int spacing = FIELD_NOTE_SPACING)
{
	auto *field = new QWidget();
	auto *column = new QVBoxLayout(field);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(spacing);
	column->addWidget(control);
	column->addWidget(make_note(note_key));
	return field;
}

/**
 * @brief チェックボックスとその説明文の間の余白を求める。
 *
 * @details
 * チェックボックスは、四角と文字の上下に部品自体の余白があり(大きさはテーマで決まる)、
 * 入力欄と同じ余白では説明文が離れて見える。そこで、四角と文字の下にある部品の余白の分だけ差し引く。
 * テーマから余白を求めるので、テーマや画面の拡大率が変わっても、説明文が四角や文字に重ならない。
 *
 * @param checkbox 対象のチェックボックス。
 * @return 余白(ピクセル)。マイナスになることがある。
 */
int checkbox_note_spacing(QCheckBox *checkbox)
{
	/* テーマのスタイルシートを当てた後の大きさで計算する */
	checkbox->ensurePolished();
	const int indicator_height = checkbox->style()->pixelMetric(QStyle::PM_IndicatorHeight, nullptr, checkbox);
	const int content_height = std::max(checkbox->fontMetrics().height(), indicator_height);
	const int padding_below = std::max(0, (checkbox->sizeHint().height() - content_height) / 2);
	return FIELD_NOTE_SPACING - padding_below;
}

/**
 * @brief 項目名のラベルの幅をそろえる。
 *
 * @details
 * 枠ごとに項目名の列の幅が決まるため、項目名の長さが違うと、枠によって入力欄の左の位置がずれる。
 * 一番長い項目名に合わせて最小の幅をそろえ、どの枠でも入力欄が同じ位置から始まるようにする。
 *
 * @param labels 幅をそろえるラベル。
 */
void align_label_widths(std::initializer_list<QLabel *> labels)
{
	int width = 0;
	for (QLabel *label : labels) {
		label->ensurePolished();
		width = std::max(width, label->sizeHint().width());
	}
	for (QLabel *label : labels)
		label->setMinimumWidth(width);
}

/**
 * @brief 見出し付きの枠と、その中の項目を並べるレイアウトを作る。
 *
 * @details
 * 左の列に項目名(右揃え)、右の列に入力欄と説明文を置く(OBS の設定画面に合わせる)。
 * 項目名と入力欄の間は BASE_SPACING、項目どうしの間は ROW_SPACING だけ空ける。
 *
 * @param title_key 枠の見出しの ini のキー。
 * @param[out] form 枠の中のレイアウト。項目は addRow() で足す。
 * @return 作った枠(レイアウトに追加すると、ダイアログが解放する)。
 */
QGroupBox *make_group(const char *title_key, QFormLayout *&form)
{
	auto *group = new QGroupBox(text(title_key));
	form = new QFormLayout(group);
	form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setHorizontalSpacing(BASE_SPACING);
	form->setVerticalSpacing(ROW_SPACING);
	return group;
}

/**
 * @brief 入力欄を左寄せで置いたウィジェットを作る(入力欄が右の列いっぱいに広がらないように)。
 *
 * @param widgets 左から順に並べるウィジェット。
 * @return 並べたウィジェット。
 */
QWidget *make_row(std::initializer_list<QWidget *> widgets)
{
	auto *row = new QWidget();
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(BASE_SPACING);
	for (QWidget *widget : widgets)
		layout->addWidget(widget);
	layout->addStretch();
	return row;
}

/**
 * @brief 割合を指定したときの最大の大きさ(ピクセル)を、割合の横のラベルに表示する。
 *
 * @details
 * 割合はキャンバスの面積に対する割合で、幅と高さにはそれぞれ √(割合) をかける(max_size_side_ratio())。
 * 例: キャンバスが 1920x1080 で 60% → 「(1487 × 837 px)」。ピクセル数は四捨五入して整数にする。
 * キャンバスの大きさは開くたび・割合を変えるたびに取り直す(画面を開いたまま OBS の解像度を変えることがあるため)。
 * キャンバスの大きさが取れないときは、ラベルを隠す(間違った数字を出さないため)。
 *
 * @param dialog 設定画面。
 */
void update_pixel_label(QDialog *dialog)
{
	auto *spin = dialog->findChild<QSpinBox *>(PERCENT_SPIN_NAME);
	auto *label = dialog->findChild<QLabel *>(PIXELS_LABEL_NAME);
	if (!spin || !label)
		return;

	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0) {
		label->hide();
		return;
	}

	const double ratio = max_size_side_ratio(spin->value());
	const int width = static_cast<int>(ovi.base_width * ratio + 0.5);
	const int height = static_cast<int>(ovi.base_height * ratio + 0.5);
	label->setText(text("Settings.MaxSize.Pixels").arg(width).arg(height));
	label->show();
}

/**
 * @brief 画像の保存フォルダを、OS のファイルの画面(Windows ではエクスプローラー)で開く。
 *
 * @details
 * 設定フォルダではなく、その中の images フォルダを直接開く(設定ファイルを目立たせないため)。
 * まだ 1 枚も保存していないとフォルダがないので、作ってから開く。
 * 開けなかったときは、ログに警告を出すだけにする(設定画面の操作は続けられるため)。
 *
 * @note 呼び出し元: 「表示履歴」のタブの「保存フォルダを開く」ボタン。
 */
void open_image_folder()
{
	const QString folder = QString::fromUtf8(saved_images_folder().c_str());
	if (folder.isEmpty() || !QDir().mkpath(folder)) {
		obs_log(LOG_WARNING, "settings: cannot create image folder %s", folder.toUtf8().constData());
		return;
	}
	if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) {
		obs_log(LOG_WARNING, "settings: cannot open image folder %s", folder.toUtf8().constData());
		return;
	}
	obs_log(LOG_INFO, "settings: opened image folder %s", folder.toUtf8().constData());
}

/**
 * @brief タブの中身を載せるページを作る。
 *
 * @param[out] layout ページの中のレイアウト。枠や案内は addWidget() で足す。
 * @return 作ったページ(タブに追加すると、ダイアログが解放する)。
 */
QWidget *make_tab_page(QVBoxLayout *&layout)
{
	auto *page = new QWidget();
	layout = new QVBoxLayout(page);
	layout->setContentsMargins(TAB_MARGIN, TAB_MARGIN, TAB_MARGIN, TAB_MARGIN);
	layout->setSpacing(BASE_SPACING);
	return page;
}

/**
 * @brief 設定画面の入力欄をまとめたもの。設定値と画面の値の受け渡しをする。
 *
 * @details 中身はウィジェットへのポインタだけ(ウィジェット自体はダイアログが持つ)なので、
 *          ボタンの処理(ラムダ)には値のままコピーして渡してよい。
 */
struct SettingsWidgets {
	/// @brief 表示時間(秒)。
	QSpinBox *display_seconds = nullptr;
	/// @brief 最大サイズの割合(%)。
	QSpinBox *max_size_percent = nullptr;
	/// @brief 常に最前面に表示する。
	QCheckBox *always_on_top = nullptr;
	/// @brief スタジオモードでもすぐに配信画面に表示する。
	QCheckBox *studio_mode_show_on_program = nullptr;
	/// @brief 保存する枚数(「表示履歴」のタブ)。
	QSpinBox *max_saved_images = nullptr;

	/**
	 * @brief 設定値を「一般」のタブの各項目に表示する。
	 * @param settings 表示する設定値。
	 */
	void show_general_values(const PluginSettings &settings) const
	{
		display_seconds->setValue(settings.display_seconds);
		max_size_percent->setValue(settings.max_size_percent);
		always_on_top->setChecked(settings.always_on_top);
		studio_mode_show_on_program->setChecked(settings.studio_mode_show_on_program);
	}

	/**
	 * @brief 設定値を「表示履歴」のタブの各項目に表示する。
	 * @param settings 表示する設定値。
	 */
	void show_history_values(const PluginSettings &settings) const
	{
		max_saved_images->setValue(settings.max_saved_images);
	}

	/**
	 * @brief 設定値を、すべてのタブの各項目に表示する。
	 * @param settings 表示する設定値。
	 */
	void show_values(const PluginSettings &settings) const
	{
		show_general_values(settings);
		show_history_values(settings);
	}

	/**
	 * @brief 各項目の値を設定値として取り出す。
	 * @return 画面で入力された設定値。
	 */
	PluginSettings values() const
	{
		PluginSettings settings;
		settings.display_seconds = display_seconds->value();
		settings.max_size_percent = max_size_percent->value();
		settings.always_on_top = always_on_top->isChecked();
		settings.studio_mode_show_on_program = studio_mode_show_on_program->isChecked();
		settings.max_saved_images = max_saved_images->value();
		return settings;
	}
};

/**
 * @brief 開いている設定画面。開いていなければ空(QPointer なので、画面が消えると自動で空になる)。
 *
 * @details 2 つ目の画面を開かないようにするためと、OBS の終了時に閉じるために持っておく。
 */
QPointer<QDialog> settings_dialog;

/// @brief 設定画面から頼む処理(settings_dialog_register() で登録する)。
SettingsDialogCallbacks dialog_callbacks;

} // namespace

/**
 * @brief 設定画面を開く。開いていれば手前に出す。
 * @details 仕様の詳細は settings-dialog.hpp の宣言側を参照。
 */
void open_settings_dialog()
{
	/* 既に開いていれば、入力途中の値を残したまま手前に出すだけ(キャンバスの大きさは取り直す) */
	if (settings_dialog) {
		update_pixel_label(settings_dialog);
		settings_dialog->show();
		settings_dialog->raise();
		settings_dialog->activateWindow();
		return;
	}

	/*
	 * 開いたまま OBS を操作できる画面にする(閉じるまで待つ exec() は使わない)。
	 * 設定を試しながら画像を貼れるようにするためと、exec() の中で別の処理ループが回り、
	 * その間に表示処理や OBS の終了処理が割り込むのを避けるため。
	 * 親を OBS のメイン画面にする。閉じ方(OK・キャンセル・Esc キー・右上の×)に関係なく、
	 * 閉じると必ず finished が届くので、そこで解放する(閉じ方ごとに後片付けを書かないため)。
	 */
	auto *main_window = static_cast<QWidget *>(obs_frontend_get_main_window());
	auto *dialog = new QDialog(main_window);
	dialog->setModal(false);
	QObject::connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);
	dialog->setWindowTitle(text("Settings.Title"));
	dialog->setMinimumWidth(DIALOG_MIN_WIDTH);
	settings_dialog = dialog;

	/*
	 * 並びは OBS の設定画面に合わせる: 見出し付きの枠の中に、左の列に項目名(右揃え)、
	 * 右の列に入力欄とその説明文(薄い文字)。チェックボックスとボタンは項目名の列を空けて右の列に置く。
	 * 項目はタブで 2 つに分ける。項目を足しても画面が縦に伸びないようにするため:
	 * - 「一般」: 画像の表示に関わる設定。枠は役割で 2 つに分ける: 画像がいつ表示され、いつ非表示になるか
	 *   (表示時間・スタジオモードでの表示)と、表示するたびに自動で行うこと(大きさ・重なり順)
	 * - 「表示履歴」: 表示履歴のドックに並ぶ、保存した画像に関わること
	 * 設定画面は、いつも「一般」のタブで開く(前に開いていたタブは覚えない)
	 */
	auto *layout = new QVBoxLayout(dialog);
	layout->setContentsMargins(DIALOG_MARGIN, DIALOG_MARGIN, DIALOG_MARGIN, DIALOG_MARGIN);
	layout->setSpacing(BASE_SPACING);
	auto *tabs = new QTabWidget();
	layout->addWidget(tabs);
	SettingsWidgets widgets;
	QFormLayout *form = nullptr;

	/* ---- 「一般」のタブ ---- */
	QVBoxLayout *general = nullptr;
	tabs->addTab(make_tab_page(general), text("Settings.Tab.General"));
	QGroupBox *display_group = make_group("Settings.Group.ShowHideTiming", form);

	/* 表示時間: [10 秒] */
	widgets.display_seconds = new QSpinBox();
	widgets.display_seconds->setRange(DISPLAY_SECONDS_MIN, DISPLAY_SECONDS_MAX);
	widgets.display_seconds->setSuffix(text("Settings.Seconds"));
	auto *display_label = new QLabel(text("Settings.DisplayTime"));
	form->addRow(display_label, make_field(make_row({widgets.display_seconds}), "Settings.DisplayTime.Note"));

	/* スタジオモードでもすぐに配信画面に表示する(チェックボックスなので項目名の列は空ける) */
	widgets.studio_mode_show_on_program = new QCheckBox(text("Settings.StudioModeShowOnProgram"));
	form->addRow(QString(), make_field(widgets.studio_mode_show_on_program, "Settings.StudioModeShowOnProgram.Note",
					   checkbox_note_spacing(widgets.studio_mode_show_on_program)));

	general->addWidget(display_group);
	general->addSpacing(SECTION_SPACING);
	QGroupBox *adjust_group = make_group("Settings.Group.AutoAdjust", form);

	/*
	 * 最大サイズ: [60%] (1487 × 837 px)。オン/オフはない(大きさを自分で決めたい配信者は境界の種類を変える)。
	 * ピクセル数は割合を変えるたびに計算し直し、薄い文字で表示する
	 */
	widgets.max_size_percent = new QSpinBox();
	widgets.max_size_percent->setObjectName(PERCENT_SPIN_NAME);
	widgets.max_size_percent->setRange(MAX_SIZE_PERCENT_MIN, MAX_SIZE_PERCENT_MAX);
	widgets.max_size_percent->setSuffix(QStringLiteral("%"));
	auto *pixels = new QLabel();
	pixels->setObjectName(PIXELS_LABEL_NAME);
	make_muted(pixels);
	auto *max_size_label = new QLabel(text("Settings.MaxSize"));
	form->addRow(max_size_label, make_field(make_row({widgets.max_size_percent, pixels}), "Settings.MaxSize.Note"));
	QObject::connect(widgets.max_size_percent, &QSpinBox::valueChanged, dialog,
			 [dialog]() { update_pixel_label(dialog); });

	/* 常に最前面に表示する(チェックボックスなので項目名の列は空ける) */
	widgets.always_on_top = new QCheckBox(text("Settings.AlwaysOnTop"));
	form->addRow(QString(), make_field(widgets.always_on_top, "Settings.AlwaysOnTop.Note",
					   checkbox_note_spacing(widgets.always_on_top)));

	general->addWidget(adjust_group);
	general->addStretch();

	/* ---- 「表示履歴」のタブ ---- */
	QVBoxLayout *history = nullptr;
	tabs->addTab(make_tab_page(history), text("Settings.Tab.History"));
	QGroupBox *saved_group = make_group("Settings.Group.SavedImages", form);

	/*
	 * 保存する枚数: [50 枚]。上限を超えた古い画像は、次に画像を表示したときに自動で削除される。
	 * 下げても、その場では削除しない(設定の変更は、次に画像を表示したときから反映する)
	 */
	widgets.max_saved_images = new QSpinBox();
	widgets.max_saved_images->setRange(MAX_SAVED_IMAGES_MIN, MAX_SAVED_IMAGES_MAX);
	widgets.max_saved_images->setSuffix(text("Settings.MaxSavedImages.Suffix"));
	auto *saved_label = new QLabel(text("Settings.MaxSavedImages"));
	form->addRow(saved_label, make_field(make_row({widgets.max_saved_images}), "Settings.MaxSavedImages.Note"));

	/* 保存フォルダを開く(ボタンなので項目名の列は空ける。押すとその場で開く) */
	auto *open_folder = new QPushButton(text("Settings.OpenImageFolder"));
	QObject::connect(open_folder, &QPushButton::clicked, dialog, []() { open_image_folder(); });
	/*
	 * 表示履歴を全て削除。数が多く押し間違えたときの影響が大きいので、確認の画面を出す(既定のボタンはキャンセル)。
	 * 削除はごみ箱に移すだけなので、元に戻せる。OK と キャンセル は OBS の言語に合わせて Qt が表示する
	 */
	auto *delete_all = new QPushButton(text("Settings.DeleteAllHistory"));
	QObject::connect(delete_all, &QPushButton::clicked, dialog, [dialog]() {
		const auto answer = QMessageBox::question(dialog, text("Settings.DeleteAllHistory"),
							  text("Settings.DeleteAllHistory.Confirm"),
							  QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
		if (answer != QMessageBox::Ok) {
			obs_log(LOG_INFO, "settings: delete all history canceled");
			return;
		}
		if (dialog_callbacks.delete_all_history)
			dialog_callbacks.delete_all_history();
	});
	form->addRow(QString(), make_row({open_folder, delete_all}));

	history->addWidget(saved_group);
	history->addStretch();

	/* どの枠・タブでも入力欄の左の位置がそろうように */
	align_label_widths({display_label, max_size_label, saved_label});

	/*
	 * タブの外の案内: 反映のタイミング。OK を押しても表示中の画像が変わらないのを、不具合と思われないように。
	 * どのタブの設定にも当てはまるので、タブの外に置き、タブを切り替えても消えないようにする。
	 * 項目の補足ではなく画面全体への案内なので、薄くせず通常の濃さで表示する
	 * (薄い文字は、そばに濃い項目名がある枠の中の説明だけに使う)。
	 * タブのすぐ下に詰めて置く(タブの中にも縁の余白があり、離すと間が空きすぎるため)。
	 * フェードの変え方の案内は、プラグインの設定ではなく OBS の操作なので、マニュアルだけに書く
	 */
	auto *apply_note = new QLabel(text("Settings.ApplyNote"));
	apply_note->setWordWrap(true);
	layout->addWidget(apply_note);

	/*
	 * OK: 保存して閉じる / キャンセル: 保存せずに閉じる / 適用: 保存して開いたまま /
	 * 既定値: 画面上の値だけ既定値に戻す(OK か適用を押すまで保存しない)。並びは OBS の設定画面に合わせる。
	 * 既定値は OBS の設定画面と同じく、開いているタブの値だけを戻す
	 */
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply |
					     QDialogButtonBox::RestoreDefaults);
	/* ボタンの文字は Qt の既定だと英語のままになることがあるので、翻訳した文字列を使う */
	buttons->button(QDialogButtonBox::Ok)->setText(text("Settings.OK"));
	buttons->button(QDialogButtonBox::Cancel)->setText(text("Settings.Cancel"));
	buttons->button(QDialogButtonBox::Apply)->setText(text("Settings.Apply"));
	buttons->button(QDialogButtonBox::RestoreDefaults)->setText(text("Settings.RestoreDefaults"));
	/* 画面の一番下(ボタンの上): バージョンとリンク。どのタブを開いていても見えるよう、タブの外に置く */
	layout->addSpacing(SECTION_SPACING);
	layout->addWidget(make_about_label());

	layout->addSpacing(SECTION_SPACING);
	layout->addWidget(buttons);

	QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, widgets]() {
		settings_save(widgets.values());
		dialog->accept();
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	/* キャンセル・Esc キー・右上の×はどれも reject になる */
	QObject::connect(dialog, &QDialog::rejected, dialog,
			 []() { obs_log(LOG_INFO, "settings dialog closed without saving"); });
	QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, dialog,
			 [widgets]() { settings_save(widgets.values()); });
	QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, dialog,
			 [widgets, tabs]() {
				 if (tabs->currentIndex() == GENERAL_TAB_INDEX)
					 widgets.show_general_values(PluginSettings());
				 else
					 widgets.show_history_values(PluginSettings());
			 });

	widgets.show_values(settings_get());
	update_pixel_label(dialog);

	dialog->show();
	obs_log(LOG_INFO, "settings dialog opened");
}

/**
 * @brief 設定画面が開いていれば閉じる。
 * @details 仕様の詳細は settings-dialog.hpp の宣言側を参照。
 */
/**
 * @brief 設定画面から頼む処理を登録する。
 *
 * @details 仕様の詳細は settings-dialog.hpp の宣言側を参照。
 */
void settings_dialog_register(SettingsDialogCallbacks callbacks)
{
	dialog_callbacks = std::move(callbacks);
}

void close_settings_dialog()
{
	if (!settings_dialog)
		return;
	/* 終了処理の途中なので、後回しにせずその場で解放する(保存はしない) */
	delete settings_dialog.data();
	obs_log(LOG_INFO, "settings dialog closed");
}
