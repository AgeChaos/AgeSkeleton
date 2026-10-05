// AgeChaos proprietary additions; see LICENSE.AgeChaos.txt.
#include "ecs_world.h"

#include "servers/audio/audio_server.h"

bool ECSWorld::set_audio(uint64_t id, const Dictionary &definition) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!is_alive(id)) {
		return false;
	}
	Dictionary data = get_audio(id);
	data.merge(definition, true);
	for (const Variant &key : data.keys()) {
		if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
			return false;
		}
		String field = key;
		if (field != "stream" && field != "bus" && field != "volume_db" && field != "pitch_scale" && field != "playing" && field != "paused" && field != "position" && field != "spatial" && field != "unit_size" && field != "max_distance" && field != "attenuation" && field != "panning_strength" && field != "doppler" && field != "speed_of_sound") {
			return false;
		}
	}
	if (!data.has("stream") || data["stream"].get_type() != Variant::OBJECT) {
		return false;
	}
	Ref<AudioStream> stream = data["stream"];
	if (stream.is_null()) {
		return false;
	}
	Variant bus = data.get("bus", "Master");
	if (bus.get_type() != Variant::STRING && bus.get_type() != Variant::STRING_NAME) {
		return false;
	}
	auto *server = AudioServer::get_singleton();
	if (!server || server->get_bus_index(bus) < 0) {
		return false;
	}
	for (const String &field : { String("volume_db"), String("pitch_scale"), String("position") }) {
		Variant value = data.get(field, field == "pitch_scale" ? 1.0 : 0.0);
		if ((value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) || !Math::is_finite(double(value))) {
			return false;
		}
		double number = value;
		if ((field == "volume_db" && (number < -80 || number > 24)) || (field == "pitch_scale" && (number <= 0 || number > 16)) || (field == "position" && number < 0)) {
			return false;
		}
		data[field] = number;
	}
	for (const String &field : { String("playing"), String("paused"), String("doppler") }) {
		Variant value = data.get(field, field == "playing");
		if (value.get_type() != Variant::BOOL) {
			return false;
		}
		data[field] = value;
	}
	Variant spatial = data.get("spatial", false);
	if (spatial.get_type() != Variant::BOOL) {
		return false;
	}
	data["spatial"] = spatial;
	for (const String &field : { String("unit_size"), String("max_distance"), String("attenuation"), String("panning_strength") }) {
		Variant value = data.get(field, field == "max_distance" ? 0.0 : 1.0);
		if ((value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) || !Math::is_finite(double(value)) || double(value) < 0 || (field == "unit_size" && double(value) <= 0) || (field == "panning_strength" && double(value) > 1)) {
			return false;
		}
		data[field] = double(value);
	}
	Variant sound_speed = data.get("speed_of_sound", 343.0);
	if ((sound_speed.get_type() != Variant::FLOAT && sound_speed.get_type() != Variant::INT) || !Math::is_finite(double(sound_speed)) || double(sound_speed) <= 0) {
		return false;
	}
	data["speed_of_sound"] = double(sound_speed);
	data["bus"] = bus;
	AudioState *existing = audio_sources.getptr(uint32_t(id));
	bool restart = !existing || existing->stream != stream || definition.has("position") || (bool(data["playing"]) && existing->playback.is_null());
	Ref<AudioStreamPlayback> playback = existing ? existing->playback : Ref<AudioStreamPlayback>();
	if (restart && bool(data["playing"])) {
		playback = stream->instantiate_playback();
		if (playback.is_null()) {
			return false;
		}
	}
	if (existing && existing->playback.is_valid() && (restart || !bool(data["playing"]))) {
		server->stop_playback_stream(existing->playback);
	}
	if (!bool(data["playing"])) {
		playback.unref();
	}
	Vector<AudioFrame> volumes;
	volumes.resize(4);
	volumes.fill(AudioFrame(0, 0));
	Vector2 gain = calculate_audio_gain(id, data);
	volumes.write[0] = AudioFrame(gain.x, gain.y);
	if (playback.is_valid()) {
		if (restart) {
			server->start_playback_stream(playback, bus, volumes, data["position"], data["pitch_scale"]);
		} else {
			server->set_playback_bus_exclusive(playback, bus, volumes);
			server->set_playback_pitch_scale(playback, data["pitch_scale"]);
		}
		server->set_playback_paused(playback, data["paused"]);
	}
	audio_sources[uint32_t(id)] = { stream, playback, data.duplicate(true) };
	audio_sources[uint32_t(id)].effective_pitch = data["pitch_scale"];
	uint8_t marker = 1;
	store_ui_column(uint32_t(id), "audio", &marker);
	if (!slots[uint32_t(id)].active) { apply_activation(uint32_t(id)); }
	return true;
}
Dictionary ECSWorld::get_audio(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const AudioState *state = is_alive(id) ? audio_sources.getptr(uint32_t(id)) : nullptr;
	if (!state) {
		return Dictionary();
	}
	Dictionary data = state->definition.duplicate(true);
	if (state->playback.is_valid()) {
		data["position"] = AudioServer::get_singleton()->get_playback_position(state->playback);
	}
	return data;
}
bool ECSWorld::remove_audio(uint64_t id) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	AudioState *state = is_alive(id) ? audio_sources.getptr(uint32_t(id)) : nullptr;
	if (!state) {
		return false;
	}
	if (state->playback.is_valid() && AudioServer::get_singleton()) {
		AudioServer::get_singleton()->stop_playback_stream(state->playback);
	}
	audio_sources.erase(uint32_t(id));
	remove_row(pools["audio"], uint32_t(id));
	return true;
}
void ECSWorld::clear_audio() {
	if (auto *server = AudioServer::get_singleton()) {
		for (const auto &entry : audio_sources) {
			if (entry.value.playback.is_valid()) {
				server->stop_playback_stream(entry.value.playback);
			}
		}
	}
	audio_sources.clear();
	if (Pool *pool = pools.getptr("audio")) {
		pool->rows.clear();
		pool->entities.clear();
		pool->bytes.clear();
	}
}
bool ECSWorld::set_audio_listener(const Transform3D &transform) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, false);
	if (!transform.is_finite() || Math::is_zero_approx(transform.basis.determinant())) {
		return false;
	}
	audio_listener = transform.orthonormalized();
	return true;
}
Vector2 ECSWorld::calculate_audio_gain(uint64_t id, const Dictionary &definition) const {
	double gain = Math::db_to_linear(double(definition["volume_db"]));
	if (!bool(definition["spatial"])) {
		return Vector2(gain, gain);
	}
	Dictionary camera = get_active_camera();
	Transform3D listener = camera.is_empty() ? audio_listener : Transform3D(camera["transform"]);
	Vector3 offset = listener.basis.xform_inv(calculate_global_transform(id).origin - listener.origin);
	double distance = offset.length();
	double maximum = definition["max_distance"], unit = definition["unit_size"];
	if (maximum > 0 && distance >= maximum) {
		return Vector2();
	}
	gain *= Math::pow(MAX(1.0, distance / unit), -double(definition["attenuation"]));
	if (maximum > 0) {
		gain *= MAX(0.0, 1.0 - distance / maximum);
	}
	double pan = distance > 0.00001 ? CLAMP(double(offset.x) / distance, -1.0, 1.0) * double(definition["panning_strength"]) : 0.0;
	// Equal-power stereo panning. Center gives -3 dB per channel.
	return Vector2(gain * Math::sqrt((1.0 - pan) * 0.5), gain * Math::sqrt((1.0 + pan) * 0.5));
}
Vector2 ECSWorld::get_audio_gain(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Vector2());
	const AudioState *state = is_alive(id) ? audio_sources.getptr(uint32_t(id)) : nullptr;
	return state ? calculate_audio_gain(id, state->definition) : Vector2();
}
double ECSWorld::get_audio_pitch(uint64_t id) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	const AudioState *state = is_alive(id) ? audio_sources.getptr(uint32_t(id)) : nullptr;
	return state ? state->effective_pitch : 0;
}
void ECSWorld::step_audio(double delta) {
	Vector<uint64_t> finished;
	auto *server = AudioServer::get_singleton();
	Dictionary camera = get_active_camera();
	Transform3D listener = camera.is_empty() ? audio_listener : Transform3D(camera["transform"]);
	uint64_t listener_entity = camera.get("entity", uint64_t(0));
	for (auto &entry : audio_sources) {
		if (!slots[entry.key].active) { continue; }
		AudioState &state = entry.value;
		uint64_t source_id = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
		Vector3 source = calculate_global_transform(source_id).origin;
		double pitch = state.definition["pitch_scale"];
		bool tracking = bool(state.definition["spatial"]) && bool(state.definition["doppler"]) && !bool(state.definition["paused"]) && delta > 0;
		if (tracking && state.motion_valid && state.listener_entity == listener_entity) {
			Vector3 source_velocity = (source - state.previous_source) / delta;
			Vector3 listener_velocity = (listener.origin - state.previous_listener) / delta;
			double speed = state.definition["speed_of_sound"];
			Vector3 direction = source - listener.origin;
			// Supersonic motion/teleports invalidate this sample instead of causing a pitch spike.
			if (source_velocity.is_finite() && listener_velocity.is_finite() && source_velocity.length() < speed * 0.9 && listener_velocity.length() < speed * 0.9 && direction.length_squared() > 0.000001) {
				direction.normalize();
				pitch *= CLAMP((speed + listener_velocity.dot(direction)) / (speed + source_velocity.dot(direction)), 0.5, 2.0);
			}
		}
		state.effective_pitch = MIN(pitch, 16.0);
		state.previous_source = source;
		state.previous_listener = listener.origin;
		state.listener_entity = listener_entity;
		state.motion_valid = tracking;
		if (state.playback.is_valid()) {
			server->set_playback_pitch_scale(state.playback, state.effective_pitch);
		}
		if (state.playback.is_valid() && bool(state.definition["spatial"])) {
			uint64_t id = (uint64_t(slots[entry.key].generation) << 32) | entry.key;
			Vector2 gain = calculate_audio_gain(id, state.definition);
			Vector<AudioFrame> volumes;
			volumes.resize(4);
			volumes.fill(AudioFrame(0, 0));
			volumes.write[0] = AudioFrame(gain.x, gain.y);
			server->set_playback_bus_exclusive(state.playback, state.definition["bus"], volumes);
		}
		if (state.playback.is_valid() && !server->is_playback_active(state.playback) && !bool(state.definition["paused"])) {
			state.definition["playing"] = false;
			state.playback.unref();
			finished.push_back((uint64_t(slots[entry.key].generation) << 32) | entry.key);
		}
	}
	for (uint64_t id : finished) {
		if (is_alive(id)) {
			emit_signal("audio_finished", id);
		}
	}
}
