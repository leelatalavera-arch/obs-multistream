/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society
SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "platforms.hpp"

// Servers come from OBS's own rtmp-services list and each platform's help
// pages. Platforms that hand out a personal or per-stream server URL
// (Kick, TikTok, Instagram) have an empty default: the user pastes theirs.
const QList<PlatformPreset> &platformPresets()
{
	static const QList<PlatformPreset> presets = {
		{"twitch", "Twitch", "rtmp://live.twitch.tv/app",
		 "Creator Dashboard > Settings > Stream > Primary Stream key.",
		 "https://dashboard.twitch.tv/settings/stream", false},
		{"youtube", "YouTube", "rtmps://a.rtmps.youtube.com:443/live2",
		 "YouTube Studio > Create > Go live > Stream settings > Stream key.",
		 "https://support.google.com/youtube/answer/2907883", false},
		{"kick", "Kick", "",
		 "Kick dashboard > Settings > Stream URL & Key. Paste BOTH the URL and the key.",
		 "https://help.kick.com/en/articles/7066931-how-to-stream-on-kick-com", false},
		{"tiktok", "TikTok", "",
		 "TikTok LIVE Producer (on a computer). The server URL and key change EVERY stream.",
		 "https://www.tiktok.com/live/creator-networks/en/blog/how-to-go-live-on-tiktok", true},
		{"instagram", "Instagram", "",
		 "Instagram on a computer > Create > Live video. The URL and key change EVERY stream.",
		 "https://about.instagram.com/blog/tips-and-tricks/instagram-live-producer", true},
		{"facebook", "Facebook", "rtmps://rtmp-api.facebook.com:443/rtmp/",
		 "Facebook Live Producer > Streaming software > Stream key.",
		 "https://www.facebook.com/live/producer", false},
		{"x", "X (Twitter)", "",
		 "X Media Studio > Producer > Sources. Paste the RTMP URL and key.",
		 "https://studio.x.com/producer", false},
	};
	return presets;
}

const PlatformPreset &customPreset()
{
	static const PlatformPreset custom = {
		"custom", "Custom RTMP", "",
		"Any service that gives you an RTMP or RTMPS server URL and a stream key.", "", false};
	return custom;
}

const PlatformPreset *findPreset(const QString &id)
{
	for (const auto &p : platformPresets())
		if (p.id == id)
			return &p;
	if (id == customPreset().id)
		return &customPreset();
	return nullptr;
}
