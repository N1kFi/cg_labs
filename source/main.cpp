#include <cstdint>
#include <climits>
#include <algorithm>
#include <cstdlib>

#include <iostream>
#include <string>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

#include "graphics_internal.hpp"
#include "application.hpp"

namespace {

constexpr int32_t default_window_width = 1400;
constexpr int32_t default_window_height = 900;

constexpr char default_window_title[] = "CG Lab 2 | Camera and Lighting | Variant 11";

GLFWwindow* glfw_window;

bool parseFrameLimit(int argc, char** argv, int& frames) {
	for (int i = 1; i < argc; ++i) {
		if (std::string(argv[i]) == "--frames") {
			if (i + 1 >= argc) return false;
			char* end = nullptr;
			const long value = std::strtol(argv[++i], &end, 10);
			if (*argv[i] == '\0' || *end != '\0' || value < 1 || value > INT_MAX) return false;
			frames = static_cast<int>(value);
		}
	}
	return true;
}

} // namespace

int main(int argc, char** argv) {
	int status = EXIT_SUCCESS;
	int frameLimit = 0;
	bool selfTest = false;
	std::string screenshotPath;
	if (!parseFrameLimit(argc, argv, frameLimit)) {
		std::cerr << "Invalid --frames value\n";
		return EXIT_FAILURE;
	}
	for (int i = 1; i < argc; ++i) {
		const std::string argument = argv[i];
		if (argument == "--frames") { ++i; continue; }
		if (argument == "--self-test") { selfTest = true; if (!frameLimit) frameLimit = 70; continue; }
		if (argument == "--screenshot" && i + 1 < argc) {
			screenshotPath = argv[++i];
			if (!frameLimit) frameLimit = 5;
			continue;
		}
		if (argument == "--demo" && i + 1 < argc) {
			const std::string name = argv[++i];
			if (name != "all" && name != "directional" && name != "points" && name != "spots" && name != "empty") return EXIT_FAILURE;
			application::setDemo(argv[i]);
			continue;
		}
		std::cerr << "Usage: vulkan-starter-app [--frames N] [--demo all|directional|points|spots|empty] [--self-test] [--screenshot frame.ppm]\n";
		return EXIT_FAILURE;
	}
	if (!screenshotPath.empty()) frameLimit = std::max(frameLimit,3);
	if (selfTest) frameLimit = std::max(frameLimit,70);
	int frameCount = 0;

	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW\n";
		return EXIT_FAILURE;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	glfw_window = glfwCreateWindow(default_window_width, default_window_height,
	                               default_window_title, nullptr, nullptr);
	if (glfw_window == nullptr) {
		status = EXIT_FAILURE;
		goto err_null_window;
	}

	glfwSetFramebufferSizeCallback(glfw_window, [](GLFWwindow*, int width, int height){
		if (width == 0 || height == 0) {
			return;
		}

		graphics::internal::resize(width, height);
	});

	if (ImGui::CreateContext() == nullptr) {
		std::cerr << "Failed to create ImGUI context\n";
		status = EXIT_FAILURE;
		goto err_imgui_init;
	}

	if (!ImGui_ImplGlfw_InitForVulkan(glfw_window, true)) {
		std::cerr << "Failed to initialize ImGUI GLFW backend for Vulkan renderer\n";
		status = EXIT_FAILURE;
		goto err_imgui_glfw_init;
	}

	if (!graphics::internal::initialize(glfw_window)) {
		std::cerr << "Failed to initialize graphics\n";
		status = EXIT_FAILURE;
		goto err_graphics_init;
	}

	if (!application::initialize()) {
		std::cerr << "Failed to initialize application\n";
		status = EXIT_FAILURE;
		goto err_application_init;
	}

	while (!glfwWindowShouldClose(glfw_window)) {
		const double time = glfwGetTime();

		glfwPollEvents();
		ImGui_ImplGlfw_NewFrame();

		ImGui::NewFrame();
		if (selfTest) application::verificationStep(frameCount);
		application::update(time);
		ImGui::Render();

		graphics::internal::FrameData fd = graphics::internal::prepare();
		application::render(fd);
		if (!application::healthy()) { status = EXIT_FAILURE; break; }
		if (!screenshotPath.empty() && frameCount == 2) graphics::internal::requestScreenshot(screenshotPath.c_str());
		graphics::internal::submitAndPresent();
		if (graphics::internal::hasFatalError()) { status = EXIT_FAILURE; break; }
		++frameCount;
		if (frameLimit && frameCount >= frameLimit) break;
	}

	application::shutdown();
err_application_init:
	graphics::internal::shutdown();
err_graphics_init:
	ImGui_ImplGlfw_Shutdown();
err_imgui_glfw_init:
	ImGui::DestroyContext();
err_imgui_init:
	glfwDestroyWindow(glfw_window);
err_null_window:
	glfwTerminate();

	if (graphics::internal::validationErrorCount()) status = EXIT_FAILURE;
	return status;
}