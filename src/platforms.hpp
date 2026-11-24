/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <QString>
#include <QList>

// A built-in platform preset. Nothing here is personal: these are the
// public ingest servers each platform documents, plus links to the official
// page where a user finds THEIR OWN stream key.
struct PlatformPreset {
	QString id;      // stable id saved in config/export ("twitch")
	QString name;    // shown in the UI ("Twitch")
	QString server;  // public default server, empty = user must paste one
	QString keyHelp; // one-line "where is my key" text
	QString keyUrl;  // official page where the key is found
	bool keyChanges; // true when the platform issues a new key every stream
};

const QList<PlatformPreset> &platformPresets();
const PlatformPreset *findPreset(const QString &id);
const PlatformPreset &customPreset();
