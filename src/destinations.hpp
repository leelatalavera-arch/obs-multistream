/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society
SPDX-License-Identifier: GPL-2.0-or-later

Destination model + JSON (de)serialisation. Depends on Qt Core only so it
can be unit-tested without OBS.
*/

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

struct EncoderSettings {
	QString encoderId = QStringLiteral("auto"); // "auto" or an OBS encoder id
	int videoBitrate = 6000;                    // kbps
	int audioBitrate = 160;                     // kbps

	bool operator==(const EncoderSettings &o) const
	{
		return encoderId == o.encoderId && videoBitrate == o.videoBitrate &&
		       audioBitrate == o.audioBitrate;
	}
	bool operator!=(const EncoderSettings &o) const { return !(*this == o); }
};

struct Destination {
	QString id;       // random, local only
	QString name;     // "My Twitch"
	QString platform; // preset id, "custom" for anything else
	QString server;
	QString key; // SECRET: saved only in the local profile, never exported
	bool enabled = true;
	bool useCustomEncoder = false;
	EncoderSettings encoder;
};

struct Setup {
	QList<Destination> destinations;
	EncoderSettings shared;
	bool linkMainStream = false; // start/stop with OBS's own Start Streaming
	QJsonValue hotkeys;          // opaque, owned by plugin-main
};

namespace model {

constexpr int kConfigVersion = 1;
constexpr int kExportVersion = 1;
extern const char *const kExportFormat; // "obs-multistream-setup"

QString newId();
int clampVideoBitrate(int kbps);
int clampAudioBitrate(int kbps);

bool hasServer(const Destination &d);
bool hasKey(const Destination &d);
bool isReady(const Destination &d); // enabled + server + key
EncoderSettings effectiveEncoder(const Destination &d, const EncoderSettings &shared);

// Local config file (includes keys). Lives inside the user's OBS profile.
QJsonObject toConfigJson(const Setup &s);
Setup fromConfigJson(const QJsonObject &o);

// Shareable export: built from a WHITELIST of fields. No key, and the server
// only when it is the platform's public default (custom/per-user URLs can
// contain account ids or even keys).
QString exportableServer(const Destination &d);
QJsonObject toExportJson(const Setup &s, const QString &pluginVersion);
QByteArray toExportBytes(const Setup &s, const QString &pluginVersion);

struct ImportResult {
	bool ok = false;
	QString error;
	QList<Destination> destinations; // fresh ids, empty keys
	EncoderSettings shared;
	bool hasShared = false;
};
ImportResult fromExportBytes(const QByteArray &bytes);

} // namespace model
