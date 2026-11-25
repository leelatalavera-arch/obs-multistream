/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society
SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "destinations.hpp"
#include "platforms.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

namespace model {

const char *const kExportFormat = "obs-multistream-setup";

QString newId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int clampVideoBitrate(int kbps)
{
	return qBound(500, kbps, 51000);
}

int clampAudioBitrate(int kbps)
{
	return qBound(64, kbps, 320);
}

bool hasServer(const Destination &d)
{
	return !d.server.trimmed().isEmpty();
}

bool hasKey(const Destination &d)
{
	return !d.key.trimmed().isEmpty();
}

bool isReady(const Destination &d)
{
	return d.enabled && hasServer(d) && hasKey(d);
}

EncoderSettings effectiveEncoder(const Destination &d, const EncoderSettings &shared)
{
	return d.useCustomEncoder ? d.encoder : shared;
}

static QJsonObject encoderToJson(const EncoderSettings &e)
{
	QJsonObject o;
	o["encoder"] = e.encoderId;
	o["video_bitrate"] = e.videoBitrate;
	o["audio_bitrate"] = e.audioBitrate;
	return o;
}

static EncoderSettings encoderFromJson(const QJsonObject &o)
{
	EncoderSettings e;
	const QString id = o.value("encoder").toString().trimmed();
	// Only accept plain identifier characters for encoder ids.
	static const QString allowed = QStringLiteral("abcdefghijklmnopqrstuvwxyz"
						      "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.");
	bool clean = !id.isEmpty() && id.size() <= 64;
	for (QChar c : id)
		if (!allowed.contains(c))
			clean = false;
	e.encoderId = clean ? id : QStringLiteral("auto");
	e.videoBitrate = clampVideoBitrate(o.value("video_bitrate").toInt(6000));
	e.audioBitrate = clampAudioBitrate(o.value("audio_bitrate").toInt(160));
	return e;
}

static QString cleanPlatform(const QString &id)
{
	return findPreset(id) ? id : customPreset().id;
}

static QString cleanName(const QString &name, const QString &platform)
{
	QString n = name.simplified().left(60);
	if (n.isEmpty()) {
		const PlatformPreset *p = findPreset(platform);
		n = p ? p->name : QStringLiteral("Destination");
	}
	return n;
}

QJsonObject toConfigJson(const Setup &s)
{
	QJsonObject root;
	root["config_version"] = kConfigVersion;
	root["shared_encoder"] = encoderToJson(s.shared);
	root["link_main_stream"] = s.linkMainStream;
	if (!s.hotkeys.isUndefined() && !s.hotkeys.isNull())
		root["hotkeys"] = s.hotkeys;

	QJsonArray arr;
	for (const auto &d : s.destinations) {
		QJsonObject o;
		o["id"] = d.id;
		o["name"] = d.name;
		o["platform"] = d.platform;
		o["server"] = d.server;
		o["key"] = d.key;
		o["enabled"] = d.enabled;
		o["custom_encoder"] = d.useCustomEncoder;
		o["encoder_settings"] = encoderToJson(d.encoder);
		arr.append(o);
	}
	root["destinations"] = arr;
	return root;
}

Setup fromConfigJson(const QJsonObject &root)
{
	Setup s;
	s.shared = encoderFromJson(root.value("shared_encoder").toObject());
	s.linkMainStream = root.value("link_main_stream").toBool(false);
	s.hotkeys = root.value("hotkeys");

	const QJsonArray list = root.value("destinations").toArray();
	for (qsizetype i = 0; i < list.size(); i++) {
		const QJsonObject o = list.at(i).toObject();
		Destination d;
		d.id = o.value("id").toString();
		if (d.id.isEmpty())
			d.id = newId();
		d.platform = cleanPlatform(o.value("platform").toString());
		d.name = cleanName(o.value("name").toString(), d.platform);
		d.server = o.value("server").toString().trimmed();
		d.key = o.value("key").toString().trimmed();
		d.enabled = o.value("enabled").toBool(true);
		d.useCustomEncoder = o.value("custom_encoder").toBool(false);
		d.encoder = encoderFromJson(o.value("encoder_settings").toObject());
		s.destinations.append(d);
	}
	return s;
}

QString exportableServer(const Destination &d)
{
	const PlatformPreset *p = findPreset(d.platform);
	if (!p || p->server.isEmpty())
		return QString();
	return d.server.trimmed() == p->server ? p->server : QString();
}

QJsonObject toExportJson(const Setup &s, const QString &pluginVersion)
{
	QJsonObject root;
	root["format"] = QString::fromLatin1(kExportFormat);
	root["format_version"] = kExportVersion;
	root["created_with"] = QStringLiteral("Multistream for OBS %1").arg(pluginVersion);
	root["note"] = QStringLiteral("This file has NO stream keys. Import it, then paste your own key "
				      "into each destination.");
	root["shared_encoder"] = encoderToJson(s.shared);

	QJsonArray arr;
	for (const auto &d : s.destinations) {
		// Whitelist only. Never copy the whole object: new secret fields
		// added later must not leak into exports by accident.
		QJsonObject o;
		o["name"] = d.name;
		o["platform"] = d.platform;
		const QString server = exportableServer(d);
		if (!server.isEmpty())
			o["server"] = server;
		o["enabled"] = d.enabled;
		o["custom_encoder"] = d.useCustomEncoder;
		if (d.useCustomEncoder)
			o["encoder_settings"] = encoderToJson(d.encoder);
		arr.append(o);
	}
	root["destinations"] = arr;
	return root;
}

QByteArray toExportBytes(const Setup &s, const QString &pluginVersion)
{
	return QJsonDocument(toExportJson(s, pluginVersion)).toJson(QJsonDocument::Indented);
}

ImportResult fromExportBytes(const QByteArray &bytes)
{
	ImportResult r;
	if (bytes.size() > 1024 * 1024) {
		r.error = QStringLiteral("That file is too big to be a Multistream setup.");
		return r;
	}
	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		r.error = QStringLiteral("That file isn't a Multistream setup (it isn't valid JSON).");
		return r;
	}
	const QJsonObject root = doc.object();
	if (root.value("format").toString() != QLatin1String(kExportFormat)) {
		r.error = QStringLiteral("That file isn't a Multistream setup export.");
		return r;
	}
	if (root.value("format_version").toInt(0) > kExportVersion) {
		r.error = QStringLiteral("That setup was made by a newer version of Multistream. "
					 "Please update the plugin first.");
		return r;
	}
	if (root.contains("shared_encoder")) {
		r.shared = encoderFromJson(root.value("shared_encoder").toObject());
		r.hasShared = true;
	}
	const QJsonArray arr = root.value("destinations").toArray();
	for (int i = 0; i < arr.size() && i < 50; i++) {
		const QJsonObject o = arr.at(i).toObject();
		Destination d;
		d.id = newId();
		d.platform = cleanPlatform(o.value("platform").toString());
		d.name = cleanName(o.value("name").toString(), d.platform);
		// Keys are ignored even if someone hand-edited one into the file.
		// Servers are only taken when they are the platform's public default.
		const PlatformPreset *p = findPreset(d.platform);
		d.server = p ? p->server : QString();
		d.key.clear();
		d.enabled = o.value("enabled").toBool(true);
		d.useCustomEncoder = o.value("custom_encoder").toBool(false);
		if (d.useCustomEncoder)
			d.encoder = encoderFromJson(o.value("encoder_settings").toObject());
		r.destinations.append(d);
	}
	r.ok = true;
	return r;
}

} // namespace model
