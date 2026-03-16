#include "ofApp.h"
#include "ofMain.h"

#ifdef TARGET_GLFW_WINDOW
#include "ofAppGLFWWindow.h"
#endif

#if defined(TARGET_LINUX) || defined(TARGET_RASPBERRY_PI)
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
#if defined(TARGET_LINUX) || defined(TARGET_RASPBERRY_PI)
constexpr const char* kStartupTracePath = "/tmp/rpi-bluetooth-receiver-ui.log";

void appendStartupTrace(const std::string& message) {
	const int fd = ::open(kStartupTracePath, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0) {
		return;
	}

	const std::string line = message + "\n";
	static_cast<void>(::write(fd, line.c_str(), line.size()));
	::close(fd);
}

void traceSignalAndExit(int signalNumber) {
	char buffer[128];
	const int written = std::snprintf(buffer, sizeof(buffer), "Fatal signal %d", signalNumber);
	if (written > 0) {
		const int fd = ::open(kStartupTracePath, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd >= 0) {
			static_cast<void>(::write(fd, buffer, static_cast<std::size_t>(written)));
			static_cast<void>(::write(fd, "\n", 1));
			::close(fd);
		}
	}

	std::_Exit(128 + signalNumber);
}

void installStartupTraceHandlers() {
	std::set_terminate([]() {
		appendStartupTrace("std::terminate called");

		const auto exception = std::current_exception();
		if (!exception) {
			std::_Exit(1);
		}

		try {
			std::rethrow_exception(exception);
		} catch (const std::exception& error) {
			appendStartupTrace(std::string("Unhandled exception: ") + error.what());
		} catch (...) {
			appendStartupTrace("Unhandled non-standard exception");
		}

		std::_Exit(1);
	});

	std::signal(SIGABRT, traceSignalAndExit);
	std::signal(SIGBUS, traceSignalAndExit);
	std::signal(SIGFPE, traceSignalAndExit);
	std::signal(SIGILL, traceSignalAndExit);
	std::signal(SIGSEGV, traceSignalAndExit);
	std::signal(SIGTERM, traceSignalAndExit);
}
#endif
} // namespace

//========================================================================
int main() {
	#if defined(TARGET_LINUX) || defined(TARGET_RASPBERRY_PI)
	installStartupTraceHandlers();
	appendStartupTrace("Starting UI");
	#endif

	ofSetLogLevel(OF_LOG_NOTICE);

	#ifdef TARGET_OPENGLES
		#ifdef TARGET_GLFW_WINDOW
	ofGLFWWindowSettings settings;
		#else
	ofGLESWindowSettings settings;
		#endif
	settings.setGLESVersion(2);
	#else
		#ifdef TARGET_GLFW_WINDOW
	ofGLFWWindowSettings settings;
		#else
	ofGLWindowSettings settings;
		#endif
	settings.setGLVersion(2, 1);
	#endif

	settings.setSize(640, 480);
	#if defined(TARGET_LINUX) || defined(TARGET_RASPBERRY_PI)
	settings.windowMode = OF_WINDOW;
	#ifdef TARGET_GLFW_WINDOW
	settings.decorated = false;
	settings.resizable = false;
	#endif
	appendStartupTrace("Creating initial windowed context");
	#else
	settings.windowMode = OF_WINDOW; // can also be OF_FULLSCREEN
	#endif

	#if defined(TARGET_LINUX) || defined(TARGET_RASPBERRY_PI)
	auto window = ofCreateWindow(settings);
	if (!window) {
		appendStartupTrace("ofCreateWindow returned null");
		return 1;
	}
	appendStartupTrace(window->getWindowContext() ? "Window context created" : "Window created without context");
	#else
	auto window = ofCreateWindow(settings);
	#endif

	#if defined(TARGET_LINUX) || defined(TARGET_RASPBERRY_PI)
	appendStartupTrace("Attaching application instance");
	#endif
	ofRunApp(window, std::make_shared<ofApp>());

	#if defined(TARGET_LINUX) || defined(TARGET_RASPBERRY_PI)
	appendStartupTrace("Entering main loop");
	#endif
	const int exitCode = ofRunMainLoop();
	#if defined(TARGET_LINUX) || defined(TARGET_RASPBERRY_PI)
	appendStartupTrace(std::string("Main loop exited with status ") + std::to_string(exitCode));
	#endif
	return exitCode;
}
