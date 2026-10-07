#include "application.hpp"
#include "scene_math.hpp"

#include <imgui.h>
#include <vk_mem_alloc.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace application {
namespace {

using lab::math::Mat4;
using lab::math::Vec3;
using lab::math::Vec4;
using lab::scene::AnimationClock;
using lab::scene::Mesh;
using lab::scene::TrajectoryMode;

struct GlobalUniforms {
	Mat4 viewProjection;
};

struct ObjectUniforms {
	Mat4 model;
	Vec4 tint;
};

struct Buffer {
	VkBuffer handle = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
};

struct ObjectState {
	Vec3 basePosition;
	Vec3 translation;
	Vec3 rotation;
	Vec3 scale{1.0f, 1.0f, 1.0f};
	Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
	TrajectoryMode trajectory = TrajectoryMode::Lissajous;
	float radius = 2.35f;
	float amplitude = 0.45f;
	float frequency = 0.75f;
	float phase = 0.0f;
};

VkDevice device() { return graphics::internal::context.device; }
VmaAllocator allocator() { return graphics::internal::context.allocator; }

VkShaderModule vertexShader = VK_NULL_HANDLE;
VkShaderModule fragmentShader = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE;
VkDescriptorSetLayout globalSetLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout objectSetLayout = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet globalSet = VK_NULL_HANDLE;
std::array<VkDescriptorSet, 2> objectSets{};
Buffer vertexBuffer;
Buffer indexBuffer;
Buffer globalBuffer;
std::array<Buffer, 2> objectBuffers{};
std::uint32_t indexCount = 0;

Mesh mesh;
AnimationClock clock;
std::array<ObjectState, 2> objects{{
	{{-1.6f, 0.0f, 0.0f}, {}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.72f, 0.48f, 1.0f},
	 TrajectoryMode::Lissajous, 0.72f, 0.38f, 0.85f, 0.0f},
	{{1.6f, 0.0f, -0.15f}, {}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.40f, 0.78f, 1.0f, 1.0f},
	 TrajectoryMode::Epicyclic, 0.72f, 0.38f, 0.85f, 2.1f},
}};
int selectedObject = 0;
int projectionMode = 0;
double lastUpdateWallTime = 0.0;

void destroyBuffer(Buffer& buffer) {
	if (buffer.handle != VK_NULL_HANDLE) {
		vmaDestroyBuffer(allocator(), buffer.handle, buffer.allocation);
	}
	buffer = {};
}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage,
                  Buffer& result, bool map) {
	const VkBufferCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	const VmaAllocationCreateInfo allocationInfo{
		.flags = map ? (VMA_ALLOCATION_CREATE_MAPPED_BIT |
		                 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) : 0u,
		.usage = memoryUsage,
	};
	VmaAllocationInfo info{};
	if (vmaCreateBuffer(allocator(), &createInfo, &allocationInfo, &result.handle,
	                    &result.allocation, &info) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to create Vulkan buffer\n";
		result = {};
		return false;
	}
	result.mapped = info.pMappedData;
	if (map && result.mapped == nullptr && vmaMapMemory(allocator(), result.allocation, &result.mapped) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to map Vulkan buffer\n";
		destroyBuffer(result);
		return false;
	}
	return true;
}

std::vector<std::uint32_t> readSpirv(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		return {};
	}
	const std::streamsize size = file.tellg();
	if (size <= 0 || (size % static_cast<std::streamsize>(sizeof(std::uint32_t))) != 0) {
		return {};
	}
	file.seekg(0, std::ios::beg);
	std::vector<std::uint32_t> bytes(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
	if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
		return {};
	}
	return bytes;
}

VkShaderModule loadShader(const char* filename) {
#ifdef LAB_SHADER_DIR
	const std::string path = std::string(LAB_SHADER_DIR) + "/" + filename;
#else
	const std::string path = std::string("shaders/") + filename;
#endif
	const std::vector<std::uint32_t> code = readSpirv(path.c_str());
	if (code.empty()) {
		std::cerr << "Lab1: unable to read shader " << path << "\n";
		return VK_NULL_HANDLE;
	}
	const VkShaderModuleCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size() * sizeof(std::uint32_t),
		.pCode = code.data(),
	};
	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(device(), &createInfo, nullptr, &module) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to create shader module " << path << "\n";
	}
	return module;
}

void destroyShader(VkShaderModule& shader) {
	if (shader != VK_NULL_HANDLE) {
		vkDestroyShaderModule(device(), shader, nullptr);
		shader = VK_NULL_HANDLE;
	}
}

bool createDescriptors() {
	const VkDescriptorSetLayoutBinding globalBinding{
		.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .pImmutableSamplers = nullptr,
	};
	const VkDescriptorSetLayoutCreateInfo globalLayoutInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1, .pBindings = &globalBinding,
	};
	if (vkCreateDescriptorSetLayout(device(), &globalLayoutInfo, nullptr, &globalSetLayout) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to create global descriptor set layout\n";
		return false;
	}
	const VkDescriptorSetLayoutBinding objectBinding{
		.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};
	const VkDescriptorSetLayoutCreateInfo objectLayoutInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1, .pBindings = &objectBinding,
	};
	if (vkCreateDescriptorSetLayout(device(), &objectLayoutInfo, nullptr, &objectSetLayout) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to create object descriptor set layout\n";
		return false;
	}

	const VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3}};
	const VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 3, .poolSizeCount = 1, .pPoolSizes = poolSizes,
	};
	if (vkCreateDescriptorPool(device(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to create application descriptor pool\n";
		return false;
	}
	const VkDescriptorSetLayout globalLayouts[] = {globalSetLayout};
	const VkDescriptorSetAllocateInfo globalAllocate{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptorPool, .descriptorSetCount = 1, .pSetLayouts = globalLayouts,
	};
	if (vkAllocateDescriptorSets(device(), &globalAllocate, &globalSet) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to allocate global descriptor set\n";
		return false;
	}
	const VkDescriptorSetLayout objectLayouts[] = {objectSetLayout, objectSetLayout};
	const VkDescriptorSetAllocateInfo objectAllocate{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptorPool, .descriptorSetCount = 2, .pSetLayouts = objectLayouts,
	};
	if (vkAllocateDescriptorSets(device(), &objectAllocate, objectSets.data()) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to allocate object descriptor sets\n";
		return false;
	}

	const VkDescriptorSetLayout layouts[] = {globalSetLayout, objectSetLayout};
	const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 2, .pSetLayouts = layouts,
	};
	if (vkCreatePipelineLayout(device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to create pipeline layout\n";
		return false;
	}
	return true;
}

void writeDescriptorSets() {
	const VkDescriptorBufferInfo globalInfo{globalBuffer.handle, 0, sizeof(GlobalUniforms)};
	const VkWriteDescriptorSet globalWrite{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = globalSet, .dstBinding = 0,
		.descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &globalInfo,
	};
	vkUpdateDescriptorSets(device(), 1, &globalWrite, 0, nullptr);
	std::array<VkWriteDescriptorSet, 2> writes{};
	std::array<VkDescriptorBufferInfo, 2> infos{};
	for (std::size_t i = 0; i < objectSets.size(); ++i) {
		infos[i] = {objectBuffers[i].handle, 0, sizeof(ObjectUniforms)};
		writes[i] = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = objectSets[i], .dstBinding = 0,
			.descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &infos[i],
		};
	}
	vkUpdateDescriptorSets(device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

bool createPipeline() {
	vertexShader = loadShader("lab.vert.spv");
	fragmentShader = loadShader("lab.frag.spv");
	if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE || !createDescriptors()) {
		return false;
	}
	const VkPipelineShaderStageCreateInfo stages[] = {
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
		 .module = vertexShader, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		 .module = fragmentShader, .pName = "main"},
	};
	const VkVertexInputBindingDescription binding{0, sizeof(lab::scene::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
	const VkVertexInputAttributeDescription attributes[] = {
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(lab::scene::Vertex, position)},
		{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(lab::scene::Vertex, localColor)},
	};
	const VkPipelineVertexInputStateCreateInfo vertexInput{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributes,
	};
	const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	const VkPipelineViewportStateCreateInfo viewportState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .scissorCount = 1,
	};
	const VkPipelineRasterizationStateCreateInfo rasterization{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE, .lineWidth = 1.0f,
	};
	const VkPipelineMultisampleStateCreateInfo multisample{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	const VkPipelineDepthStencilStateCreateInfo depthStencil{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS,
	};
	const VkPipelineColorBlendAttachmentState blendAttachment{
		.blendEnable = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	const VkPipelineColorBlendStateCreateInfo blend{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1, .pAttachments = &blendAttachment,
	};
	const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	const VkPipelineDynamicStateCreateInfo dynamicState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2, .pDynamicStates = dynamicStates,
	};
	const VkGraphicsPipelineCreateInfo pipelineInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2, .pStages = stages, .pVertexInputState = &vertexInput,
		.pInputAssemblyState = &inputAssembly, .pViewportState = &viewportState,
		.pRasterizationState = &rasterization, .pMultisampleState = &multisample,
		.pDepthStencilState = &depthStencil, .pColorBlendState = &blend,
		.pDynamicState = &dynamicState, .layout = pipelineLayout, .renderPass = graphics::internal::context.render_pass,
	};
	if (vkCreateGraphicsPipelines(device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to create graphics pipeline\n";
		return false;
	}
	return true;
}

bool createGeometryAndUniforms() {
	mesh = lab::scene::makeIcosahedron();
	indexCount = static_cast<std::uint32_t>(mesh.indices.size());
	if (!createBuffer(sizeof(mesh.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                  VMA_MEMORY_USAGE_AUTO_PREFER_HOST, vertexBuffer, true) ||
	    !createBuffer(sizeof(mesh.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	                  VMA_MEMORY_USAGE_AUTO_PREFER_HOST, indexBuffer, true) ||
	    !createBuffer(sizeof(GlobalUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                  VMA_MEMORY_USAGE_AUTO_PREFER_HOST, globalBuffer, true)) {
		return false;
	}
	for (Buffer& buffer : objectBuffers) {
		if (!createBuffer(sizeof(ObjectUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		                  VMA_MEMORY_USAGE_AUTO_PREFER_HOST, buffer, true)) {
			return false;
		}
	}
	std::memcpy(vertexBuffer.mapped, mesh.vertices.data(), sizeof(mesh.vertices));
	std::memcpy(indexBuffer.mapped, mesh.indices.data(), sizeof(mesh.indices));
	vmaFlushAllocation(allocator(), vertexBuffer.allocation, 0, VK_WHOLE_SIZE);
	vmaFlushAllocation(allocator(), indexBuffer.allocation, 0, VK_WHOLE_SIZE);
	writeDescriptorSets();
	return true;
}

void destroyResources() {
	if (device() == VK_NULL_HANDLE) {
		return;
	}
	if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device(), pipeline, nullptr);
	pipeline = VK_NULL_HANDLE;
	if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device(), pipelineLayout, nullptr);
	pipelineLayout = VK_NULL_HANDLE;
	if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device(), descriptorPool, nullptr);
	descriptorPool = VK_NULL_HANDLE;
	if (objectSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device(), objectSetLayout, nullptr);
	objectSetLayout = VK_NULL_HANDLE;
	if (globalSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device(), globalSetLayout, nullptr);
	globalSetLayout = VK_NULL_HANDLE;
	for (Buffer& buffer : objectBuffers) destroyBuffer(buffer);
	destroyBuffer(globalBuffer);
	destroyBuffer(indexBuffer);
	destroyBuffer(vertexBuffer);
	destroyShader(fragmentShader);
	destroyShader(vertexShader);
}

void drawControls() {
	ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_Once);
	ImGui::Begin("Lab 1 | Regular Icosahedron", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextUnformatted("Variant 11  |  all six bonuses");
	ImGui::Separator();
	ImGui::TextUnformatted("Projection");
	ImGui::RadioButton("Perspective", &projectionMode, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Orthographic", &projectionMode, 1);
	ImGui::Separator();
	if (ImGui::Button(clock.playing ? "Pause animation" : "Play animation")) clock.playing = !clock.playing;
	ImGui::SameLine();
	if (ImGui::Button("Reset")) {
		clock.reset(lastUpdateWallTime);
		for (ObjectState& object : objects) {
			object.translation = {};
			object.rotation = {};
		}
	}
	ImGui::SliderFloat("Speed", &clock.speed, 0.0f, 3.0f, "%.2f");
	ImGui::Text("Simulation time %.2f s", clock.time());
	ImGui::Separator();
	ImGui::TextUnformatted("Independent descriptor sets");
	ImGui::TextWrapped("Each icosahedron has its own model and tint UBO; both share one indexed mesh.");
	const char* objectLabels[] = {"Object A", "Object B"};
	ImGui::Combo("Selected", &selectedObject, objectLabels, 2);
	ObjectState& object = objects[static_cast<std::size_t>(selectedObject)];
	ImGui::ColorEdit3("Tint", &object.tint.x, ImGuiColorEditFlags_NoInputs);
	ImGui::DragFloat3("Translation", &object.translation.x, 0.02f, -4.0f, 4.0f);
	ImGui::DragFloat3("Rotation", &object.rotation.x, 0.02f, -lab::math::pi * 4.0f, lab::math::pi * 4.0f);
	ImGui::DragFloat3("Scale", &object.scale.x, 0.01f, 0.25f, 2.0f);
	const char* trajectoryLabels[] = {"Lissajous", "Epicyclic"};
	int trajectory = static_cast<int>(object.trajectory);
	if (ImGui::Combo("Trajectory", &trajectory, trajectoryLabels, 2)) object.trajectory = static_cast<TrajectoryMode>(trajectory);
	ImGui::DragFloat("Radius", &object.radius, 0.01f, 0.0f, 2.5f);
	ImGui::DragFloat("Amplitude", &object.amplitude, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Frequency", &object.frequency, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Phase", &object.phase, 0.01f, -lab::math::pi * 4.0f, lab::math::pi * 4.0f);
	ImGui::End();
}

} // namespace

bool initialize() {
	clock.reset(0.0);
	if (!createPipeline() || !createGeometryAndUniforms()) {
		destroyResources();
		return false;
	}
	return true;
}

void shutdown() {
	if (device() != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(device());
		destroyResources();
	}
}

void update(double time) {
	lastUpdateWallTime = time;
	drawControls();
	clock.update(time);
}

void render(const graphics::internal::FrameData& fd) {
	if (fd.command_buffer == VK_NULL_HANDLE || pipeline == VK_NULL_HANDLE) return;
	const float width = static_cast<float>(graphics::internal::context.swapchain_extent.width);
	const float height = static_cast<float>(graphics::internal::context.swapchain_extent.height);
	const float aspect = height > 0.0f ? width / height : 1.0f;
	const Mat4 view = lab::math::lookAtRH({0.0f, 0.15f, 8.2f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
	const Mat4 projection = projectionMode == 0
		? lab::math::perspectiveRHZeroToOne(lab::math::degreesToRadians(52.0f), aspect, 0.1f, 40.0f)
		: lab::math::orthographicRHZeroToOne(9.0f * aspect, 9.0f, 0.1f, 40.0f);
	const GlobalUniforms globals{projection * view};
	std::memcpy(globalBuffer.mapped, &globals, sizeof(globals));
	vmaFlushAllocation(allocator(), globalBuffer.allocation, 0, sizeof(globals));
	for (std::size_t i = 0; i < objects.size(); ++i) {
		const ObjectState& object = objects[i];
		const Vec3 motion = lab::scene::trajectoryPosition(object.trajectory, clock.time(), object.radius,
		                                                   object.amplitude, object.frequency, object.phase);
		const Vec3 animationRotation = lab::scene::trajectoryRotation(clock.time(), clock.speed, object.phase);
		const Mat4 model = lab::math::translate(object.basePosition + object.translation + motion) *
		                   lab::math::eulerXYZ(object.rotation + animationRotation) * lab::math::scale(object.scale);
		const ObjectUniforms uniforms{model, object.tint};
		std::memcpy(objectBuffers[i].mapped, &uniforms, sizeof(uniforms));
		vmaFlushAllocation(allocator(), objectBuffers[i].allocation, 0, sizeof(uniforms));
	}

	vkResetCommandBuffer(fd.command_buffer, 0);
	const VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	if (vkBeginCommandBuffer(fd.command_buffer, &beginInfo) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to begin command buffer\n";
		return;
	}
	const VkClearValue clears[] = {
		{.color = {{0.018f, 0.026f, 0.055f, 1.0f}}},
		{.depthStencil = {1.0f, 0}},
	};
	const VkRenderPassBeginInfo renderPass{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = graphics::internal::context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = {.offset = {0, 0}, .extent = graphics::internal::context.swapchain_extent},
		.clearValueCount = 2, .pClearValues = clears,
	};
	vkCmdBeginRenderPass(fd.command_buffer, &renderPass, VK_SUBPASS_CONTENTS_INLINE);
	const VkViewport viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
	const VkRect2D scissor{{0, 0}, graphics::internal::context.swapchain_extent};
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer.handle, &offset);
	vkCmdBindIndexBuffer(fd.command_buffer, indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
	                        0, 1, &globalSet, 0, nullptr);
	for (std::size_t i = 0; i < objectSets.size(); ++i) {
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
		                        1, 1, &objectSets[i], 0, nullptr);
		vkCmdDrawIndexed(fd.command_buffer, indexCount, 1, 0, 0, 0);
	}
	vkCmdEndRenderPass(fd.command_buffer);
	if (vkEndCommandBuffer(fd.command_buffer) != VK_SUCCESS) {
		std::cerr << "Lab1: failed to end command buffer\n";
	}
}

} // namespace application
