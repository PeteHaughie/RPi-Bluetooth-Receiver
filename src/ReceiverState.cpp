#include "ReceiverState.h"

namespace {
std::string getStringOrDefault(const ofJson& jsonState, const std::string& key, const std::string& fallback) {
    if (!jsonState.contains(key) || jsonState.at(key).is_null()) {
        return fallback;
    }

    if (jsonState.at(key).is_string()) {
        return jsonState.at(key).get<std::string>();
    }

    return jsonState.at(key).dump();
}

float getFloatOrDefault(const ofJson& jsonState, const std::string& key, float fallback) {
    if (!jsonState.contains(key) || jsonState.at(key).is_null()) {
        return fallback;
    }

    if (jsonState.at(key).is_number()) {
        return jsonState.at(key).get<float>();
    }

    if (jsonState.at(key).is_string()) {
        try {
            return std::stof(jsonState.at(key).get<std::string>());
        } catch (const std::exception&) {
            return fallback;
        }
    }

    return fallback;
}

std::int64_t getInt64OrDefault(const ofJson& jsonState, const std::string& key, std::int64_t fallback) {
    if (!jsonState.contains(key) || jsonState.at(key).is_null()) {
        return fallback;
    }

    if (jsonState.at(key).is_number_integer()) {
        return jsonState.at(key).get<std::int64_t>();
    }

    if (jsonState.at(key).is_string()) {
        try {
            return std::stoll(jsonState.at(key).get<std::string>());
        } catch (const std::exception&) {
            return fallback;
        }
    }

    return fallback;
}

std::string resolveArtworkPath(const std::string& artworkPath, const std::string& dataDirectory) {
    if (artworkPath.empty()) {
        return "";
    }

    if (ofFilePath::isAbsolute(artworkPath)) {
        return artworkPath;
    }

    return ofFilePath::join(dataDirectory, artworkPath);
}
} // namespace

ReceiverState ReceiverState::disconnected() {
    ReceiverState state;
    state.errorMessage = "Waiting for metadata from receiver_state_bridge.py";
    return state;
}

ReceiverState ReceiverState::fromJson(const ofJson& jsonState, const std::string& dataDirectory) {
    ReceiverState state = ReceiverState::disconnected();
    state.sourceType = getStringOrDefault(jsonState, "sourceType", state.sourceType);
    state.connectionState = getStringOrDefault(jsonState, "connectionState", state.connectionState);
    state.playbackStatus = getStringOrDefault(jsonState, "playbackStatus", state.playbackStatus);
    state.activeUser = getStringOrDefault(jsonState, "activeUser", "");
    state.deviceName = getStringOrDefault(jsonState, "deviceName", "");
    state.sessionName = getStringOrDefault(jsonState, "sessionName", "");
    state.title = getStringOrDefault(jsonState, "title", "");
    state.artist = getStringOrDefault(jsonState, "artist", "");
    state.album = getStringOrDefault(jsonState, "album", "");
    state.lastUpdated = getStringOrDefault(jsonState, "lastUpdated", "");
    state.errorMessage = getStringOrDefault(jsonState, "error", "");
    state.artworkUrl = getStringOrDefault(jsonState, "artworkUrl", "");
    state.artworkPath = resolveArtworkPath(getStringOrDefault(jsonState, "artworkPath", ""), dataDirectory);
    state.volumePercent = getFloatOrDefault(jsonState, "volumePercent", -1.0f);
    state.positionMs = getInt64OrDefault(jsonState, "positionMs", 0);
    state.durationMs = getInt64OrDefault(jsonState, "durationMs", 0);
    return state;
}

std::string ReceiverState::sourceLabel() const {
    if (sourceType == "bluetooth") {
        return "Bluetooth";
    }

    if (sourceType == "airplay") {
        return "AirPlay";
    }

    return "Idle";
}

std::string ReceiverState::connectionLabel() const {
    if (connectionState.empty()) {
        return "Unknown";
    }

    std::string label = connectionState;
    label[0] = static_cast<char>(std::toupper(label[0]));
    return label;
}

std::string ReceiverState::volumeLabel() const {
    if (volumePercent < 0.0f) {
        return "Unknown";
    }

    return ofToString(std::round(volumePercent)) + "%";
}

bool ReceiverState::isConnected() const {
    return connectionState == "connected" || connectionState == "playing";
}

bool ReceiverState::hasArtwork() const {
    return !artworkPath.empty();
}
