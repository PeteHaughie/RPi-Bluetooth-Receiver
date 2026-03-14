#pragma once

#include "ofMain.h"

struct ReceiverState {
    std::string sourceType = "idle";
    std::string connectionState = "disconnected";
    std::string playbackStatus = "stopped";
    std::string activeUser;
    std::string deviceName;
    std::string sessionName;
    std::string title;
    std::string artist;
    std::string album;
    std::string lastUpdated;
    std::string errorMessage;
    std::string artworkPath;
    std::string artworkUrl;
    float volumePercent = -1.0f;
    std::int64_t positionMs = 0;
    std::int64_t durationMs = 0;

    static ReceiverState disconnected();
    static ReceiverState fromJson(const ofJson& jsonState, const std::string& dataDirectory);

    std::string sourceLabel() const;
    std::string connectionLabel() const;
    std::string volumeLabel() const;
    bool isConnected() const;
    bool hasArtwork() const;
};
