/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society
SPDX-License-Identifier: GPL-2.0-or-later

Owns the extra RTMP outputs. Destinations with identical encoder settings
share one encoder ("encode once, send many").
*/

#pragma once

#include "destinations.hpp"

#include <obs.h>

#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <mutex>

enum class StreamState { Off, Connecting, Live, Reconnecting, Stopping, Stopped, Failed, Skipped };

struct StreamStatus {
	StreamState state = StreamState::Off;
	QString detail; // human friendly text for errors
};

struct EncoderChoice {
	QString id;
	QString label;
};

class StreamEngine {
public:
	StreamEngine() = default;
	~StreamEngine();
	StreamEngine(const StreamEngine &) = delete;
	StreamEngine &operator=(const StreamEngine &) = delete;

	// Returns false (and a friendly reason) if it could not even begin.
	bool start(const Destination &d, const EncoderSettings &enc, QString *error);
	void stop(const QString &destId);
	void stopAll();
	// Force-stop and free everything (OBS exit / profile switch / unload).
	void shutdown();

	bool isActive(const QString &destId) const;
	bool anyActive() const;
	StreamStatus status(const QString &destId) const;
	QString encoderLabelFor(const QString &destId) const;
	void forget(const QString &destId); // remove a deleted destination

	// Video encoders installed in this OBS, for the settings dropdown.
	static QList<EncoderChoice> availableVideoEncoders();

private:
	struct EncGroup {
		obs_encoder_t *video = nullptr;
		obs_encoder_t *audio = nullptr;
		QString label;
	};
	struct SignalCtx {
		StreamEngine *engine;
		QString destId;
		QString name;
	};
	struct Out {
		obs_output_t *output = nullptr;
		obs_service_t *service = nullptr;
		QString groupKey;
		std::unique_ptr<SignalCtx> ctx;
	};

	static QString groupKeyFor(const EncoderSettings &e);
	EncGroup *ensureGroupFor(const QString &key, const EncoderSettings &e, Out &out, QString *error,
				 bool *alreadyStarted);
	void destroyOutput(const QString &destId);
	void purgeUnusedGroups();
	void setStatus(const QString &destId, StreamState st, const QString &detail = QString());

	static void onStart(void *data, calldata_t *cd);
	static void onStop(void *data, calldata_t *cd);
	static void onStopping(void *data, calldata_t *cd);
	static void onReconnect(void *data, calldata_t *cd);
	static void onReconnectOk(void *data, calldata_t *cd);

	std::map<QString, Out> outs_;
	std::map<QString, EncGroup> groups_;
	mutable std::mutex statusMutex_;
	std::map<QString, StreamStatus> statuses_;
};

QString describeStatus(const StreamStatus &s);
