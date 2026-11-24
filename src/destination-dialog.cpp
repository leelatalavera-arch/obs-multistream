/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society
SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "destination-dialog.hpp"
#include "platforms.hpp"
#include "stream-engine.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

// ---------------------------------------------------------------- encoder
EncoderSettingsWidget::EncoderSettingsWidget(QWidget *parent) : QWidget(parent)
{
	auto *form = new QFormLayout(this);
	form->setContentsMargins(0, 0, 0, 0);

	encoder_ = new QComboBox(this);
	encoder_->addItem(QStringLiteral("Auto (graphics card first, CPU if needed)"), QStringLiteral("auto"));
	for (const auto &e : StreamEngine::availableVideoEncoders())
		encoder_->addItem(e.label, e.id);
	form->addRow(QStringLiteral("Video encoder"), encoder_);

	videoBitrate_ = new QSpinBox(this);
	videoBitrate_->setRange(500, 51000);
	videoBitrate_->setSingleStep(250);
	videoBitrate_->setSuffix(QStringLiteral(" kbps"));
	videoBitrate_->setToolTip(QStringLiteral("Most platforms: 4500–6000 for 1080p. Each destination uses its "
						 "own share of your upload speed."));
	form->addRow(QStringLiteral("Video bitrate"), videoBitrate_);

	audioBitrate_ = new QComboBox(this);
	for (int b : {96, 128, 160, 192, 256, 320})
		audioBitrate_->addItem(QStringLiteral("%1 kbps").arg(b), b);
	form->addRow(QStringLiteral("Audio bitrate"), audioBitrate_);

	setValue(EncoderSettings{});
}

void EncoderSettingsWidget::setValue(const EncoderSettings &e)
{
	int idx = encoder_->findData(e.encoderId);
	if (idx < 0) {
		// Saved encoder isn't installed on this computer (e.g. setup from a
		// friend with a different graphics card): fall back to Auto.
		idx = 0;
	}
	encoder_->setCurrentIndex(idx);
	videoBitrate_->setValue(model::clampVideoBitrate(e.videoBitrate));
	int a = audioBitrate_->findData(e.audioBitrate);
	audioBitrate_->setCurrentIndex(a < 0 ? audioBitrate_->findData(160) : a);
}

EncoderSettings EncoderSettingsWidget::value() const
{
	EncoderSettings e;
	e.encoderId = encoder_->currentData().toString();
	e.videoBitrate = videoBitrate_->value();
	e.audioBitrate = audioBitrate_->currentData().toInt();
	return e;
}

// ---------------------------------------------------------------- destination
DestinationDialog::DestinationDialog(const Destination &d, const EncoderSettings &shared, bool isNew, bool isLive,
				     QWidget *parent)
	: QDialog(parent),
	  dest_(d)
{
	setWindowTitle(isNew ? QStringLiteral("Add a platform") : QStringLiteral("Edit %1").arg(d.name));
	setMinimumWidth(460);

	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout();
	layout->addLayout(form);

	platform_ = new QComboBox(this);
	for (const auto &p : platformPresets())
		platform_->addItem(p.name, p.id);
	platform_->addItem(customPreset().name, customPreset().id);
	form->addRow(QStringLiteral("Platform"), platform_);

	name_ = new QLineEdit(this);
	name_->setMaxLength(60);
	name_->setPlaceholderText(QStringLiteral("e.g. My Twitch"));
	form->addRow(QStringLiteral("Name"), name_);

	help_ = new QLabel(this);
	help_->setWordWrap(true);
	help_->setOpenExternalLinks(true);
	help_->setTextFormat(Qt::RichText);
	form->addRow(QString(), help_);

	server_ = new QLineEdit(this);
	server_->setPlaceholderText(QStringLiteral("rtmp://… or rtmps://…"));
	form->addRow(QStringLiteral("Server URL"), server_);

	key_ = new QLineEdit(this);
	key_->setEchoMode(QLineEdit::Password);
	key_->setPlaceholderText(QStringLiteral("Paste your stream key"));
	form->addRow(QStringLiteral("Stream key"), key_);

	showKey_ = new QCheckBox(QStringLiteral("Show key"), this);
	connect(showKey_, &QCheckBox::toggled, this,
		[this](bool on) { key_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
	form->addRow(QString(), showKey_);

	auto *privacy = new QLabel(QStringLiteral("<small>Your key stays on this computer, in your OBS profile. "
						  "It is never included when you export your setup. "
						  "Never show it on stream.</small>"),
				   this);
	privacy->setWordWrap(true);
	form->addRow(QString(), privacy);

	auto *encBox = new QGroupBox(QStringLiteral("Video quality"), this);
	auto *encLayout = new QVBoxLayout(encBox);
	custom_ = new QCheckBox(QStringLiteral("Use different settings for this platform"), encBox);
	custom_->setToolTip(QStringLiteral("Off = share one encoder with the other platforms (lighter on your PC)."));
	encoder_ = new EncoderSettingsWidget(encBox);
	encLayout->addWidget(custom_);
	encLayout->addWidget(encoder_);
	layout->addWidget(encBox);
	connect(custom_, &QCheckBox::toggled, encoder_, &QWidget::setEnabled);

	if (isLive) {
		auto *note = new QLabel(QStringLiteral("<b>This platform is live.</b> Changes apply the next time "
						       "you start it."),
					this);
		note->setWordWrap(true);
		layout->addWidget(note);
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &DestinationDialog::onAccept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	// fill values
	int pidx = platform_->findData(d.platform);
	platform_->setCurrentIndex(pidx < 0 ? 0 : pidx);
	name_->setText(d.name);
	server_->setText(d.server);
	key_->setText(d.key);
	custom_->setChecked(d.useCustomEncoder);
	encoder_->setValue(d.useCustomEncoder ? d.encoder : shared);
	encoder_->setEnabled(d.useCustomEncoder);
	if (isNew) {
		lastAutoName_ = d.name;
		lastAutoServer_ = d.server;
	}

	connect(platform_, &QComboBox::currentIndexChanged, this, [this](int) { onPlatformChanged(); });
	onPlatformChanged();
}

void DestinationDialog::onPlatformChanged()
{
	const PlatformPreset *p = findPreset(platform_->currentData().toString());
	if (!p)
		p = &customPreset();

	// Only replace name/server if the user hasn't typed their own.
	if (name_->text().trimmed().isEmpty() || name_->text() == lastAutoName_) {
		name_->setText(p->name);
		lastAutoName_ = p->name;
	}
	if (server_->text().trimmed().isEmpty() || server_->text() == lastAutoServer_) {
		server_->setText(p->server);
		lastAutoServer_ = p->server;
	}

	QString html = p->keyHelp.toHtmlEscaped();
	if (!p->keyUrl.isEmpty())
		html += QStringLiteral("<br><a href=\"%1\">Where do I find my key?</a>").arg(p->keyUrl.toHtmlEscaped());
	if (p->keyChanges)
		html += QStringLiteral("<br><b>Tip:</b> paste the new URL and key here before each stream.");
	help_->setText(html);
}

void DestinationDialog::onAccept()
{
	const QString server = server_->text().trimmed();
	if (name_->text().trimmed().isEmpty()) {
		QMessageBox::warning(this, windowTitle(), QStringLiteral("Please give this platform a name."));
		return;
	}
	if (!server.isEmpty() && !server.startsWith(QStringLiteral("rtmp://"), Qt::CaseInsensitive) &&
	    !server.startsWith(QStringLiteral("rtmps://"), Qt::CaseInsensitive)) {
		QMessageBox::warning(this, windowTitle(),
				     QStringLiteral("The server URL should start with rtmp:// or rtmps://"));
		return;
	}
	dest_.name = name_->text().simplified().left(60);
	dest_.platform = platform_->currentData().toString();
	dest_.server = server;
	dest_.key = key_->text().trimmed();
	dest_.useCustomEncoder = custom_->isChecked();
	if (dest_.useCustomEncoder)
		dest_.encoder = encoder_->value();
	accept();
}

Destination DestinationDialog::result() const
{
	return dest_;
}

// ---------------------------------------------------------------- shared
SharedEncoderDialog::SharedEncoderDialog(const EncoderSettings &e, QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QStringLiteral("Video quality (all platforms)"));
	setMinimumWidth(420);
	auto *layout = new QVBoxLayout(this);
	auto *intro = new QLabel(QStringLiteral("These settings are shared by every platform that doesn't have its "
						"own. One encoder feeds them all, so your computer only does the "
						"work once."),
				 this);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	w_ = new EncoderSettingsWidget(this);
	w_->setValue(e);
	layout->addWidget(w_);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
