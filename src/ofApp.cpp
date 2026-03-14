#include "ofApp.h"

#include <filesystem>
#include <sstream>

namespace {
constexpr std::uint64_t kStatePollIntervalMs = 500;
constexpr float kArtworkPanelWidthRatio = 0.42f;

ofColor statusColorForState(const ReceiverState& state) {
    if (!state.errorMessage.empty()) {
        return ofColor(255, 99, 71);
    }

    if (state.isConnected()) {
        return ofColor(78, 201, 120);
    }

    return ofColor(255, 215, 0);
}

std::string chooseDisplayName(const ReceiverState& state) {
    if (!state.activeUser.empty()) {
        return state.activeUser;
    }

    if (!state.sessionName.empty()) {
        return state.sessionName;
    }

    if (!state.deviceName.empty()) {
        return state.deviceName;
    }

    return "No active device";
}

std::string chooseTrackTitle(const ReceiverState& state) {
    if (!state.title.empty()) {
        return state.title;
    }

    if (state.isConnected()) {
        return "Waiting for track metadata";
    }

    return "No track playing";
}
} // namespace

void ofApp::setup(){
    ofSetWindowTitle("Receiver Display");
    ofSetFrameRate(30);
    ofBackground(10, 14, 20);
    ofSetBackgroundAuto(true);
    state = ReceiverState::disconnected();
    stateFilePath = ofToDataPath("receiver_state.json", true);
    dataDirectory = ofFilePath::getEnclosingDirectory(stateFilePath, true);
    loadStateIfChanged(true);
}

void ofApp::update(){
    const auto now = ofGetElapsedTimeMillis();
    if (now - lastPollMillis >= kStatePollIntervalMs) {
        loadStateIfChanged(false);
        lastPollMillis = now;
    }
}

void ofApp::draw(){
    ofBackgroundGradient(ofColor(12, 16, 24), ofColor(34, 40, 52), OF_GRADIENT_CIRCULAR);

    const float margin = 28.0f;
    const float gutter = 24.0f;
    const float statusHeight = 70.0f;
    const float contentHeight = ofGetHeight() - margin * 2.0f - statusHeight - gutter;
    const float artworkWidth = std::min((ofGetWidth() - margin * 2.0f - gutter) * kArtworkPanelWidthRatio, contentHeight);

    const ofRectangle statusBounds(margin, margin, ofGetWidth() - margin * 2.0f, statusHeight);
    const ofRectangle artworkBounds(margin, statusBounds.getBottom() + gutter, artworkWidth, contentHeight);
    const ofRectangle metadataBounds(artworkBounds.getRight() + gutter,
                                     statusBounds.getBottom() + gutter,
                                     ofGetWidth() - artworkBounds.getWidth() - margin * 2.0f - gutter,
                                     contentHeight);

    drawStatusBar(statusBounds);
    drawArtworkPanel(artworkBounds);
    drawMetadataPanel(metadataBounds);
}

void ofApp::keyPressed(int key){
    if (key == 'r' || key == 'R') {
        loadStateIfChanged(true);
    }

    if (key == 'f' || key == 'F') {
        ofToggleFullscreen();
    }
}

void ofApp::windowResized(int w, int h){
    loadStateIfChanged(true);
}

void ofApp::loadStateIfChanged(bool forceReload) {
    namespace fs = std::filesystem;

    if (!fs::exists(stateFilePath)) {
        state = ReceiverState::disconnected();
        artworkImage.clear();
        loadedArtworkPath.clear();
        ofLogWarning() << "Missing state file at " << stateFilePath;
        return;
    }

    const auto modifiedAt = fs::last_write_time(stateFilePath);
    if (!forceReload && hasStateTimestamp && modifiedAt == lastStateTimestamp) {
        return;
    }

    try {
        ofJson jsonState = ofLoadJson(stateFilePath);
        state = ReceiverState::fromJson(jsonState, dataDirectory);
        lastStateTimestamp = modifiedAt;
        hasStateTimestamp = true;
        loadArtworkIfNeeded();
    } catch (const std::exception& error) {
        state = ReceiverState::disconnected();
        state.errorMessage = "Invalid receiver_state.json";
        artworkImage.clear();
        loadedArtworkPath.clear();
        ofLogError() << "Unable to parse state file: " << error.what();
    }
}

void ofApp::loadArtworkIfNeeded() {
    if (!state.hasArtwork()) {
        artworkImage.clear();
        loadedArtworkPath.clear();
        return;
    }

    if (state.artworkPath == loadedArtworkPath) {
        return;
    }

    ofFile artworkFile(state.artworkPath);
    if (!artworkFile.exists()) {
        artworkImage.clear();
        loadedArtworkPath.clear();
        ofLogWarning() << "Artwork file missing: " << state.artworkPath;
        return;
    }

    if (!artworkImage.load(state.artworkPath)) {
        artworkImage.clear();
        loadedArtworkPath.clear();
        ofLogError() << "Unable to load artwork: " << state.artworkPath;
        return;
    }

    loadedArtworkPath = state.artworkPath;
}

void ofApp::drawArtworkPanel(const ofRectangle& bounds) {
    ofPushStyle();
    ofSetColor(24, 29, 38, 220);
    ofDrawRectRounded(bounds, 18.0f);

    if (artworkImage.isAllocated()) {
        const float imageSize = std::min(bounds.getWidth() - 24.0f, bounds.getHeight() - 24.0f);
        const float x = bounds.getCenter().x - imageSize * 0.5f;
        const float y = bounds.getCenter().y - imageSize * 0.5f;
        artworkImage.draw(x, y, imageSize, imageSize);
    } else {
        ofSetColor(46, 54, 68);
        ofDrawRectRounded(bounds.x + 24.0f, bounds.y + 24.0f, bounds.getWidth() - 48.0f, bounds.getHeight() - 48.0f, 16.0f);
        ofSetColor(204, 214, 246);
        ofNoFill();
        ofSetLineWidth(5.0f);
        const ofPoint centre = bounds.getCenter();
        const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.16f;
        ofDrawCircle(centre, radius);
        ofDrawLine(centre.x + radius * 0.75f, centre.y - radius * 0.75f, centre.x + radius * 1.45f, centre.y - radius * 1.45f);
        ofFill();
        drawScaledBitmapText("Artwork\npending", bounds.x + 40.0f, bounds.getBottom() - 70.0f, 2.2f, ofColor(24, 29, 38, 0), ofColor(204, 214, 246));
    }
    ofPopStyle();
}

void ofApp::drawMetadataPanel(const ofRectangle& bounds) {
    ofPushStyle();
    ofSetColor(24, 29, 38, 220);
    ofDrawRectRounded(bounds, 18.0f);

    const float left = bounds.x + 28.0f;
    float cursorY = bounds.y + 42.0f;

    drawScaledBitmapText(chooseDisplayName(state), left, cursorY, 3.0f, ofColor(24, 29, 38, 0), ofColor(255));
    cursorY += 76.0f;

    drawScaledBitmapText(wrapText(chooseTrackTitle(state), 26), left, cursorY, 3.8f, ofColor(24, 29, 38, 0), ofColor(240, 240, 240));
    cursorY += 120.0f;

    drawScaledBitmapText(wrapText(state.artist.empty() ? "Artist unavailable" : state.artist, 28), left, cursorY, 2.5f, ofColor(24, 29, 38, 0), ofColor(170, 212, 255));
    cursorY += 64.0f;
    drawScaledBitmapText(wrapText(state.album.empty() ? "Album unavailable" : state.album, 30), left, cursorY, 2.1f, ofColor(24, 29, 38, 0), ofColor(197, 204, 212));
    cursorY += 74.0f;

    std::ostringstream details;
    details << "Source: " << state.sourceLabel() << "\n";
    details << "Status: " << state.playbackStatus << "\n";
    details << "Volume: " << state.volumeLabel() << "\n";
    details << "Device: " << (state.deviceName.empty() ? "Unknown" : state.deviceName) << "\n";
    details << "Updated: " << (state.lastUpdated.empty() ? "n/a" : state.lastUpdated);
    drawScaledBitmapText(details.str(), left, cursorY, 1.8f, ofColor(24, 29, 38, 0), ofColor(222, 227, 234));

    if (!state.errorMessage.empty()) {
        drawScaledBitmapText(wrapText("Bridge: " + state.errorMessage, 40), left, bounds.getBottom() - 90.0f, 1.8f, ofColor(24, 29, 38, 0), ofColor(255, 153, 153));
    }

    drawScaledBitmapText("R reloads state   F toggles fullscreen", left, bounds.getBottom() - 34.0f, 1.3f, ofColor(24, 29, 38, 0), ofColor(143, 153, 166));
    ofPopStyle();
}

void ofApp::drawStatusBar(const ofRectangle& bounds) {
    ofPushStyle();
    ofSetColor(24, 29, 38, 210);
    ofDrawRectRounded(bounds, 18.0f);

    const ofColor indicatorColor = statusColorForState(state);
    ofSetColor(indicatorColor);
    ofDrawCircle(bounds.x + 28.0f, bounds.getCenter().y, 10.0f);

    const std::string headline = state.sourceLabel() + " · " + state.connectionLabel();
    drawScaledBitmapText(headline, bounds.x + 52.0f, bounds.y + 28.0f, 2.2f, ofColor(24, 29, 38, 0), ofColor(255));

    std::string subline = state.errorMessage.empty() ? "Bridge healthy" : state.errorMessage;
    if (!state.isConnected() && state.errorMessage.empty()) {
        subline = "Waiting for a Bluetooth or AirPlay session";
    }
    drawScaledBitmapText(wrapText(subline, 56), bounds.x + 52.0f, bounds.y + 54.0f, 1.4f, ofColor(24, 29, 38, 0), ofColor(188, 196, 208));
    ofPopStyle();
}

void ofApp::drawScaledBitmapText(const std::string& text, float x, float y, float scale, const ofColor& background, const ofColor& foreground) {
    ofPushMatrix();
    ofTranslate(x, y);
    ofScale(scale, scale);
    ofDrawBitmapStringHighlight(text, 0.0f, 0.0f, background, foreground);
    ofPopMatrix();
}

std::string ofApp::wrapText(const std::string& text, std::size_t maxCharsPerLine) const {
    if (text.size() <= maxCharsPerLine || maxCharsPerLine == 0) {
        return text;
    }

    std::istringstream stream(text);
    std::ostringstream wrapped;
    std::string word;
    std::size_t lineLength = 0;

    while (stream >> word) {
        const std::size_t nextLength = lineLength == 0 ? word.size() : lineLength + 1 + word.size();
        if (lineLength != 0 && nextLength > maxCharsPerLine) {
            wrapped << '\n';
            lineLength = 0;
        } else if (lineLength != 0) {
            wrapped << ' ';
            lineLength += 1;
        }

        wrapped << word;
        lineLength += word.size();
    }

    return wrapped.str();
}
