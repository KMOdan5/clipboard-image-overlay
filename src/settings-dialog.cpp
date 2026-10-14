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
 * 「OK」で settings_save()(settings.cpp)を呼び、変更は次に画像を表示したときから反映される。
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
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

/// @brief 説明文を、上の項目より字下げして表示するときの左の余白(ピクセル)。
constexpr int NOTE_INDENT = 24;

/// @brief ダイアログの最小の幅(ピクセル)。説明文が細かく折り返されて読みにくくならないように。
constexpr int DIALOG_MIN_WIDTH = 440;

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
 * @brief 設定値を画面の各項目に表示する。
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

} // namespace

/**
 * @brief 設定画面を開く。
 * @details 仕様の詳細は settings-dialog.hpp の宣言側を参照。
 */
void open_settings_dialog()
{
	auto *main_window = static_cast<QWidget *>(obs_frontend_get_main_window());
	QDialog dialog(main_window);
	dialog.setWindowTitle(text("Settings.Title"));
	dialog.setMinimumWidth(DIALOG_MIN_WIDTH);

	auto *layout = new QVBoxLayout(&dialog);
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

	/* 大きい画像を収める: 「[✓] 大きい画像を画面の [60]% に収める」。割合はオンのときだけ変えられる */
	auto *fit_row = new QHBoxLayout();
	widgets.fit_large_images = new QCheckBox(text("Settings.FitLarge.Before"));
	widgets.max_size_percent = new QSpinBox();
	widgets.max_size_percent->setRange(MAX_SIZE_PERCENT_MIN, MAX_SIZE_PERCENT_MAX);
	widgets.max_size_percent->setSuffix(QStringLiteral("%"));
	fit_row->addWidget(widgets.fit_large_images);
	fit_row->addWidget(widgets.max_size_percent);
	fit_row->addWidget(new QLabel(text("Settings.FitLarge.After")));
	fit_row->addStretch();
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

	/* OK / キャンセル / 既定に戻す(既定に戻すは画面上の値だけ。OK を押すまで保存しない) */
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
					     QDialogButtonBox::RestoreDefaults);
	/* ボタンの文字は Qt の既定だと英語のままになることがあるので、翻訳した文字列を使う */
	buttons->button(QDialogButtonBox::Ok)->setText(text("Settings.OK"));
	buttons->button(QDialogButtonBox::Cancel)->setText(text("Settings.Cancel"));
	buttons->button(QDialogButtonBox::RestoreDefaults)->setText(text("Settings.RestoreDefaults"));
	layout->addSpacing(8);
	layout->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
			 [&widgets]() { widgets.show_values(PluginSettings()); });

	widgets.show_values(settings_get());

	obs_log(LOG_INFO, "settings dialog opened");
	if (dialog.exec() != QDialog::Accepted) {
		obs_log(LOG_INFO, "settings dialog canceled");
		return;
	}
	settings_save(widgets.values());
}
