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

/**
 * @file settings-dialog.cpp
 * @brief 設定画面(ダイアログ)。
 *
 * @details
 * Qt のウィジェットを組み合わせて作る(.ui ファイルや Q_OBJECT は使わない。項目が少ないため)。
 * 表示する文字列はすべて data/locale の ini から読む。
 * 「OK」「適用」で settings_save()(settings.cpp)を呼び、変更は次に画像を表示したときから反映される。
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
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

/// @brief 説明文を、上の項目より字下げして表示するときの左の余白(ピクセル)。
constexpr int NOTE_INDENT = 24;

/// @brief ダイアログの最小の幅(ピクセル)。説明文が細かく折り返されて読みにくくならないように。
constexpr int DIALOG_MIN_WIDTH = 440;

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
 * @brief 項目の下に置く説明文のラベルを作る。
 *
 * @details 折り返して表示し、項目より少し字下げする。
 *
 * @param key 説明文の ini のキー。
 * @return 作ったラベル(レイアウトに追加すると、ダイアログが解放する)。
 */
QLabel *make_note(const char *key)
{
	auto *note = new QLabel(text(key));
	note->setWordWrap(true);
	note->setContentsMargins(NOTE_INDENT, 0, 0, 0);
	return note;
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
	label->setText(text("Settings.FitLarge.Pixels").arg(width).arg(height));
	label->show();
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
 * @brief 設定画面の入力欄をまとめたもの。設定値と画面の値の受け渡しをする。
 *
 * @details 中身はウィジェットへのポインタだけ(ウィジェット自体はダイアログが持つ)なので、
 *          ボタンの処理(ラムダ)には値のままコピーして渡してよい。
 */
struct SettingsWidgets {
	/// @brief 表示時間(秒)。
	QSpinBox *display_seconds = nullptr;
	/// @brief 大きい画像を画面に収めるか。
	QCheckBox *fit_large_images = nullptr;
	/// @brief 最大サイズの割合(%)。
	QSpinBox *max_size_percent = nullptr;
	/// @brief 常に手前に表示する。
	QCheckBox *always_on_top = nullptr;

	/**
	 * @brief 設定値を各項目に表示する。
	 * @param settings 表示する設定値。
	 */
	void show_values(const PluginSettings &settings) const
	{
		display_seconds->setValue(settings.display_seconds);
		fit_large_images->setChecked(settings.fit_large_images);
		max_size_percent->setValue(settings.max_size_percent);
		max_size_percent->setEnabled(settings.fit_large_images);
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
		settings.fit_large_images = fit_large_images->isChecked();
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

	auto *layout = new QVBoxLayout(dialog);
	SettingsWidgets widgets;

	/* 表示時間: 「表示時間 [7] 秒」 */
	auto *time_row = new QHBoxLayout();
	time_row->addWidget(new QLabel(text("Settings.DisplayTime")));
	widgets.display_seconds = new QSpinBox();
	widgets.display_seconds->setRange(DISPLAY_SECONDS_MIN, DISPLAY_SECONDS_MAX);
	widgets.display_seconds->setSuffix(text("Settings.Seconds"));
	time_row->addWidget(widgets.display_seconds);
	time_row->addStretch();
	layout->addLayout(time_row);
	layout->addWidget(make_note("Settings.DisplayTime.Note"));

	/*
	 * 大きい画像を収める: 「[✓] 画像をキャンバスサイズの [60]% に収める (1152 × 648 px)」。
	 * 割合はオンのときだけ変えられる。ピクセル数は割合を変えるたびに計算し直し、薄い文字で表示する
	 */
	auto *fit_row = new QHBoxLayout();
	widgets.fit_large_images = new QCheckBox(text("Settings.FitLarge.Before"));
	widgets.max_size_percent = new QSpinBox();
	widgets.max_size_percent->setObjectName(PERCENT_SPIN_NAME);
	widgets.max_size_percent->setRange(MAX_SIZE_PERCENT_MIN, MAX_SIZE_PERCENT_MAX);
	widgets.max_size_percent->setSuffix(QStringLiteral("%"));
	auto *pixels = new QLabel();
	pixels->setObjectName(PIXELS_LABEL_NAME);
	make_muted(pixels);
	fit_row->addWidget(widgets.fit_large_images);
	fit_row->addWidget(widgets.max_size_percent);
	fit_row->addWidget(new QLabel(text("Settings.FitLarge.After")));
	fit_row->addWidget(pixels);
	fit_row->addStretch();
	QObject::connect(widgets.max_size_percent, &QSpinBox::valueChanged, dialog,
			 [dialog]() { update_pixel_label(dialog); });
	layout->addSpacing(8);
	layout->addLayout(fit_row);
	layout->addWidget(make_note("Settings.FitLarge.Note"));
	QObject::connect(widgets.fit_large_images, &QCheckBox::toggled, widgets.max_size_percent,
			 &QSpinBox::setEnabled);

	/* 常に手前に表示する */
	widgets.always_on_top = new QCheckBox(text("Settings.AlwaysOnTop"));
	layout->addSpacing(8);
	layout->addWidget(widgets.always_on_top);
	layout->addWidget(make_note("Settings.AlwaysOnTop.Note"));

	/* フェードの長さはプラグインの設定ではないことと、反映のタイミングを案内する */
	layout->addSpacing(8);
	auto *fade_note = new QLabel(text("Settings.FadeNote"));
	fade_note->setWordWrap(true);
	layout->addWidget(fade_note);
	auto *apply_note = new QLabel(text("Settings.ApplyNote"));
	apply_note->setWordWrap(true);
	layout->addWidget(apply_note);

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
	layout->addSpacing(8);
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
