/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society
SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "destinations.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;

// Bitrate + encoder picker, used for the shared settings and per destination.
class EncoderSettingsWidget : public QWidget {
public:
	explicit EncoderSettingsWidget(QWidget *parent = nullptr);
	void setValue(const EncoderSettings &e);
	EncoderSettings value() const;

private:
	QComboBox *encoder_;
	QSpinBox *videoBitrate_;
	QComboBox *audioBitrate_;
};

class DestinationDialog : public QDialog {
public:
	DestinationDialog(const Destination &d, const EncoderSettings &shared, bool isNew, bool isLive,
			  QWidget *parent = nullptr);
	Destination result() const;

private:
	void onPlatformChanged();
	void onAccept();

	Destination dest_;
	QLineEdit *name_;
	QComboBox *platform_;
	QLineEdit *server_;
	QLineEdit *key_;
	QCheckBox *showKey_;
	QLabel *help_;
	QCheckBox *custom_;
	EncoderSettingsWidget *encoder_;
	QString lastAutoServer_;
	QString lastAutoName_;
};

class SharedEncoderDialog : public QDialog {
public:
	SharedEncoderDialog(const EncoderSettings &e, QWidget *parent = nullptr);
	EncoderSettings value() const { return w_->value(); }

private:
	EncoderSettingsWidget *w_;
};
