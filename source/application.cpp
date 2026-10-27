#include "application.hpp"
#include "lighting_scene.hpp"
#include <imgui.h>
#include <vk_mem_alloc.h>
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
using namespace lab::math;
using namespace lab::lighting;
using lab::scene::AnimationClock;
using lab::scene::TrajectoryMode;
constexpr std::size_t objectCount = 3;
constexpr float panelWidth = 355.0f;
struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};
struct ObjectState {
    Vec3 position, rotation, scaling{1,1,1};
    Vec4 albedo{1,1,1,1}, specular{0.6f,0.6f,0.6f,1};
    float shininess = 64, procedural = 0;
    TrajectoryMode trajectory = TrajectoryMode::Lissajous;
    float radius = 0.55f, amplitude = 0.2f, frequency = 0.7f, phase = 0;
};
struct PointState {
    Vec3 position{-3,2,2}, color{1,0.52f,0.22f};
    float intensity = 14;
    bool enabled = true;
};
struct SpotState {
    Vec3 position{0,3.5f,1.5f}, color{0.65f,0.8f,1}, direction{0,-1,-0.3f};
    float intensity = 28, innerAngle = 18, outerAngle = 28;
    bool enabled = true;
};
struct MeshRange { std::uint32_t firstIndex = 0, indexCount = 0; };
VkDevice device() { return graphics::internal::context.device; }
VmaAllocator allocator() { return graphics::internal::context.allocator; }
VkShaderModule vertexShader = VK_NULL_HANDLE, fragmentShader = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE, markerPipeline = VK_NULL_HANDLE, linePipeline = VK_NULL_HANDLE;
VkDescriptorSetLayout sceneSetLayout = VK_NULL_HANDLE, objectSetLayout = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet sceneSet = VK_NULL_HANDLE;
std::array<VkDescriptorSet, objectCount> objectSets{};
Buffer vertexBuffer, indexBuffer, sceneBuffer, pointBuffer, spotBuffer, markerBuffer, lineBuffer;
std::array<Buffer, objectCount> objectBuffers{};
std::array<MeshRange, objectCount> meshRanges{};
std::array<ObjectState, objectCount> objects;
std::vector<PointState> points;
std::vector<SpotState> spots;
Camera camera;
AnimationClock clock;
Vec3 ambientColor{0.62f,0.72f,1}, directionalColor{1,0.91f,0.78f}, directionalDirection{-0.5f,-1,-0.35f};
float ambientIntensity = 0.13f, directionalIntensity = 0.75f, orthoHeight = 8;
bool directionalEnabled = true, showGizmos = true, showCones = true, animateLights = false, smoothNormals = false;
bool geometryDirty = false, resourceFailure = false;
int selectedObject = 0, selectedPoint = 0, selectedSpot = 0, projectionMode = 0;
int requestedTab = 0;
std::string demoName = "all", errorMessage;
std::size_t maxPointLights = 1, maxSpotLights = 1;
double previousTime = 0;
float frameDelta = 0;

void resetScene() {
    camera = {};
    objects = {};
    objects[0].position = {-1.65f,0,0};
    objects[0].albedo = {0.95f,0.44f,0.15f,1};
    objects[0].rotation = {0.1f,0.2f,0};
    objects[1].position = {1.65f,0,0};
    objects[1].albedo = {0.16f,0.57f,0.9f,1};
    objects[1].shininess = 128;
    objects[1].phase = 2.1f;
    objects[1].trajectory = TrajectoryMode::Epicyclic;
    objects[2].position = {0,-1.15f,0};
    objects[2].albedo = {0.24f,0.27f,0.32f,1};
    objects[2].specular = {0.12f,0.12f,0.12f,1};
    objects[2].shininess = 32;
    points = {PointState{}, PointState{{3,1.7f,-0.3f},{0.2f,0.65f,1},18,true}};
    spots = {SpotState{}};
    ambientColor = {0.62f,0.72f,1};
    directionalColor = {1,0.91f,0.78f};
    directionalDirection = {-0.5f,-1,-0.35f};
    ambientIntensity = 0.13f; directionalIntensity = 0.75f;
    directionalEnabled = true; showGizmos = true; showCones = true; animateLights = false;
    projectionMode = 0;
    clock.playing = false; clock.speed = 1; clock.reset(previousTime);
    selectedObject = selectedPoint = selectedSpot = 0;
    errorMessage.clear();
}
void applyDemo(const std::string& name) {
    resetScene();
    demoName = name;
    requestedTab = name == "all" ? 0 : 2;
    if (name == "directional") { points.clear(); spots.clear(); }
    if (name == "points") { directionalEnabled = false; spots.clear(); }
    if (name == "spots") {
        directionalEnabled = false; points.clear();
        spots[0].position = {-1.65f,3.3f,1.3f}; spots[0].direction = {0,-1,-0.36f};
        spots[0].color = {1,0.65f,0.28f}; spots[0].intensity = 40;
        SpotState second = spots[0]; second.position.x = 1.65f; second.color = {0.25f,0.65f,1};
        spots.push_back(second);
        ambientIntensity = 0.055f;
    }
    if (name == "empty") { directionalEnabled = false; points.clear(); spots.clear(); }
}
void destroyBuffer(Buffer& b) {
    if (b.handle) vmaDestroyBuffer(allocator(),b.handle,b.allocation);
    b = {};
}
bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& result) {
    const VkBufferCreateInfo info{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=size,
        .usage=usage, .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    const VmaAllocationCreateInfo alloc{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT |
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, .usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
    VmaAllocationInfo mapped{};
    if (vmaCreateBuffer(allocator(),&info,&alloc,&result.handle,&result.allocation,&mapped) != VK_SUCCESS) return false;
    result.mapped = mapped.pMappedData; result.size = size;
    if (!result.mapped) { destroyBuffer(result); return false; }
    return true;
}
bool ensureBuffer(Buffer& buffer, VkDeviceSize required, VkBufferUsageFlags usage, bool& changed) {
    if (buffer.handle && buffer.size >= required) return true;
    Buffer replacement;
    VkDeviceSize capacity = std::max<VkDeviceSize>(required,buffer.size * 2);
    if (usage == VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(graphics::internal::context.physical_device,&properties);
        capacity = std::min<VkDeviceSize>(capacity,properties.limits.maxStorageBufferRange);
    }
    if (!createBuffer(capacity,usage,replacement)) {
        errorMessage = "Unable to allocate GPU buffer"; resourceFailure = true; return false;
    }
    // render() runs only after prepare() has waited for the previous frame's fence.
    destroyBuffer(buffer); buffer = replacement; changed = true;
    return true;
}
void upload(const Buffer& buffer, const void* data, std::size_t bytes) {
    if (!bytes) return;
    std::memcpy(buffer.mapped,data,bytes);
    vmaFlushAllocation(allocator(),buffer.allocation,0,bytes);
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
		std::cerr << "Lab2: unable to read shader " << path << "\n";
		return VK_NULL_HANDLE;
	}
	const VkShaderModuleCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size() * sizeof(std::uint32_t),
		.pCode = code.data(),
	};
	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(device(), &createInfo, nullptr, &module) != VK_SUCCESS) {
		std::cerr << "Lab2: failed to create shader module " << path << "\n";
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
    const VkDescriptorSetLayoutBinding sceneBindings[] = {
        {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
    const VkDescriptorSetLayoutCreateInfo sceneInfo{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=3,.pBindings=sceneBindings};
    if (vkCreateDescriptorSetLayout(device(),&sceneInfo,nullptr,&sceneSetLayout) != VK_SUCCESS) return false;
    const VkDescriptorSetLayoutBinding modelBinding{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,
        VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    const VkDescriptorSetLayoutCreateInfo modelInfo{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=1,.pBindings=&modelBinding};
    if (vkCreateDescriptorSetLayout(device(),&modelInfo,nullptr,&objectSetLayout) != VK_SUCCESS) return false;
    const VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,objectCount+1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2}};
    const VkDescriptorPoolCreateInfo poolInfo{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=objectCount+1,.poolSizeCount=2,.pPoolSizes=poolSizes};
    if (vkCreateDescriptorPool(device(),&poolInfo,nullptr,&descriptorPool) != VK_SUCCESS) return false;
    const std::array<VkDescriptorSetLayout,objectCount+1> layouts{sceneSetLayout,objectSetLayout,objectSetLayout,objectSetLayout};
    std::array<VkDescriptorSet,objectCount+1> sets{};
    const VkDescriptorSetAllocateInfo alloc{.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=descriptorPool,.descriptorSetCount=static_cast<std::uint32_t>(sets.size()),.pSetLayouts=layouts.data()};
    if (vkAllocateDescriptorSets(device(),&alloc,sets.data()) != VK_SUCCESS) return false;
    sceneSet = sets[0];
    std::copy(sets.begin()+1,sets.end(),objectSets.begin());
    const VkDescriptorSetLayout pipelineLayouts[] = {sceneSetLayout,objectSetLayout};
    const VkPipelineLayoutCreateInfo info{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=2,.pSetLayouts=pipelineLayouts};
    return vkCreatePipelineLayout(device(),&info,nullptr,&pipelineLayout) == VK_SUCCESS;
}
void writeDescriptorSets() {
    std::array<VkDescriptorBufferInfo,objectCount+3> infos{};
    infos[0] = {sceneBuffer.handle,0,sizeof(SceneUniforms)};
    infos[1] = {pointBuffer.handle,0,pointBuffer.size};
    infos[2] = {spotBuffer.handle,0,spotBuffer.size};
    for (std::size_t i=0;i<objectCount;++i) infos[i+3] = {objectBuffers[i].handle,0,sizeof(ModelUniforms)};
    std::array<VkWriteDescriptorSet,objectCount+3> writes{};
    for (std::size_t i=0;i<writes.size();++i) {
        writes[i] = {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet=i<3 ? sceneSet : objectSets[i-3], .dstBinding=i<3 ? static_cast<std::uint32_t>(i) : 0,
            .descriptorCount=1,.descriptorType=(i==1||i==2) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo=&infos[i]};
    }
    vkUpdateDescriptorSets(device(),static_cast<std::uint32_t>(writes.size()),writes.data(),0,nullptr);
}
bool createPipeline(const char* vertexFile, const char* fragmentFile, VkPrimitiveTopology topology, VkPipeline& output, bool gizmo) {
	vertexShader = loadShader(vertexFile);
	fragmentShader = loadShader(fragmentFile);
	if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE) {
		return false;
	}
	const VkPipelineShaderStageCreateInfo stages[] = {
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
		 .module = vertexShader, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		 .module = fragmentShader, .pName = "main"},
	};
	const VkVertexInputBindingDescription binding{0, sizeof(LitVertex), VK_VERTEX_INPUT_RATE_VERTEX};
	const VkVertexInputAttributeDescription attributes[] = {
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LitVertex, position)},
		{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LitVertex, normal)},
		{2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LitVertex, color)},
	};
	const VkVertexInputAttributeDescription gizmoAttributes[] = {attributes[0], attributes[2]};
	const VkPipelineVertexInputStateCreateInfo vertexInput{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = gizmo ? 2u : 3u, .pVertexAttributeDescriptions = gizmo ? gizmoAttributes : attributes,
	};
	const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = topology,
	};
	const VkPipelineViewportStateCreateInfo viewportState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1, .scissorCount = 1,
	};
	const VkPipelineRasterizationStateCreateInfo rasterization{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL, .cullMode = gizmo ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f,
	};
	const VkPipelineMultisampleStateCreateInfo multisample{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	const VkPipelineDepthStencilStateCreateInfo depthStencil{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE, .depthWriteEnable = gizmo ? VK_FALSE : VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS,
	};
	const VkPipelineColorBlendAttachmentState blendAttachment{
		.blendEnable = VK_FALSE,
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
	if (vkCreateGraphicsPipelines(device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &output) != VK_SUCCESS) {
		std::cerr << "Lab2: failed to create graphics pipeline\n";
		return false;
	}
	destroyShader(vertexShader);
	destroyShader(fragmentShader);
	return true;
}

bool updateGeometry() {
    const auto ico = makeLitIcosahedron(smoothNormals);
    const auto floor = makeFloor();
    std::vector<LitVertex> vertices = ico.vertices;
    std::vector<std::uint32_t> indices = ico.indices;
    meshRanges[0] = meshRanges[1] = {0,static_cast<std::uint32_t>(indices.size())};
    meshRanges[2] = {static_cast<std::uint32_t>(indices.size()),static_cast<std::uint32_t>(floor.indices.size())};
    for (auto i : floor.indices) indices.push_back(i+static_cast<std::uint32_t>(vertices.size()));
    vertices.insert(vertices.end(),floor.vertices.begin(),floor.vertices.end());
    bool changed = false;
    if (!ensureBuffer(vertexBuffer,vertices.size()*sizeof(LitVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,changed) ||
        !ensureBuffer(indexBuffer,indices.size()*sizeof(std::uint32_t),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,changed)) return false;
    upload(vertexBuffer,vertices.data(),vertices.size()*sizeof(LitVertex));
    upload(indexBuffer,indices.data(),indices.size()*sizeof(std::uint32_t));
    geometryDirty = false;
    return true;
}
void destroyResources() {
    for (auto* p : {&pipeline,&markerPipeline,&linePipeline}) {
        if (*p) vkDestroyPipeline(device(),*p,nullptr); *p = VK_NULL_HANDLE;
    }
    if (pipelineLayout) vkDestroyPipelineLayout(device(),pipelineLayout,nullptr);
    if (descriptorPool) vkDestroyDescriptorPool(device(),descriptorPool,nullptr);
    if (sceneSetLayout) vkDestroyDescriptorSetLayout(device(),sceneSetLayout,nullptr);
    if (objectSetLayout) vkDestroyDescriptorSetLayout(device(),objectSetLayout,nullptr);
    pipelineLayout = VK_NULL_HANDLE; descriptorPool = VK_NULL_HANDLE;
    sceneSetLayout = objectSetLayout = VK_NULL_HANDLE;
    for (auto& b : objectBuffers) destroyBuffer(b);
    for (auto* b : {&vertexBuffer,&indexBuffer,&sceneBuffer,&pointBuffer,&spotBuffer,&markerBuffer,&lineBuffer}) destroyBuffer(*b);
    destroyShader(vertexShader); destroyShader(fragmentShader);
}
Vec3 animatedPosition(Vec3 position, float phase) {
    if (!animateLights) return position;
    const auto t = clock.time()*0.7f+phase;
    return position+Vec3{0.6f*std::cos(t),0.2f*std::sin(t),0.6f*std::sin(t)};
}
void cameraControls() {
    ImGui::TextWrapped("Right mouse + WASD: fly. Q/E: down/up. Shift: faster.");
    ImGui::DragFloat3("Position",&camera.position.x,0.05f);
    ImGui::DragFloat3("Pitch/yaw/roll",&camera.rotation.x,0.3f);
    camera.rotation.x = std::clamp(camera.rotation.x,-89.0f,89.0f);
    ImGui::SliderFloat("Field of view",&camera.fov,20,100,"%.0f deg",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Move speed",&camera.speed,0.5f,12,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::RadioButton("Perspective",&projectionMode,0); ImGui::SameLine();
    ImGui::RadioButton("Orthographic",&projectionMode,1);
    if (projectionMode) ImGui::SliderFloat("Ortho height",&orthoHeight,3,20,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::Button("Reset camera")) camera = {};
    ImGui::Spacing(); ImGui::SeparatorText("Ambient light");
    ImGui::ColorEdit3("Ambient color",&ambientColor.x,ImGuiColorEditFlags_NoInputs);
    ImGui::SliderFloat("Ambient intensity",&ambientIntensity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);
}
void objectControls() {
    const char* labels[] = {"Icosahedron A","Icosahedron B","Floor"};
    ImGui::Combo("Object",&selectedObject,labels,3);
    auto& o = objects[static_cast<std::size_t>(selectedObject)];
    ImGui::DragFloat3("Translation",&o.position.x,0.02f);
    ImGui::DragFloat3("Rotation (rad)",&o.rotation.x,0.02f);
    ImGui::DragFloat3("Scale",&o.scaling.x,0.01f,0.1f,3,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    for (float* s : {&o.scaling.x,&o.scaling.y,&o.scaling.z}) *s = std::max(*s,0.1f);
    ImGui::SeparatorText("Blinn-Phong material");
    ImGui::ColorEdit3("Albedo",&o.albedo.x,ImGuiColorEditFlags_NoInputs);
    ImGui::ColorEdit3("Specular color",&o.specular.x,ImGuiColorEditFlags_NoInputs);
    ImGui::SliderFloat("Shininess",&o.shininess,1,256,"%.0f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Vertex color mix",&o.procedural,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    if (selectedObject < 2) {
        if (ImGui::Checkbox("Smooth vertex normals",&smoothNormals)) geometryDirty = true;
        ImGui::SeparatorText("Object trajectory");
        int mode = static_cast<int>(o.trajectory);
        if (ImGui::Combo("Trajectory",&mode,"Lissajous\0Epicyclic\0")) o.trajectory = static_cast<TrajectoryMode>(mode);
        ImGui::SliderFloat("Radius",&o.radius,0,1.5f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Amplitude",&o.amplitude,0,0.7f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Frequency",&o.frequency,0,3,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Phase",&o.phase,-pi,pi,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    }
}
void lightColor(Vec3& color, float& intensity) {
    ImGui::ColorEdit3("Color",&color.x,ImGuiColorEditFlags_NoInputs);
    ImGui::DragFloat("Intensity",&intensity,0.1f,0,200,"%.2f",ImGuiSliderFlags_AlwaysClamp);
}
void lightControls() {
    ImGui::Checkbox("Show light markers",&showGizmos);
    ImGui::SameLine(); ImGui::Checkbox("Cones",&showCones);
    ImGui::Checkbox("Animate lights",&animateLights);
    ImGui::TextWrapped("Markers use constant color. White: outer cone; colored: inner cone.");
    if (ImGui::CollapsingHeader("Directional light",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("directional");
        ImGui::Checkbox("Enabled",&directionalEnabled);
        ImGui::DragFloat3("Ray direction",&directionalDirection.x,0.02f);
        lightColor(directionalColor,directionalIntensity);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Point lights",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("points");
        ImGui::BeginDisabled(points.size()>=maxPointLights);
        if (ImGui::Button("Add point")) {
            points.push_back(PointState{}); selectedPoint = static_cast<int>(points.size())-1;
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(points.empty());
        if (ImGui::Button("Remove point")) {
            points.erase(points.begin()+selectedPoint); selectedPoint = std::max(0,selectedPoint-1);
        }
        ImGui::EndDisabled();
        ImGui::Text("Count: %zu",points.size());
        if (!points.empty()) {
            if (ImGui::BeginCombo("Selected point",("Point "+std::to_string(selectedPoint+1)).c_str())) {
                for (int i=0;i<static_cast<int>(points.size());++i)
                    if (ImGui::Selectable(("Point "+std::to_string(i+1)).c_str(),i==selectedPoint)) selectedPoint = i;
                ImGui::EndCombo();
            }
            auto& p = points[static_cast<std::size_t>(selectedPoint)];
            ImGui::Checkbox("Enabled",&p.enabled);
            ImGui::DragFloat3("Position",&p.position.x,0.03f);
            lightColor(p.color,p.intensity);
        }
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Spot lights",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("spots");
        ImGui::BeginDisabled(spots.size()>=maxSpotLights);
        if (ImGui::Button("Add spot")) {
            spots.push_back(SpotState{}); selectedSpot = static_cast<int>(spots.size())-1;
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(spots.empty());
        if (ImGui::Button("Remove spot")) {
            spots.erase(spots.begin()+selectedSpot); selectedSpot = std::max(0,selectedSpot-1);
        }
        ImGui::EndDisabled();
        ImGui::Text("Count: %zu",spots.size());
        if (!spots.empty()) {
            if (ImGui::BeginCombo("Selected spot",("Spot "+std::to_string(selectedSpot+1)).c_str())) {
                for (int i=0;i<static_cast<int>(spots.size());++i)
                    if (ImGui::Selectable(("Spot "+std::to_string(i+1)).c_str(),i==selectedSpot)) selectedSpot = i;
                ImGui::EndCombo();
            }
            auto& s = spots[static_cast<std::size_t>(selectedSpot)];
            ImGui::Checkbox("Enabled",&s.enabled);
            ImGui::DragFloat3("Position",&s.position.x,0.03f);
            ImGui::DragFloat3("Ray direction",&s.direction.x,0.02f);
            lightColor(s.color,s.intensity);
            ImGui::SliderFloat("Inner half-angle",&s.innerAngle,0,75,"%.1f deg",ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Outer half-angle",&s.outerAngle,0,80,"%.1f deg",ImGuiSliderFlags_AlwaysClamp);
            s.outerAngle = std::clamp(s.outerAngle,0.0f,80.0f);
            s.innerAngle = std::clamp(s.innerAngle,0.0f,s.outerAngle);
            ImGui::TextWrapped("Equal angles: hard edge. Larger outer angle: soft edge.");
        }
        ImGui::PopID();
    }
}
void drawControls() {
    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0,0},ImGuiCond_Always);
    ImGui::SetNextWindowSize({std::min(panelWidth,io.DisplaySize.x),io.DisplaySize.y},ImGuiCond_Always);
    ImGui::Begin("CG Lab 2",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoTitleBar);
    ImGui::PushItemWidth(175.0f);
    ImGui::TextUnformatted("CAMERA & LIGHTING");
    ImGui::TextDisabled("Variant 11 / Regular icosahedron");
    ImGui::Separator();
    if (ImGui::Button("Play / pause")) clock.playing = !clock.playing;
    ImGui::SameLine();
    if (ImGui::Button("Reset time")) clock.reset(previousTime);
    ImGui::SameLine(); ImGui::Text("%s",clock.playing ? "Playing" : "Paused");
    ImGui::SliderFloat("Animation speed",&clock.speed,0,3,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    const char* demos[] = {"All lights","Directional only","Point lights only","Spot lights only","Ambient only"};
    int selected = demoName=="directional"?1:demoName=="points"?2:demoName=="spots"?3:demoName=="empty"?4:0;
    if (ImGui::Combo("Scene preset",&selected,demos,5)) {
        const char* names[] = {"all","directional","points","spots","empty"}; applyDemo(names[selected]);
    }
    ImGui::Separator();
    if (ImGui::BeginTabBar("Controls")) {
        if (ImGui::BeginTabItem("Camera",nullptr,requestedTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) { cameraControls(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Materials",nullptr,requestedTab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) { objectControls(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Lights",nullptr,requestedTab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) { lightControls(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
        requestedTab = -1;
    }
    ImGui::Spacing(); ImGui::Separator();
    ImGui::TextDisabled("%zu point / %zu spot",points.size(),spots.size());
    ImGui::TextDisabled("%.1f FPS",io.Framerate);
    if (!errorMessage.empty()) ImGui::TextWrapped("%s",errorMessage.c_str());
    ImGui::PopItemWidth();
    ImGui::End();
}
void flyCamera() {
    const auto& io = ImGui::GetIO();
    if (io.WantCaptureMouse || !ImGui::IsMouseDown(ImGuiMouseButton_Right)) return;
    camera.rotation.y -= io.MouseDelta.x * 0.15f;
    camera.rotation.x = std::clamp(camera.rotation.x-io.MouseDelta.y*0.15f,-89.0f,89.0f);
    if (io.WantCaptureKeyboard) return;
    Vec3 movement{};
    if (ImGui::IsKeyDown(ImGuiKey_W)) movement = movement+camera.axis({0,0,-1});
    if (ImGui::IsKeyDown(ImGuiKey_S)) movement = movement-camera.axis({0,0,-1});
    if (ImGui::IsKeyDown(ImGuiKey_D)) movement = movement+camera.axis({1,0,0});
    if (ImGui::IsKeyDown(ImGuiKey_A)) movement = movement-camera.axis({1,0,0});
    if (ImGui::IsKeyDown(ImGuiKey_E)) movement.y += 1;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) movement.y -= 1;
    camera.position = camera.position+normalize(movement)*camera.speed*frameDelta*(io.KeyShift?3.0f:1.0f);
}
} // namespace

void setDemo(const char* name) { demoName = name; }
bool healthy() { return !resourceFailure; }
bool initialize() {
    applyDemo(demoName);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(graphics::internal::context.physical_device,&properties);
    maxPointLights = properties.limits.maxStorageBufferRange/sizeof(PointLight);
    maxSpotLights = properties.limits.maxStorageBufferRange/sizeof(SpotLight);
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding=0; style.FrameRounding=4; style.ItemSpacing={8,9}; style.WindowPadding={14,16};
    if (!createDescriptors() ||
        !createPipeline("lab.vert.spv","lab.frag.spv",VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,pipeline,false) ||
        !createPipeline("gizmo.vert.spv","gizmo.frag.spv",VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,markerPipeline,true) ||
        !createPipeline("gizmo.vert.spv","gizmo.frag.spv",VK_PRIMITIVE_TOPOLOGY_LINE_LIST,linePipeline,true) ||
        !updateGeometry() || !createBuffer(sizeof(SceneUniforms),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,sceneBuffer) ||
        !createBuffer(sizeof(PointLight),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,pointBuffer) ||
        !createBuffer(sizeof(SpotLight),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,spotBuffer)) {
        destroyResources(); return false;
    }
    for (auto& b : objectBuffers) if (!createBuffer(sizeof(ModelUniforms),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,b)) {
        destroyResources(); return false;
    }
    writeDescriptorSets();
    return true;
}
void shutdown() {
    if (device()) { vkDeviceWaitIdle(device()); destroyResources(); }
}
void update(double time) {
    frameDelta = static_cast<float>(std::clamp(time-previousTime,0.0,0.1)); previousTime = time;
    drawControls(); flyCamera(); clock.update(time);
}

// Deterministic GPU integration scenario: reallocate, remove all, refill, change normals/camera.
void verificationStep(int frame) {
    if (frame==0) applyDemo("empty");
    if (frame==5) { points.resize(9); spots.resize(7); }
    if (frame==10) { points.clear(); spots.clear(); }
    if (frame==15) { points.resize(33); spots.resize(19); }
    if (frame==20) { points.resize(1); spots.resize(1); spots[0].innerAngle=spots[0].outerAngle; }
    if (frame==25) { spots[0].innerAngle=0; spots[0].outerAngle=80; spots[0].direction={}; }
    if (frame==30) { smoothNormals=true; geometryDirty=true; objects[0].scaling={2,0.3f,1.2f}; }
    if (frame==35) { showGizmos=false; projectionMode=1; camera.rotation={-40,70,20}; }
    if (frame==40) { showGizmos=true; showCones=false; }
    if (frame==45) { smoothNormals=false; geometryDirty=true; applyDemo("all"); clock.playing=true; animateLights=true; }
    if (frame==60 && !resourceFailure) std::cout << "Lab2 GPU scenario: empty / 33 point + 19 spot / hard + soft cones / normals / camera / markers completed\n";
}

void render(const graphics::internal::FrameData& fd) {
    if (!fd.command_buffer || !pipeline || resourceFailure) return;
    if (geometryDirty && !updateGeometry()) return;
    std::vector<PointLight> gpuPoints;
    std::vector<SpotLight> gpuSpots;
    std::vector<LitVertex> markers, lines;
    for (std::size_t i=0;i<points.size();++i) {
        const auto& p=points[i]; const Vec3 position=animatedPosition(p.position,static_cast<float>(i));
        gpuPoints.push_back({{position.x,position.y,position.z,1},{p.color.x,p.color.y,p.color.z,p.enabled?p.intensity:0}});
        if (showGizmos) appendMarker(markers,position,p.color);
    }
    for (std::size_t i=0;i<spots.size();++i) {
        const auto& s=spots[i]; const Vec3 position=animatedPosition(s.position,static_cast<float>(i)+1);
        const Vec3 d=safeDirection(s.direction);
        gpuSpots.push_back({{position.x,position.y,position.z,1},{s.color.x,s.color.y,s.color.z,s.enabled?s.intensity:0},
            {d.x,d.y,d.z,std::cos(degreesToRadians(s.innerAngle))},{std::cos(degreesToRadians(s.outerAngle)),0,0,0}});
        if (showGizmos) {
            appendMarker(markers,position,s.color);
            if (showCones) {
                appendCone(lines,position,d,s.outerAngle,2.6f,{0.85f,0.87f,0.9f});
                appendCone(lines,position,d,s.innerAngle,2.6f,s.color*0.6f);
            }
        }
    }
    bool storageChanged=false, vertexChanged=false;
    if (!ensureBuffer(pointBuffer,std::max<std::size_t>(1,gpuPoints.size())*sizeof(PointLight),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,storageChanged) ||
        !ensureBuffer(spotBuffer,std::max<std::size_t>(1,gpuSpots.size())*sizeof(SpotLight),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,storageChanged)) return;
    if (!markers.empty() && !ensureBuffer(markerBuffer,markers.size()*sizeof(LitVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertexChanged)) return;
    if (!lines.empty() && !ensureBuffer(lineBuffer,lines.size()*sizeof(LitVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertexChanged)) return;
    if (storageChanged) writeDescriptorSets();
    upload(pointBuffer,gpuPoints.data(),gpuPoints.size()*sizeof(PointLight));
    upload(spotBuffer,gpuSpots.data(),gpuSpots.size()*sizeof(SpotLight));
    upload(markerBuffer,markers.data(),markers.size()*sizeof(LitVertex));
    upload(lineBuffer,lines.data(),lines.size()*sizeof(LitVertex));
    const auto extent=graphics::internal::context.swapchain_extent;
    const float fbScale=static_cast<float>(extent.width)/std::max(ImGui::GetIO().DisplaySize.x,1.0f);
    const float left=std::min(panelWidth*fbScale,static_cast<float>(extent.width)-1);
    const float width=static_cast<float>(extent.width)-left, height=static_cast<float>(extent.height);
    const Mat4 projection=projectionMode==0 ? perspectiveRHZeroToOne(degreesToRadians(camera.fov),width/height,0.1f,100) :
        orthographicRHZeroToOne(orthoHeight*width/height,orthoHeight,0.1f,100);
    const Vec3 d=safeDirection(directionalDirection);
    const SceneUniforms globals{projection*camera.view(),{camera.position.x,camera.position.y,camera.position.z,1},
        {ambientColor.x,ambientColor.y,ambientColor.z,ambientIntensity},{d.x,d.y,d.z,0},
        {directionalColor.x,directionalColor.y,directionalColor.z,directionalEnabled?directionalIntensity:0},
        {static_cast<std::uint32_t>(points.size()),static_cast<std::uint32_t>(spots.size()),0,0}};
    upload(sceneBuffer,&globals,sizeof(globals));
    for (std::size_t i=0;i<objectCount;++i) {
        const auto& o=objects[i];
        Vec3 motion{}, rotation=o.rotation;
        if (i<2) {
            motion=lab::scene::trajectoryPosition(o.trajectory,clock.time(),o.radius,o.amplitude,o.frequency,o.phase);
            rotation=rotation+lab::scene::trajectoryRotation(clock.time(),1,o.phase);
        }
        const ModelUniforms data{translate(o.position+motion)*eulerXYZ(rotation)*scale(o.scaling),normalTransform(rotation,o.scaling),
            o.albedo,o.specular,{o.shininess,o.procedural,0,0}};
        upload(objectBuffers[i],&data,sizeof(data));
    }
    vkResetCommandBuffer(fd.command_buffer,0);
    const VkCommandBufferBeginInfo begin{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(fd.command_buffer,&begin)!=VK_SUCCESS) { resourceFailure=true; return; }
    const VkClearValue clears[]={ {.color={{0.014f,0.021f,0.035f,1}}}, {.depthStencil={1,0}} };
    const VkRenderPassBeginInfo pass{.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass=graphics::internal::context.render_pass,.framebuffer=fd.framebuffer,
        .renderArea={.offset={0,0},.extent=extent},.clearValueCount=2,.pClearValues=clears};
    vkCmdBeginRenderPass(fd.command_buffer,&pass,VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{left,0,width,height,0,1};
    const VkRect2D scissor{{static_cast<int>(left),0},{extent.width-static_cast<std::uint32_t>(left),extent.height}};
    vkCmdSetViewport(fd.command_buffer,0,1,&viewport); vkCmdSetScissor(fd.command_buffer,0,1,&scissor);
    vkCmdBindPipeline(fd.command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    const VkDeviceSize offset=0;
    vkCmdBindVertexBuffers(fd.command_buffer,0,1,&vertexBuffer.handle,&offset);
    vkCmdBindIndexBuffer(fd.command_buffer,indexBuffer.handle,0,VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(fd.command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&sceneSet,0,nullptr);
    for (std::size_t i=0;i<objectCount;++i) {
        vkCmdBindDescriptorSets(fd.command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,1,1,&objectSets[i],0,nullptr);
        vkCmdDrawIndexed(fd.command_buffer,meshRanges[i].indexCount,1,meshRanges[i].firstIndex,0,0);
    }
    if (!markers.empty()) {
        vkCmdBindPipeline(fd.command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,markerPipeline);
        vkCmdBindVertexBuffers(fd.command_buffer,0,1,&markerBuffer.handle,&offset);
        vkCmdDraw(fd.command_buffer,static_cast<std::uint32_t>(markers.size()),1,0,0);
    }
    if (!lines.empty()) {
        vkCmdBindPipeline(fd.command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,linePipeline);
        vkCmdBindVertexBuffers(fd.command_buffer,0,1,&lineBuffer.handle,&offset);
        vkCmdDraw(fd.command_buffer,static_cast<std::uint32_t>(lines.size()),1,0,0);
    }
    vkCmdEndRenderPass(fd.command_buffer);
    if (vkEndCommandBuffer(fd.command_buffer)!=VK_SUCCESS) resourceFailure=true;
}
} // namespace application
