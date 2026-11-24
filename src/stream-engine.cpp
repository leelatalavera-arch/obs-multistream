/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society
SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stream-engine.hpp"
#include "plugin-support.h"

#include <cstring>

#include <set>
#include <vector>

namespace {

// Best first. Ids that aren't installed are skipped, ids that fail to
// initialise (no GPU, driver too old, ...) fall through to the next one.
const char *const kAutoVideoEncoders[] = {
	"obs_nvenc_h264_tex", // NVIDIA (OBS 31+)
	"jim_nvenc",          // NVIDIA (older OBS)
	"h264_texture_amf",   // AMD
	"obs_qsv11_v2",       // Intel QuickSync
	"com.apple.videotoolbox.videoencoder.ave.avc", // Apple hardware
	"obs_nvenc_h264_cuda", // NVIDIA (Linux)
	"ffmpeg_vaapi_tex",    // Linux VA-API
	"obs_x264",            // CPU, always last
};

const char *const kAudioEncoders[] = {"ffmpeg_aac", "CoreAudio_AAC", "libfdk_aac"};

bool encoderInstalled(const char *id)
{
	const char *type = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &type); i++)
		if (type && strcmp(type, id) == 0)
			return true;
	return false;
}

QString encoderLabel(const char *id)
{
	const char *n = obs_encoder_get_display_name(id);
	return n ? QString::fromUtf8(n) : QString::fromUtf8(id);
}

QString stopReason(int code, const char *lastError)
{
	QString why;
	switch (code) {
	case OBS_OUTPUT_SUCCESS:
		return QString();
	case OBS_OUTPUT_BAD_PATH:
		why = QStringLiteral("The server URL looks wrong.");
		break;
	case OBS_OUTPUT_CONNECT_FAILED:
		why = QStringLiteral("Couldn't connect. Check the server URL and your internet.");
		break;
	case OBS_OUTPUT_INVALID_STREAM:
		why = QStringLiteral("The platform refused the stream key. Paste a fresh key.");
		break;
	case OBS_OUTPUT_DISCONNECTED:
		why = QStringLiteral("Connection lost and couldn't reconnect.");
		break;
	case OBS_OUTPUT_UNSUPPORTED:
		why = QStringLiteral("This platform doesn't support these settings (try H.264).");
		break;
	case OBS_OUTPUT_NO_SPACE:
		why = QStringLiteral("Out of disk space.");
		break;
	case OBS_OUTPUT_ENCODE_ERROR:
		why = QStringLiteral("The video encoder stopped working.");
		break;
	default:
		why = QStringLiteral("Stopped with an error (code %1).").arg(code);
		break;
	}
	if (lastError && *lastError)
		why += QStringLiteral(" (%1)").arg(QString::fromUtf8(lastError));
	return why;
}

void applyEncoderDefaults(obs_data_t *s, const char *id, int bitrate)
{
	obs_data_set_string(s, "rate_control", "CBR");
	obs_data_set_int(s, "bitrate", bitrate);
	obs_data_set_int(s, "keyint_sec", 2);
	if (strcmp(id, "obs_x264") == 0)
		obs_data_set_string(s, "preset", "veryfast");
	if (strstr(id, "nvenc"))
		obs_data_set_string(s, "preset2", "p5");
}

} // namespace

QString describeStatus(const StreamStatus &s)
{
	switch (s.state) {
	case StreamState::Off:
		return QStringLiteral("Off");
	case StreamState::Connecting:
		return QStringLiteral("Connecting…");
	case StreamState::Live:
		return QStringLiteral("LIVE");
	case StreamState::Reconnecting:
		return QStringLiteral("Reconnecting…");
	case StreamState::Stopping:
		return QStringLiteral("Stopping…");
	case StreamState::Stopped:
		return QStringLiteral("Stopped");
	case StreamState::Skipped:
		return QStringLiteral("Skipped: ") + s.detail;
	case StreamState::Failed:
		return s.detail.isEmpty() ? QStringLiteral("Failed") : QStringLiteral("Failed: ") + s.detail;
	}
	return QString();
}

StreamEngine::~StreamEngine()
{
	shutdown();
}

QList<EncoderChoice> StreamEngine::availableVideoEncoders()
{
	QList<EncoderChoice> list;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
		if (!id || obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
			continue;
		const uint32_t caps = obs_get_encoder_caps(id);
		if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
			continue;
		const char *codec = obs_get_encoder_codec(id);
		if (!codec || (strcmp(codec, "h264") != 0 && strcmp(codec, "hevc") != 0 && strcmp(codec, "av1") != 0))
			continue;
		QString label = encoderLabel(id);
		if (strcmp(codec, "h264") != 0)
			label += QStringLiteral(" — not every platform accepts %1").arg(QString::fromUtf8(codec).toUpper());
		list.append({QString::fromUtf8(id), label});
	}
	return list;
}

QString StreamEngine::groupKeyFor(const EncoderSettings &e)
{
	return QStringLiteral("%1|%2|%3").arg(e.encoderId).arg(e.videoBitrate).arg(e.audioBitrate);
}

void StreamEngine::setStatus(const QString &destId, StreamState st, const QString &detail)
{
	std::lock_guard<std::mutex> lock(statusMutex_);
	statuses_[destId] = {st, detail};
}

StreamStatus StreamEngine::status(const QString &destId) const
{
	std::lock_guard<std::mutex> lock(statusMutex_);
	auto it = statuses_.find(destId);
	return it == statuses_.end() ? StreamStatus{} : it->second;
}

bool StreamEngine::isActive(const QString &destId) const
{
	auto it = outs_.find(destId);
	return it != outs_.end() && it->second.output && obs_output_active(it->second.output);
}

bool StreamEngine::anyActive() const
{
	for (const auto &kv : outs_)
		if (kv.second.output && obs_output_active(kv.second.output))
			return true;
	return false;
}

QString StreamEngine::encoderLabelFor(const QString &destId) const
{
	auto it = outs_.find(destId);
	if (it == outs_.end())
		return QString();
	auto g = groups_.find(it->second.groupKey);
	return g == groups_.end() ? QString() : g->second.label;
}

void StreamEngine::onStart(void *data, calldata_t *)
{
	auto *ctx = static_cast<SignalCtx *>(data);
	ctx->engine->setStatus(ctx->destId, StreamState::Live);
	obs_log(LOG_INFO, "[%s] live", ctx->name.toUtf8().constData());
}

void StreamEngine::onStopping(void *data, calldata_t *)
{
	auto *ctx = static_cast<SignalCtx *>(data);
	ctx->engine->setStatus(ctx->destId, StreamState::Stopping);
}

void StreamEngine::onStop(void *data, calldata_t *cd)
{
	auto *ctx = static_cast<SignalCtx *>(data);
	const int code = (int)calldata_int(cd, "code");
	auto *output = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	const char *lastError = output ? obs_output_get_last_error(output) : nullptr;
	if (code == OBS_OUTPUT_SUCCESS) {
		ctx->engine->setStatus(ctx->destId, StreamState::Stopped);
		obs_log(LOG_INFO, "[%s] stopped", ctx->name.toUtf8().constData());
	} else {
		const QString why = stopReason(code, lastError);
		ctx->engine->setStatus(ctx->destId, StreamState::Failed, why);
		obs_log(LOG_WARNING, "[%s] stopped with error %d: %s", ctx->name.toUtf8().constData(), code,
			why.toUtf8().constData());
	}
}

void StreamEngine::onReconnect(void *data, calldata_t *)
{
	auto *ctx = static_cast<SignalCtx *>(data);
	ctx->engine->setStatus(ctx->destId, StreamState::Reconnecting);
	obs_log(LOG_WARNING, "[%s] reconnecting", ctx->name.toUtf8().constData());
}

void StreamEngine::onReconnectOk(void *data, calldata_t *)
{
	auto *ctx = static_cast<SignalCtx *>(data);
	ctx->engine->setStatus(ctx->destId, StreamState::Live);
}

void StreamEngine::destroyOutput(const QString &destId)
{
	auto it = outs_.find(destId);
	if (it == outs_.end())
		return;
	Out &o = it->second;
	if (o.output) {
		signal_handler_t *sh = obs_output_get_signal_handler(o.output);
		signal_handler_disconnect(sh, "start", onStart, o.ctx.get());
		signal_handler_disconnect(sh, "stopping", onStopping, o.ctx.get());
		signal_handler_disconnect(sh, "stop", onStop, o.ctx.get());
		signal_handler_disconnect(sh, "reconnect", onReconnect, o.ctx.get());
		signal_handler_disconnect(sh, "reconnect_success", onReconnectOk, o.ctx.get());
		if (obs_output_active(o.output))
			obs_output_force_stop(o.output);
		obs_output_release(o.output);
	}
	if (o.service)
		obs_service_release(o.service);
	outs_.erase(it);
}

void StreamEngine::purgeUnusedGroups()
{
	std::set<QString> used;
	for (const auto &kv : outs_)
		used.insert(kv.second.groupKey);
	for (auto it = groups_.begin(); it != groups_.end();) {
		if (used.count(it->first)) {
			++it;
			continue;
		}
		if (it->second.video)
			obs_encoder_release(it->second.video);
		if (it->second.audio)
			obs_encoder_release(it->second.audio);
		it = groups_.erase(it);
	}
}

StreamEngine::EncGroup *StreamEngine::ensureGroupFor(const QString &key, const EncoderSettings &e, Out &out,
						     QString *error, bool *alreadyStarted)
{
	*alreadyStarted = false;
	auto existing = groups_.find(key);
	if (existing != groups_.end()) {
		obs_output_set_video_encoder(out.output, existing->second.video);
		obs_output_set_audio_encoder(out.output, existing->second.audio, 0);
		return &existing->second;
	}

	// Audio encoder
	obs_encoder_t *aenc = nullptr;
	for (const char *aid : kAudioEncoders) {
		if (!encoderInstalled(aid))
			continue;
		obs_data_t *as = obs_data_create();
		obs_data_set_int(as, "bitrate", e.audioBitrate);
		aenc = obs_audio_encoder_create(aid, "multistream-audio", as, 0, nullptr);
		obs_data_release(as);
		if (aenc)
			break;
	}
	if (!aenc) {
		*error = QStringLiteral("No AAC audio encoder is available in this OBS.");
		return nullptr;
	}
	obs_encoder_set_audio(aenc, obs_get_audio());

	// Video encoder candidates
	std::vector<std::string> candidates;
	const std::string wanted = e.encoderId.toStdString();
	if (wanted != "auto" && encoderInstalled(wanted.c_str()))
		candidates.push_back(wanted);
	for (const char *id : kAutoVideoEncoders)
		if (wanted != id && encoderInstalled(id))
			candidates.push_back(id);
	if (wanted != "auto" && !encoderInstalled(wanted.c_str()))
		obs_log(LOG_WARNING, "encoder '%s' is not installed, using Auto", wanted.c_str());

	for (const auto &id : candidates) {
		obs_data_t *vs = obs_data_create();
		applyEncoderDefaults(vs, id.c_str(), e.videoBitrate);
		obs_encoder_t *venc = obs_video_encoder_create(id.c_str(), "multistream-video", vs, nullptr);
		obs_data_release(vs);
		if (!venc)
			continue;
		obs_encoder_set_video(venc, obs_get_video());
		obs_output_set_video_encoder(out.output, venc);
		obs_output_set_audio_encoder(out.output, aenc, 0);
		if (obs_output_start(out.output)) {
			EncGroup g;
			g.video = venc;
			g.audio = aenc;
			g.label = encoderLabel(id.c_str());
			obs_log(LOG_INFO, "video encoder: %s (%d kbps)", id.c_str(), e.videoBitrate);
			*alreadyStarted = true;
			return &(groups_[key] = g);
		}
		const char *err = obs_output_get_last_error(out.output);
		obs_log(LOG_WARNING, "encoder %s failed to start: %s", id.c_str(), err ? err : "(no details)");
		obs_output_set_video_encoder(out.output, nullptr);
		obs_encoder_release(venc);
	}
	obs_output_set_audio_encoder(out.output, nullptr, 0);
	obs_encoder_release(aenc);
	*error = QStringLiteral("No video encoder could start. Open Help > Log Files for details.");
	return nullptr;
}

bool StreamEngine::start(const Destination &d, const EncoderSettings &enc, QString *error)
{
	QString err;
	if (isActive(d.id)) {
		return true;
	}
	destroyOutput(d.id); // drop a previous, finished output for this id
	purgeUnusedGroups();

	if (!model::hasServer(d) || !model::hasKey(d)) {
		err = !model::hasServer(d) ? QStringLiteral("needs a server URL (click Edit)")
					   : QStringLiteral("needs your stream key (click Edit)");
		setStatus(d.id, StreamState::Skipped, err);
		obs_log(LOG_INFO, "[%s] skipped: %s", d.name.toUtf8().constData(), err.toUtf8().constData());
		if (error)
			*error = err;
		return false;
	}

	Out o;
	o.ctx = std::make_unique<SignalCtx>(SignalCtx{this, d.id, d.name});
	o.groupKey = groupKeyFor(enc);

	obs_data_t *ss = obs_data_create();
	obs_data_set_string(ss, "server", d.server.trimmed().toUtf8().constData());
	obs_data_set_string(ss, "key", d.key.trimmed().toUtf8().constData());
	o.service = obs_service_create_private("rtmp_custom", "multistream-service", ss);
	obs_data_release(ss);

	const QString outName = QStringLiteral("multistream: %1").arg(d.name);
	o.output = obs_output_create("rtmp_output", outName.toUtf8().constData(), nullptr, nullptr);
	if (!o.service || !o.output) {
		if (o.output)
			obs_output_release(o.output);
		if (o.service)
			obs_service_release(o.service);
		err = QStringLiteral("OBS couldn't create an RTMP output.");
		setStatus(d.id, StreamState::Failed, err);
		if (error)
			*error = err;
		return false;
	}
	obs_output_set_service(o.output, o.service);
	obs_output_set_reconnect_settings(o.output, 20, 5);

	signal_handler_t *sh = obs_output_get_signal_handler(o.output);
	signal_handler_connect(sh, "start", onStart, o.ctx.get());
	signal_handler_connect(sh, "stopping", onStopping, o.ctx.get());
	signal_handler_connect(sh, "stop", onStop, o.ctx.get());
	signal_handler_connect(sh, "reconnect", onReconnect, o.ctx.get());
	signal_handler_connect(sh, "reconnect_success", onReconnectOk, o.ctx.get());

	setStatus(d.id, StreamState::Connecting);
	const QString key = o.groupKey;
	auto &slot = (outs_[d.id] = std::move(o));

	bool started = false;
	EncGroup *g = ensureGroupFor(key, enc, slot, &err, &started);
	if (g && !started) {
		started = obs_output_start(slot.output);
		if (!started) {
			const char *le = obs_output_get_last_error(slot.output);
			err = le && *le ? QString::fromUtf8(le) : QStringLiteral("OBS couldn't start this output.");
		}
	}
	if (!started) {
		setStatus(d.id, StreamState::Failed, err);
		obs_log(LOG_WARNING, "[%s] could not start: %s", d.name.toUtf8().constData(), err.toUtf8().constData());
		destroyOutput(d.id);
		purgeUnusedGroups();
		if (error)
			*error = err;
		return false;
	}
	return true;
}

void StreamEngine::stop(const QString &destId)
{
	auto it = outs_.find(destId);
	if (it == outs_.end() || !it->second.output)
		return;
	// Inactive outputs have already stopped (or failed); keep their status
	// so the user can still read why it failed.
	if (obs_output_active(it->second.output)) {
		setStatus(destId, StreamState::Stopping);
		obs_output_stop(it->second.output);
	}
}

void StreamEngine::stopAll()
{
	std::vector<QString> ids;
	for (const auto &kv : outs_)
		ids.push_back(kv.first);
	for (const auto &id : ids)
		stop(id);
}

void StreamEngine::forget(const QString &destId)
{
	destroyOutput(destId);
	purgeUnusedGroups();
	std::lock_guard<std::mutex> lock(statusMutex_);
	statuses_.erase(destId);
}

void StreamEngine::shutdown()
{
	std::vector<QString> ids;
	for (const auto &kv : outs_)
		ids.push_back(kv.first);
	for (const auto &id : ids)
		destroyOutput(id);
	purgeUnusedGroups();
	std::lock_guard<std::mutex> lock(statusMutex_);
	statuses_.clear();
}
