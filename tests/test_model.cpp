// Unit tests for the destination model. Qt Core only, no OBS required.
// Main point: an export must NEVER contain a stream key or a private URL.

#include "destinations.hpp"
#include "platforms.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                              \
	do {                                                                     \
		if (!(cond)) {                                                   \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                              \
		} else {                                                         \
			std::printf("ok   %s\n", #cond);                         \
		}                                                                \
	} while (0)

static Setup sampleSetup()
{
	Setup s;
	s.shared.encoderId = "obs_x264";
	s.shared.videoBitrate = 4500;
	Destination twitch;
	twitch.id = model::newId();
	twitch.name = "My Twitch";
	twitch.platform = "twitch";
	twitch.server = findPreset("twitch")->server;
	twitch.key = "live_SECRET_TWITCH_KEY_123";
	Destination kick;
	kick.id = model::newId();
	kick.name = "Kick main";
	kick.platform = "kick";
	kick.server = "rtmps://abc123private.global-contribute.live-video.net:443/app/";
	kick.key = "sk_SECRET_KICK_KEY";
	kick.useCustomEncoder = true;
	kick.encoder.videoBitrate = 8000;
	Destination custom;
	custom.id = model::newId();
	custom.name = "Home server";
	custom.platform = "custom";
	custom.server = "rtmp://192.168.1.50/live/SECRET_IN_URL";
	custom.key = "SECRET_CUSTOM";
	Destination yt;
	yt.id = model::newId();
	yt.name = "YouTube";
	yt.platform = "youtube";
	yt.server = "rtmps://a.rtmps.youtube.com:443/live2?user=SECRET_PARAM"; // edited, not the default
	yt.key = "SECRET_YT";
	s.destinations = {twitch, kick, custom, yt};
	return s;
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const Setup s = sampleSetup();

	// ---- export never contains secrets
	const QByteArray exported = model::toExportBytes(s, "test");
	CHECK(!exported.contains("SECRET"));
	CHECK(!exported.contains("\"key\""));
	CHECK(!exported.contains("192.168"));
	CHECK(!exported.contains("abc123private"));
	CHECK(!exported.contains("\"id\""));
	CHECK(exported.contains("rtmp://live.twitch.tv/app")); // public default is fine
	CHECK(exported.contains("My Twitch"));

	// ---- round trip through import
	const model::ImportResult r = model::fromExportBytes(exported);
	CHECK(r.ok);
	CHECK(r.destinations.size() == 4);
	CHECK(r.hasShared && r.shared.videoBitrate == 4500 && r.shared.encoderId == "obs_x264");
	for (const auto &d : r.destinations) {
		CHECK(d.key.isEmpty());
		CHECK(!d.id.isEmpty());
	}
	CHECK(r.destinations[0].server == "rtmp://live.twitch.tv/app");
	CHECK(r.destinations[1].server.isEmpty()); // kick: user pastes their own
	CHECK(r.destinations[1].useCustomEncoder && r.destinations[1].encoder.videoBitrate == 8000);
	CHECK(r.destinations[2].server.isEmpty());
	CHECK(r.destinations[3].server == findPreset("youtube")->server); // reset to public default
	CHECK(r.destinations[0].id != s.destinations[0].id);              // fresh ids

	// ---- hand-edited keys in an import file are ignored
	QJsonObject evil = QJsonDocument::fromJson(exported).object();
	QJsonArray arr = evil["destinations"].toArray();
	QJsonObject first = arr[0].toObject();
	first["key"] = "INJECTED";
	first["server"] = "rtmp://evil.example/app";
	arr[0] = first;
	evil["destinations"] = arr;
	const model::ImportResult r2 = model::fromExportBytes(QJsonDocument(evil).toJson());
	CHECK(r2.ok && r2.destinations[0].key.isEmpty());
	CHECK(r2.destinations[0].server == "rtmp://live.twitch.tv/app");

	// ---- bad input is rejected politely
	CHECK(!model::fromExportBytes("not json").ok);
	CHECK(!model::fromExportBytes("{\"format\":\"something-else\"}").ok);
	CHECK(!model::fromExportBytes("{\"format\":\"obs-multistream-setup\",\"format_version\":99}").ok);
	CHECK(!model::fromExportBytes(QByteArray(2 * 1024 * 1024, ' ')).ok);

	// ---- weird encoder ids are neutralised, bitrates clamped
	QJsonObject enc{{"encoder", "../../evil;rm"}, {"video_bitrate", 999999}, {"audio_bitrate", 1}};
	QJsonObject root{{"format", "obs-multistream-setup"}, {"format_version", 1}, {"shared_encoder", enc}};
	const model::ImportResult r3 = model::fromExportBytes(QJsonDocument(root).toJson());
	CHECK(r3.ok && r3.shared.encoderId == "auto" && r3.shared.videoBitrate == 51000 &&
	      r3.shared.audioBitrate == 64);

	// ---- local config DOES keep keys (it's the user's own profile)
	const Setup back = model::fromConfigJson(model::toConfigJson(s));
	CHECK(back.destinations.size() == 4);
	CHECK(back.destinations[0].key == "live_SECRET_TWITCH_KEY_123");
	CHECK(back.destinations[2].server == "rtmp://192.168.1.50/live/SECRET_IN_URL");
	CHECK(back.destinations[1].useCustomEncoder);

	// ---- fresh config = empty list (first-run guide)
	CHECK(model::fromConfigJson(QJsonObject()).destinations.isEmpty());

	// ---- readiness
	CHECK(model::isReady(s.destinations[0]));
	CHECK(!model::isReady(r.destinations[0])); // imported: no key yet

	std::printf("\n%s (%d failure(s))\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
	return failures ? 1 : 0;
}
