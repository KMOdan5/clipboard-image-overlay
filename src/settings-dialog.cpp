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
 * @file settings-dialog.cpp
 * @brief 設定画面(ダイアログ)。
 *
 * @details
 * Qt のウィジェットを組み合わせて作る(.ui ファイルや Q_OBJECT は使わない。項目が少ないため)。
 * 表示する文字列はすべて data/locale の ini から読む。
 * 「OK」「適用」で settings_save()(settings.cpp)を呼び、変更は次に画像を表示したときから反映される。
 * 画面の一番下に、バージョンと、マニュアル・不具合・ご要望のフォームへのリンクを表示する(make_about_label())。
 * 開いたまま OBS を操作できる画面(モードレス)で、同時に開くのは 1 つだけ。
 *
 * 公開している関数の説明は settings-dialog.hpp を参照。
 */

#include "settings-dialog.hpp"
#include "settings.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QSysInfo>
#include <QUrl>
#include <QVBoxLayout>

#include <initializer_list>

namespace {

/// @brief ダイアログの最小の幅(ピクセル)。説明文が細かく折り返されて読みにくくならないように。
constexpr int DIALOG_MIN_WIDTH = 480;

/// @brief 画面下の案内文の上に空ける余白(ピクセル)。枠の中の項目と区別するため。
constexpr int NOTES_SPACING = 6;

/// @brief ボタンの上に空ける余白(ピクセル)。案内文とボタンが詰まって見えないように。
constexpr int BUTTONS_SPACING = 12;

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
 * 利用者が環境を調べて書く手間をなくし、報告に必要な情報を確実に集めるため(#49)。
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
	make_muted(label);
	return label;
}

/**
 * @brief 説明文のラベルを作る。
 *
 * @details 折り返して表示し、項目名や入力欄と区別できるよう薄い文字にする。
 *
 * @param key 説明文の ini のキー。
 * @return 作ったラベル(レイアウトに追加すると、ダイアログが解放する)。
 */
QLabel *make_note(const char *key)
{
	auto *note = new QLabel(text(key));
	note->setWordWrap(true);
	make_muted(note);
	return note;
}

/**
 * @brief 入力欄(右の列)と、その下の説明文をひとまとめにする。
 *
 * @param control 入力欄、または入力欄を並べたレイアウトを持つウィジェット。
 * @param note_key 説明文の ini のキー。
 * @return まとめたウィジェット(QFormLayout の右の列に置く)。
 */
QWidget *make_field(QWidget *control, const char *note_key)
{
	auto *field = new QWidget();
	auto *column = new QVBoxLayout(field);
	column->setContentsMargins(0, 0, 0, 0);
	column->addWidget(control);
	column->addWidget(make_note(note_key));
	return field;
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
	for (QWidget *widget : widgets)
		layout->addWidget(widget);
	layout->addStretch();
	return row;
}

/**
 * @brief 割合を指定したときの大きさ(ピクセル)を、割合の横のラベルに表示する。
 *
 * @details
 * 例: キャンバスが 1920x1080 で 60% → 「(1152 × 648 px)」。
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

	const double ratio = spin->value() / 100.0;
	const int width = static_cast<int>(ovi.base_width * ratio + 0.5);
	const int height = static_cast<int>(ovi.base_height * ratio + 0.5);
	label->setText(text("Settings.MaxSize.Pixels").arg(width).arg(height));
	label->show();
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

	/**
	 * @brief 設定値を各項目に表示する。
	 * @param settings 表示する設定値。
	 */
	void show_values(const PluginSettings &settings) const
	{
		display_seconds->setValue(settings.display_seconds);
		max_size_percent->setValue(settings.max_size_percent);
		always_on_top->setChecked(settings.always_on_top);
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
		return settings;
	}
};

/**
 * @brief 開いている設定画面。開いていなければ空(QPointer なので、画面が消えると自動で空になる)。
 *
 * @details 2 つ目の画面を開かないようにするためと、OBS の終了時に閉じるために持っておく。
 */
QPointer<QDialog> settings_dialog;

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
	 * 右の列に入力欄とその説明文(薄い文字)。チェックボックスは項目名の列を空けて右の列に置く
	 */
	auto *layout = new QVBoxLayout(dialog);
	SettingsWidgets widgets;
	auto *group = new QGroupBox(text("Settings.Group.Display"));
	auto *form = new QFormLayout(group);
	form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	/* 表示時間: [10 秒] */
	widgets.display_seconds = new QSpinBox();
	widgets.display_seconds->setRange(DISPLAY_SECONDS_MIN, DISPLAY_SECONDS_MAX);
	widgets.display_seconds->setSuffix(text("Settings.Seconds"));
	form->addRow(text("Settings.DisplayTime"),
		     make_field(make_row({widgets.display_seconds}), "Settings.DisplayTime.Note"));

	/*
	 * 最大サイズ: [60%] (1152 × 648 px)。オン/オフはない(大きさを自分で決めたい配信者は境界の種類を変える)。
	 * ピクセル数は割合を変えるたびに計算し直し、薄い文字で表示する
	 */
	widgets.max_size_percent = new QSpinBox();
	widgets.max_size_percent->setObjectName(PERCENT_SPIN_NAME);
	widgets.max_size_percent->setRange(MAX_SIZE_PERCENT_MIN, MAX_SIZE_PERCENT_MAX);
	widgets.max_size_percent->setSuffix(QStringLiteral("%"));
	auto *pixels = new QLabel();
	pixels->setObjectName(PIXELS_LABEL_NAME);
	make_muted(pixels);
	form->addRow(text("Settings.MaxSize"),
		     make_field(make_row({widgets.max_size_percent, pixels}), "Settings.MaxSize.Note"));
	QObject::connect(widgets.max_size_percent, &QSpinBox::valueChanged, dialog,
			 [dialog]() { update_pixel_label(dialog); });

	/* 常に最前面に表示する(チェックボックスなので項目名の列は空ける) */
	widgets.always_on_top = new QCheckBox(text("Settings.AlwaysOnTop"));
	form->addRow(QString(), make_field(widgets.always_on_top, "Settings.AlwaysOnTop.Note"));

	layout->addWidget(group);

	/*
	 * 枠の外の案内: フェードはプラグインの設定ではないこと、反映のタイミング。
	 * 項目の補足ではなく画面全体への案内なので、薄くせず通常の濃さで表示する
	 * (薄い文字は、そばに濃い項目名がある枠の中の説明だけに使う)
	 */
	layout->addSpacing(NOTES_SPACING);
	for (const char *key : {"Settings.FadeNote", "Settings.ApplyNote"}) {
		auto *note = new QLabel(text(key));
		note->setWordWrap(true);
		layout->addWidget(note);
	}

	/*
	 * OK: 保存して閉じる / キャンセル: 保存せずに閉じる / 適用: 保存して開いたまま /
	 * 既定値: 画面上の値だけ既定値に戻す(OK か適用を押すまで保存しない)。並びは OBS の設定画面に合わせる
	 */
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply |
					     QDialogButtonBox::RestoreDefaults);
	/* ボタンの文字は Qt の既定だと英語のままになることがあるので、翻訳した文字列を使う */
	buttons->button(QDialogButtonBox::Ok)->setText(text("Settings.OK"));
	buttons->button(QDialogButtonBox::Cancel)->setText(text("Settings.Cancel"));
	buttons->button(QDialogButtonBox::Apply)->setText(text("Settings.Apply"));
	buttons->button(QDialogButtonBox::RestoreDefaults)->setText(text("Settings.RestoreDefaults"));
	/* 画面の一番下(ボタンの上): バージョンとリンク */
	layout->addSpacing(BUTTONS_SPACING);
	layout->addWidget(make_about_label());

	layout->addSpacing(BUTTONS_SPACING);
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
			 [widgets]() { widgets.show_values(PluginSettings()); });

	widgets.show_values(settings_get());
	update_pixel_label(dialog);

	dialog->show();
	obs_log(LOG_INFO, "settings dialog opened");
}

/**
 * @brief 設定画面が開いていれば閉じる。
 * @details 仕様の詳細は settings-dialog.hpp の宣言側を参照。
 */
void close_settings_dialog()
{
	if (!settings_dialog)
		return;
	/* 終了処理の途中なので、後回しにせずその場で解放する(保存はしない) */
	delete settings_dialog.data();
	obs_log(LOG_INFO, "settings dialog closed");
}
