cmake_minimum_required(VERSION 3.20)
include(FetchContent)

project(vulkan-starter-app LANGUAGES C CXX)

add_executable(${PROJECT_NAME} source/main.cpp source/graphics_internal.cpp source/application.cpp)

if(LINUX)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(APPLE)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(WIN32)
	if(MSVC)
		target_compile_options(${PROJECT_NAME} PRIVATE /W4 /wd4201)
		target_link_options(${PROJECT_NAME} PRIVATE /subsystem:windows /entry:mainCRTStartup)
	else()
		target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
		target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
	endif()
	target_compile_definitions(${PROJECT_NAME} PRIVATE -D_USE_MATH_DEFINES)
else()
	message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is not supported")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED TRUE CXX_STANDARD 20)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.5.1
)

FetchContent_Declare(
	vk-bootstrap
	GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
	GIT_TAG v1.4.321
)

FetchContent_Declare(
	VulkanMemoryAllocator
	GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
	GIT_TAG v3.4.0
)

FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.92.9
)

find_package(Vulkan REQUIRED)

set(GLFW_LIBRARY_TYPE STATIC)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)

FetchContent_MakeAvailable(
	glfw
	vk-bootstrap
	VulkanMemoryAllocator
	imgui
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	glfw
	Vulkan::Vulkan
	vk-bootstrap::vk-bootstrap
	GPUOpen::VulkanMemoryAllocator
)

target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_sources(${PROJECT_NAME} PRIVATE
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
)

# Compile shaders into the build tree.  Keeping generated SPIR-V out of the
# source tree makes clean builds reproducible and lets the executable use one
# absolute, CMake-provided resource directory.
find_program(GLSLC glslc REQUIRED)
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(SHADER_BINARIES)

macro(compile_shader SHADER_FILE)
	set(SHADER_SOURCE "${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE}")
	set(SHADER_BINARY_PATH "${SHADER_OUTPUT_DIR}/${SHADER_FILE}.spv")

	add_custom_command(
		OUTPUT "${SHADER_BINARY_PATH}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
		COMMAND "${GLSLC}" "${SHADER_SOURCE}" -o "${SHADER_BINARY_PATH}"
		DEPENDS "${SHADER_SOURCE}"
		COMMENT "Compiling ${SHADER_FILE} shader"
		VERBATIM
	)

	list(APPEND SHADER_BINARIES "${SHADER_BINARY_PATH}")
endmacro()

compile_shader(lab.vert)
compile_shader(lab.frag)

add_custom_target(shaders DEPENDS ${SHADER_BINARIES})
add_dependencies(${PROJECT_NAME} shaders)
target_compile_definitions(${PROJECT_NAME} PRIVATE LAB_SHADER_DIR="${SHADER_OUTPUT_DIR}")

include(CTest)
if(BUILD_TESTING)
	add_executable(scene_tests tests/scene_tests.cpp)
	target_compile_features(scene_tests PRIVATE cxx_std_20)
	target_include_directories(scene_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
	add_test(NAME scene_tests COMMAND scene_tests)
endif()