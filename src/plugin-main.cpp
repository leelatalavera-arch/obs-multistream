/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society

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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "destinations.hpp"
#include "multistream-dock.hpp"
#include "stream-engine.hpp"

#include <QDir>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMessageBox>
#include <QSaveFile>
#include <QTimer>

#include <functional>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Stream to several platforms at once from a dock.";
}

namespace {

// Oldest OBS this build supports. Keep in sync with README/INSTALL and the
// Windows installer check.
constexpr uint32_t kMinObsVersion = MAKE_SEMANTIC_VERSION(31, 0, 0);
constexpr const char *kMinObsText = "31.0";
constexpr const char *kDockId = "obs-multistream-dock";
constexpr const char *kConfigFile = "obs-multistream.json";

struct State {
	Setup setup;
	StreamEngine engine;
	MultistreamDock *dock = nullptr;
	QString configPath;
	obs_hotkey_id hkStart = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id hkStop = OBS_INVALID_HOTKEY_ID;
	bool loaded = false;
};
State *g = nullptr;

QString currentConfigPath()
{
	char *dir = obs_frontend_get_current_profile_path();
	if (!dir)
		return QString();
	QString path = QDir(QString::fromUtf8(dir)).filePath(QString::fromLatin1(kConfigFile));
	bfree(dir);
	return path;
}

QJsonValue hotkeyToJson(obs_hotkey_id id)
{
	obs_data_array_t *arr = obs_hotkey_save(id);
	obs_data_t *wrap = obs_data_create();
	obs_data_set_array(wrap, "b", arr);
	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(obs_data_get_json(wrap)));
	obs_data_release(wrap);
	obs_data_array_release(arr);
	return doc.object().value("b");
}

void hotkeyFromJson(obs_hotkey_id id, const QJsonValue &v)
{
	QJsonObject wrapObj;
	wrapObj["b"] = v.isArray() ? v : QJsonArray();
	obs_data_t *wrap =
		obs_data_create_from_json(QJsonDocument(wrapObj).toJson(QJsonDocument::Compact).constData());
	obs_data_array_t *arr = wrap ? obs_data_get_array(wrap, "b") : nullptr;
	obs_hotkey_load(id, arr);
	obs_data_array_release(arr);
	obs_data_release(wrap);
}

void saveConfig()
{
	if (!g || !g->loaded || g->configPath.isEmpty())
		return;
	QJsonObject hk;
	hk["start_all"] = hotkeyToJson(g->hkStart);
	hk["stop_all"] = hotkeyToJson(g->hkStop);
	g->setup.hotkeys = hk;

	QSaveFile f(g->configPath);
	if (!f.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "could not write %s", g->configPath.toUtf8().constData());
		return;
	}
	f.write(QJsonDocument(model::toConfigJson(g->setup)).toJson(QJsonDocument::Indented));
	if (!f.commit()) {
		obs_log(LOG_WARNING, "could not save %s", g->configPath.toUtf8().constData());
		return;
	}
	// The file holds stream keys: owner read/write only (no-op on Windows).
	QFile::setPermissions(g->configPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void loadConfig()
{
	g->configPath = currentConfigPath();
	const bool firstRun = !g->configPath.isEmpty() && !QFile::exists(g->configPath);
	Setup fresh;
	QFile f(g->configPath);
	if (!g->configPath.isEmpty() && f.open(QIODevice::ReadOnly)) {
		const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
		if (doc.isObject())
			fresh = model::fromConfigJson(doc.object());
		else
			obs_log(LOG_WARNING, "config file unreadable, starting fresh");
	}
	// No file yet = brand-new user: empty list, first-run guide shows.
	g->setup = fresh;
	g->loaded = true;

	const QJsonObject hk = g->setup.hotkeys.toObject();
	hotkeyFromJson(g->hkStart, hk.value("start_all"));
	hotkeyFromJson(g->hkStop, hk.value("stop_all"));

	obs_log(LOG_INFO, "loaded %d destination(s) for this profile", (int)g->setup.destinations.size());
	if (g->dock)
		g->dock->reload();

	if (firstRun && g->dock) {
		// First time in this profile: open the dock so the user sees the
		// "Add your first platform" guide instead of hunting in Docks menu.
		if (auto *dw = qobject_cast<QDockWidget *>(g->dock->parentWidget())) {
			dw->setVisible(true);
			dw->raise();
		}
		saveConfig(); // creates the file, so this only happens once
	}
}

void runOnUi(std::function<void()> fn)
{
	if (g && g->dock)
		QMetaObject::invokeMethod(g->dock, std::move(fn), Qt::QueuedConnection);
}

void onHotkeyStart(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		runOnUi([]() {
			if (g && g->dock)
				g->dock->startAll();
		});
}

void onHotkeyStop(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		runOnUi([]() {
			if (g && g->dock)
				g->dock->stopAll();
		});
}

// Test hook used by the automated end-to-end test (tests/e2e). Does nothing
// unless the environment variable is set.
void maybeRunTestHooks()
{
	const QByteArray autostart = qgetenv("MULTISTREAM_TEST_AUTOSTART");
	if (autostart.isEmpty() || !g->dock)
		return;
	obs_log(LOG_INFO, "TEST: autostart in %s ms", autostart.constData());
	QTimer::singleShot(autostart.toInt(), g->dock, []() { g->dock->startAll(); });
	const int exitAfter = qgetenv("MULTISTREAM_TEST_EXIT_AFTER").toInt();
	if (exitAfter > 0) {
		QTimer::singleShot(exitAfter, g->dock, []() {
			g->dock->stopAll();
			QTimer::singleShot(3000, []() {
				auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
				if (main)
					main->close();
			});
		});
	}
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (!g)
		return;
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		loadConfig();
		maybeRunTestHooks();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		saveConfig();
		g->engine.shutdown();
		g->loaded = false;
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		loadConfig();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		if (g->loaded && g->setup.linkMainStream && g->dock)
			g->dock->startAll();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		if (g->loaded && g->setup.linkMainStream && g->dock)
			g->dock->stopAll();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		saveConfig();
		g->engine.shutdown();
		g->loaded = false;
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------- too old
void onFrontendEventTooOld(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
		return;
	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	QMessageBox::information(main, QStringLiteral("Multistream for OBS"),
				 QStringLiteral("<b>Multistream needs a newer OBS.</b><br><br>"
						"It needs OBS %1 or newer, and you have OBS %2.<br>"
						"Update OBS from <a href=\"https://obsproject.com/download\">"
						"obsproject.com/download</a>. Your scenes and settings are kept.<br><br>"
						"Multistream is switched off until then. Nothing else is affected.")
					 .arg(QString::fromLatin1(kMinObsText),
					      QString::fromUtf8(obs_get_version_string())));
}

} // namespace

static bool g_tooOld = false;

bool obs_module_load(void)
{
	// MULTISTREAM_TEST_PRETEND_OLD_OBS lets the automated test exercise the
	// "OBS too old" path on a current OBS.
	g_tooOld = obs_get_version() < kMinObsVersion || !qgetenv("MULTISTREAM_TEST_PRETEND_OLD_OBS").isEmpty();
	if (g_tooOld) {
		obs_log(LOG_WARNING, "OBS %s is older than %s; plugin disabled", obs_get_version_string(),
			kMinObsText);
		obs_frontend_add_event_callback(onFrontendEventTooOld, nullptr);
		return true; // stay loaded so the friendly message can show; do nothing else
	}

	g = new State();
	g->hkStart = obs_hotkey_register_frontend("obs_multistream.start_all", "Multistream: Start all",
						  onHotkeyStart, nullptr);
	g->hkStop = obs_hotkey_register_frontend("obs_multistream.stop_all", "Multistream: Stop all", onHotkeyStop,
						 nullptr);

	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	g->dock = new MultistreamDock(g->setup, g->engine, []() { saveConfig(); }, main);
	if (!obs_frontend_add_dock_by_id(kDockId, "Multistream", g->dock)) {
		obs_log(LOG_WARNING, "could not add dock");
	}

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	obs_log(LOG_INFO, "plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	// Frontend callbacks are already cleared by OBS at shutdown.
	if (g_tooOld || !g)
		return;
	g->engine.shutdown();
	obs_hotkey_unregister(g->hkStart);
	obs_hotkey_unregister(g->hkStop);
	// The dock widget is owned and deleted by OBS's main window.
	g->dock = nullptr;
	delete g;
	g = nullptr;
	obs_log(LOG_INFO, "plugin unloaded");
}
