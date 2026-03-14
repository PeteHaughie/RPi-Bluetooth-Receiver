#pragma once

#include <filesystem>

#include "ofMain.h"
#include "ReceiverState.h"

class ofApp : public ofBaseApp{

	public:
		void setup() override;
		void update() override;
		void draw() override;
		void keyPressed(int key) override;
		void windowResized(int w, int h) override;

	private:
		void loadStateIfChanged(bool forceReload);
		void loadArtworkIfNeeded();
		void drawArtworkPanel(const ofRectangle& bounds);
		void drawMetadataPanel(const ofRectangle& bounds);
		void drawStatusBar(const ofRectangle& bounds);
		void drawScaledBitmapText(const std::string& text, float x, float y, float scale, const ofColor& background, const ofColor& foreground);
		std::string wrapText(const std::string& text, std::size_t maxCharsPerLine) const;

		ReceiverState state;
		ofImage artworkImage;
		std::string stateFilePath;
		std::string dataDirectory;
		std::string loadedArtworkPath;
		std::uint64_t lastPollMillis = 0;
		bool hasStateTimestamp = false;
		std::filesystem::file_time_type lastStateTimestamp;
};
