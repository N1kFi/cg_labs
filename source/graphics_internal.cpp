#include "graphics_internal.hpp"

#include <iostream>
#include <atomic>
#include <string>
#include "frame_capture.hpp"
#include <vector>

#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>

#include <VkBootstrap.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4100 4189 4324)
#endif // _MSC_VER
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#include <backends/imgui_impl_vulkan.h>

namespace graphics::internal {

namespace {

std::atomic_uint32_t validation_errors{0};
VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++validation_errors;
    std::cerr << ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "[Vulkan ERROR] " : "[Vulkan WARNING] ")
              << data->pMessage << '\n';
    return VK_FALSE;
}
VkInstance vk_instance = VK_NULL_HANDLE;
VkDebugUtilsMessengerEXT vk_debug_messenger = VK_NULL_HANDLE;
uint32_t vk_api_version = VK_MAKE_VERSION(1, 0, 0);
VkSurfaceKHR vk_surface = VK_NULL_HANDLE;

VkSwapchainKHR vk_swapchain = VK_NULL_HANDLE;
std::vector<VkImage> vk_swapchain_images;
std::vector<VkImageView> vk_swapchain_image_views;
uint32_t vk_swapchain_current_image = UINT32_MAX;

uint32_t vk_swapchain_resize_width = 0;
uint32_t vk_swapchain_resize_height = 0;
bool vk_swapchain_resize_require = false;
bool vk_fatal_error = false;
bool vk_capture_supported = false;
std::string capture_path;
bool vk_imgui_initialized = false;

VkFormat vk_depth_buffer_format = VK_FORMAT_UNDEFINED;
VkImage vk_image_depth_buffer;
VmaAllocation vma_allocation_depth_buffer;
VkImageView vk_image_view_depth_buffer;

std::vector<VkFramebuffer> vk_framebuffers;

VkSemaphore vk_semaphore_image_available;
std::vector<VkSemaphore> vk_semaphores_image_finished;
VkFence vk_fence_frame_in_flight;

VkCommandPool vk_command_pool;
VkCommandBuffer vk_command_buffer;

VkDescriptorPool vk_imgui_descriptor_pool;
VkRenderPass vk_imgui_render_pass;
std::vector<VkFramebuffer> vk_imgui_framebuffers;
VkCommandPool vk_imgui_command_pool;
VkCommandBuffer vk_imgui_command_buffer;

VkFormat selectDepthFormat(VkPhysicalDevice physical_device) {
	// Prefer the original format and preserve stencil support in the fallback.
	const VkFormat candidates[] = {
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D32_SFLOAT_S8_UINT,
	};

	for (VkFormat format : candidates) {
		VkFormatProperties properties{};
		vkGetPhysicalDeviceFormatProperties(physical_device, format, &properties);
		if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			return format;
		}
	}

	return VK_FORMAT_UNDEFINED;
}

void destroyImageFinishedSemaphores(std::vector<VkSemaphore>& semaphores) {
	for (VkSemaphore semaphore : semaphores) {
		if (semaphore != VK_NULL_HANDLE) {
			vkDestroySemaphore(context.device, semaphore, nullptr);
		}
	}
	semaphores.clear();
}

bool createImageFinishedSemaphores(uint32_t count, std::vector<VkSemaphore>& semaphores) {
	const VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	semaphores.assign(count, VK_NULL_HANDLE);
	for (uint32_t i = 0; i < count; ++i) {
		if (vkCreateSemaphore(context.device, &semaphore, nullptr, &semaphores[i]) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan semaphore #" << i << " for finished image\n";
			destroyImageFinishedSemaphores(semaphores);
			return false;
		}
	}
	return true;
}

void destroyDepthBuffer() {
	if (vk_image_view_depth_buffer != VK_NULL_HANDLE) {
		vkDestroyImageView(context.device, vk_image_view_depth_buffer, nullptr);
		vk_image_view_depth_buffer = VK_NULL_HANDLE;
	}
	if (vk_image_depth_buffer != VK_NULL_HANDLE) {
		vmaDestroyImage(context.allocator, vk_image_depth_buffer, vma_allocation_depth_buffer);
		vk_image_depth_buffer = VK_NULL_HANDLE;
		vma_allocation_depth_buffer = VK_NULL_HANDLE;
	}
}

bool createDepthBuffer() {
	const VkImageCreateInfo depth_buffer = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = vk_depth_buffer_format,
		.extent = { context.swapchain_extent.width, context.swapchain_extent.height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	const VmaAllocationCreateInfo allocation{.usage = VMA_MEMORY_USAGE_AUTO};
	if (vmaCreateImage(context.allocator, &depth_buffer, &allocation, &vk_image_depth_buffer,
	                   &vma_allocation_depth_buffer, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to allocate and create Vulkan image for depth buffer\n";
		return false;
	}

	const VkImageViewCreateInfo view = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = vk_image_depth_buffer,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = vk_depth_buffer_format,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};
	if (vkCreateImageView(context.device, &view, nullptr, &vk_image_view_depth_buffer) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan image view for depth buffer\n";
		destroyDepthBuffer();
		return false;
	}
	return true;
}

bool initializeImGUI() {
	const VkDescriptorPoolSize descriptor_pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE,
		},
	};

	const VkDescriptorPoolCreateInfo descriptor_pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = uint32_t(vk_swapchain_images.size()),
		.poolSizeCount = sizeof(descriptor_pool_sizes) / sizeof(descriptor_pool_sizes[0]),
		.pPoolSizes = descriptor_pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &descriptor_pool, nullptr,
							   &vk_imgui_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool for ImGUI rendering\n";
		return false;
	}

	const VkAttachmentDescription render_pass_attachment = {
		.format = context.swapchain_format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};

	const VkAttachmentReference render_pass_attachment_ref = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	const VkSubpassDescription render_pass_subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &render_pass_attachment_ref,
	};

	const VkSubpassDependency render_pass_dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
						 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
	};

	const VkRenderPassCreateInfo render_pass = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &render_pass_attachment,
		.subpassCount = 1,
		.pSubpasses = &render_pass_subpass,
		.dependencyCount = 1,
		.pDependencies = &render_pass_dependency,
	};

	if (vkCreateRenderPass(context.device, &render_pass, nullptr,
						   &vk_imgui_render_pass) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan render pass for ImGUI rendering\n";
		return false;
	}

	const uint32_t swapchain_images_count = uint32_t(vk_swapchain_images.size());

	vk_imgui_framebuffers.resize(swapchain_images_count);

	VkFramebufferCreateInfo framebuffer = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = vk_imgui_render_pass,
		.attachmentCount = 1,
		.width = context.swapchain_extent.width,
		.height = context.swapchain_extent.height,
		.layers = 1,
	};

	for (uint32_t i = 0; i < swapchain_images_count; ++i) {
		framebuffer.pAttachments = &vk_swapchain_image_views[i];

		if (vkCreateFramebuffer(context.device, &framebuffer, nullptr,
								&vk_imgui_framebuffers[i]) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan framebuffer #" << i << " for ImGUI rendering\n";
			return false;
		}
	}

	const VkCommandPoolCreateInfo command_pool = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = context.graphics_queue_index,
	};

	if (vkCreateCommandPool(context.device, &command_pool, nullptr,
							&vk_imgui_command_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan command pool for ImGUI rendering\n";
		return false;
	}

	const VkCommandBufferAllocateInfo command_buffer = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vk_imgui_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	if (vkAllocateCommandBuffers(context.device, &command_buffer,
								 &vk_imgui_command_buffer) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan command buffer for ImGUI rendering\n";
		return false;
	}

	ImGui_ImplVulkan_InitInfo init = {
		.ApiVersion = vk_api_version,
		.Instance = vk_instance,
		.PhysicalDevice = context.physical_device,
		.Device = context.device,
		.QueueFamily = context.graphics_queue_index,
		.Queue = context.graphics_queue,
		.DescriptorPool = vk_imgui_descriptor_pool,
		.MinImageCount = swapchain_images_count,
		.ImageCount = swapchain_images_count,
		.PipelineInfoMain = {
			.RenderPass = vk_imgui_render_pass,
		},
	};

	return ImGui_ImplVulkan_Init(&init);
}

void drawImGUI() {
	vkResetCommandBuffer(vk_imgui_command_buffer, 0);

	const VkCommandBufferBeginInfo command_buffer_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(vk_imgui_command_buffer, &command_buffer_begin);

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = vk_imgui_render_pass,
		.framebuffer = vk_imgui_framebuffers[vk_swapchain_current_image],
		.renderArea = { .extent = context.swapchain_extent },
	};

	vkCmdBeginRenderPass(vk_imgui_command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vk_imgui_command_buffer);

	vkCmdEndRenderPass(vk_imgui_command_buffer);

	vkEndCommandBuffer(vk_imgui_command_buffer);
}

bool rebuildSwapchain(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0) return true;
	if (vkQueueWaitIdle(context.graphics_queue) != VK_SUCCESS) {
		std::cerr << "Failed to wait for graphics queue during swapchain rebuild\n";
		return false;
	}

	vkb::SwapchainBuilder sb(context.physical_device, context.device, vk_surface,
	                         context.graphics_queue_index, context.graphics_queue_index);
	auto sb_result = sb.set_desired_extent(width, height)
	                   .use_default_format_selection()
	                   .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
	                   .use_default_image_usage_flags()
                       .add_image_usage_flags(vk_capture_supported ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0)
	                   .set_old_swapchain(vk_swapchain)
	                   .build();
	if (!sb_result) {
		std::cerr << "Failed to rebuild Vulkan swapchain: " << sb_result.error().message() << '\n';
		return false;
	}
	auto vkb_swapchain = std::move(sb_result.value());
	auto images_result = vkb_swapchain.get_images();
	if (!images_result) {
		std::cerr << "Failed to get rebuilt Vulkan swapchain images: " << images_result.error().message() << '\n';
		vkb::destroy_swapchain(vkb_swapchain);
		return false;
	}
	auto views_result = vkb_swapchain.get_image_views();
	if (!views_result) {
		std::cerr << "Failed to get rebuilt Vulkan swapchain image views: " << views_result.error().message() << '\n';
		vkb::destroy_swapchain(vkb_swapchain);
		return false;
	}
	std::vector<VkImage> new_images = std::move(images_result.value());
	std::vector<VkImageView> new_views = std::move(views_result.value());
	if (new_images.empty() || new_images.size() != new_views.size()) {
		std::cerr << "Rebuilt Vulkan swapchain returned no matching images and views\n";
		for (VkImageView view : new_views) vkDestroyImageView(context.device, view, nullptr);
		vkb::destroy_swapchain(vkb_swapchain);
		return false;
	}

	for (VkFramebuffer framebuffer : vk_imgui_framebuffers)
		if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(context.device, framebuffer, nullptr);
	for (VkFramebuffer framebuffer : vk_framebuffers)
		if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(context.device, framebuffer, nullptr);
	for (VkImageView view : vk_swapchain_image_views)
		if (view != VK_NULL_HANDLE) vkDestroyImageView(context.device, view, nullptr);
	vk_imgui_framebuffers.clear();
	vk_framebuffers.clear();
	destroyImageFinishedSemaphores(vk_semaphores_image_finished);
	destroyDepthBuffer();
	if (vk_swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(context.device, vk_swapchain, nullptr);

	vk_swapchain = vkb_swapchain.swapchain;
	vkb_swapchain.swapchain = VK_NULL_HANDLE;
	context.swapchain_format = vkb_swapchain.image_format;
	context.swapchain_extent = vkb_swapchain.extent;
	vk_swapchain_images = std::move(new_images);
	vk_swapchain_image_views = std::move(new_views);
	vk_swapchain_current_image = UINT32_MAX;

	if (!createDepthBuffer() || !createImageFinishedSemaphores(uint32_t(vk_swapchain_images.size()), vk_semaphores_image_finished)) {
		std::cerr << "Failed to recreate Vulkan swapchain resources\n";
		return false;
	}
	const uint32_t count = uint32_t(vk_swapchain_images.size());
	VkImageView attachments[] = {VK_NULL_HANDLE, vk_image_view_depth_buffer};
	const VkFramebufferCreateInfo framebuffer{
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = context.render_pass,
		.attachmentCount = 2, .pAttachments = attachments, .width = context.swapchain_extent.width,
		.height = context.swapchain_extent.height, .layers = 1,
	};
	vk_framebuffers.assign(count, VK_NULL_HANDLE);
	for (uint32_t i = 0; i < count; ++i) {
		attachments[0] = vk_swapchain_image_views[i];
		if (vkCreateFramebuffer(context.device, &framebuffer, nullptr, &vk_framebuffers[i]) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan framebuffer #" << i << '\n';
			return false;
		}
	}
	const VkFramebufferCreateInfo imgui_framebuffer{
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = vk_imgui_render_pass,
		.attachmentCount = 1, .width = context.swapchain_extent.width,
		.height = context.swapchain_extent.height, .layers = 1,
	};
	vk_imgui_framebuffers.assign(count, VK_NULL_HANDLE);
	for (uint32_t i = 0; i < count; ++i) {
		VkFramebufferCreateInfo info = imgui_framebuffer;
		info.pAttachments = &vk_swapchain_image_views[i];
		if (vkCreateFramebuffer(context.device, &info, nullptr, &vk_imgui_framebuffers[i]) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan framebuffer #" << i << " for ImGUI rendering\n";
			return false;
		}
	}
	vk_swapchain_resize_require = false;
	return true;
}

} // namespace

Context context;

std::uint32_t validationErrorCount() { return validation_errors.load(); }

bool initialize(GLFWwindow* const window) {
	// The app links Vulkan::Vulkan; pass its loader entry point directly so
	// vk-bootstrap does not depend on DYLD_LIBRARY_PATH on Homebrew macOS.
	vkb::InstanceBuilder ib(vkGetInstanceProcAddr);
	auto ibr = ib.set_app_name("CG Lab 2 | Camera and Lighting | Variant 11")
	              .require_api_version(VK_MAKE_VERSION(1, 1, 0))
	              .request_validation_layers()
	              .set_debug_callback(validationCallback)
	              .build();
	if (!ibr) {
		std::cerr << "Failed to create Vulkan instance: " << ibr.error().message() << '\n';
		return false;
	}

	auto vkb_instance = std::move(ibr.value());
	vk_instance = vkb_instance.instance;
	vk_debug_messenger = vkb_instance.debug_messenger;
	vk_api_version = vkb_instance.api_version;


	if (glfwCreateWindowSurface(vk_instance, window, nullptr, &vk_surface) != VK_SUCCESS) {
		const char *message = nullptr;
		glfwGetError(&message);
		std::cerr << message << '\n';
		return false;
	}

	vkb::PhysicalDeviceSelector pds(vkb_instance, vk_surface);

	auto pds_result = pds.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
						 .require_present()
						 .select();
	if (!pds_result) {
		std::cerr << pds_result.error().message() << '\n';
		return false;
	}

	auto vkb_physical_device = pds_result.value();

	vk_depth_buffer_format = selectDepthFormat(vkb_physical_device.physical_device);
	if (vk_depth_buffer_format == VK_FORMAT_UNDEFINED) {
		std::cerr << "No supported depth/stencil attachment format found\n";
		return false;
	}

	vkb::DeviceBuilder db(vkb_physical_device);

	auto db_result = db.build();
	if (!db_result) {
		std::cerr << db_result.error().message() << '\n';
		return false;
	}

	auto vkb_device = db_result.value();

	context.physical_device = vkb_device.physical_device;
	context.device = vkb_device.device;

	if (auto result = vkb_device.get_queue(vkb::QueueType::graphics); result) {
		context.graphics_queue = result.value();
	} else {
		std::cerr << result.error().message() << '\n';
		return false;
	}

	if (auto result = vkb_device.get_queue_index(vkb::QueueType::graphics); result) {
		context.graphics_queue_index = result.value();
	} else {
		std::cerr << result.error().message() << '\n';
		return false;
	}

	const VmaAllocatorCreateInfo allocator = {
		.physicalDevice = context.physical_device,
		.device = context.device,
		.instance = vk_instance,
		.vulkanApiVersion = vk_api_version,
	};

	if (vmaCreateAllocator(&allocator, &context.allocator) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan Memory Allocator\n";
		return false;
	}

	vkb::SwapchainBuilder sb(vkb_device);
	VkSurfaceCapabilitiesKHR capabilities{};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physical_device,vk_surface,&capabilities);
	vk_capture_supported = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

	auto sb_result = sb.use_default_format_selection()
					   .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
					   .use_default_image_usage_flags()
                       .add_image_usage_flags(vk_capture_supported ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0)
					   .build();
	if (!sb_result) {
		std::cerr << sb_result.error().message() << '\n';
		return false;
	}

	auto vkb_swapchain = sb_result.value();

	vk_swapchain = vkb_swapchain.swapchain;
	context.swapchain_format = vkb_swapchain.image_format;
	context.swapchain_extent = vkb_swapchain.extent;
	auto images_result = vkb_swapchain.get_images();
	if (!images_result) {
		std::cerr << "Failed to get Vulkan swapchain images: " << images_result.error().message() << '\n';
		return false;
	}
	auto views_result = vkb_swapchain.get_image_views();
	if (!views_result) {
		std::cerr << "Failed to get Vulkan swapchain image views: " << views_result.error().message() << '\n';
		return false;
	}
	vk_swapchain_images = std::move(images_result.value());
	vk_swapchain_image_views = std::move(views_result.value());
	if (vk_swapchain_images.empty() || vk_swapchain_images.size() != vk_swapchain_image_views.size()) {
		std::cerr << "Vulkan swapchain returned no matching images and views\n";
		return false;
	}
	vk_swapchain_current_image = UINT32_MAX;
	vk_swapchain_resize_width = context.swapchain_extent.width;
	vk_swapchain_resize_height = context.swapchain_extent.height;

	const uint32_t swapchain_images_count = uint32_t(vk_swapchain_images.size());

	if (!createDepthBuffer()) return false;

	const VkAttachmentDescription render_pass_attachments[] = {
		{
			.format = context.swapchain_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		},
		{
			.format = vk_depth_buffer_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		},
	};

	const VkAttachmentReference render_pass_color_attachment = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	const VkAttachmentReference render_pass_depth_attachment = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	const VkSubpassDescription render_pass_subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &render_pass_color_attachment,
		.pDepthStencilAttachment = &render_pass_depth_attachment,
	};

	// Acquire waits at color output; explicitly cover both color and depth transitions.
	const VkSubpassDependency dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
	};
	const VkRenderPassCreateInfo render_pass = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = sizeof(render_pass_attachments) / sizeof(render_pass_attachments[0]),
		.pAttachments = render_pass_attachments,
		.subpassCount = 1,
		.pSubpasses = &render_pass_subpass,
		.dependencyCount = 1,
		.pDependencies = &dependency,
	};

	if (vkCreateRenderPass(context.device, &render_pass, nullptr, &context.render_pass) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan render pass\n";
		return false;
	}

	VkImageView framebuffer_attachments[] = {
		VK_NULL_HANDLE,
		vk_image_view_depth_buffer
	};

	const VkFramebufferCreateInfo framebuffer = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = context.render_pass,
		.attachmentCount = sizeof(framebuffer_attachments) / sizeof(framebuffer_attachments[0]),
		.pAttachments = framebuffer_attachments,
		.width = context.swapchain_extent.width,
		.height = context.swapchain_extent.height,
		.layers = 1,
	};

	vk_framebuffers.resize(swapchain_images_count);

	for (uint32_t i = 0; i < swapchain_images_count; ++i) {
		framebuffer_attachments[0] = vk_swapchain_image_views[i];

		if (vkCreateFramebuffer(context.device, &framebuffer, nullptr,
								&vk_framebuffers[i]) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan framebuffer #" << i << '\n';
			return false;
		}
	}

	const VkSemaphoreCreateInfo semaphore = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	const VkFenceCreateInfo fence = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};

	if (!createImageFinishedSemaphores(swapchain_images_count, vk_semaphores_image_finished)) return false;

	if (vkCreateSemaphore(context.device, &semaphore, NULL,
						  &vk_semaphore_image_available) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan semaphore for available image\n";
		return false;
	}

	if (vkCreateFence(context.device, &fence, nullptr, &vk_fence_frame_in_flight) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan semaphore for image in flight\n";
		return false;
	}

	const VkCommandPoolCreateInfo command_pool = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = context.graphics_queue_index,
	};

	if (vkCreateCommandPool(context.device, &command_pool, nullptr, &vk_command_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan command pool\n";
		return false;
	}

	const VkCommandBufferAllocateInfo command_buffers = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vk_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	if (vkAllocateCommandBuffers(context.device, &command_buffers, &vk_command_buffer) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan command buffer\n";
		return false;
	}

	if (!initializeImGUI()) {
		std::cerr << "Failed to initialize ImGUI Vulkan rendering backend\n";
		return false;
	}
	vk_imgui_initialized = true;

	return true;
}

void shutdown() {
	if (context.device != VK_NULL_HANDLE) {
		if (vkQueueWaitIdle(context.graphics_queue) != VK_SUCCESS)
			std::cerr << "Failed to wait for graphics queue during shutdown\n";
	}
	if (vk_imgui_initialized) {
		ImGui_ImplVulkan_Shutdown();
		vk_imgui_initialized = false;
	}
	if (context.device != VK_NULL_HANDLE) {
		if (vk_imgui_command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(context.device, vk_imgui_command_pool, nullptr);
		for (VkFramebuffer framebuffer : vk_imgui_framebuffers)
			if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(context.device, framebuffer, nullptr);
		if (vk_imgui_render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(context.device, vk_imgui_render_pass, nullptr);
		if (vk_imgui_descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(context.device, vk_imgui_descriptor_pool, nullptr);
		if (vk_command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(context.device, vk_command_pool, nullptr);
		if (vk_fence_frame_in_flight != VK_NULL_HANDLE) vkDestroyFence(context.device, vk_fence_frame_in_flight, nullptr);
		destroyImageFinishedSemaphores(vk_semaphores_image_finished);
		if (vk_semaphore_image_available != VK_NULL_HANDLE) vkDestroySemaphore(context.device, vk_semaphore_image_available, nullptr);
		for (VkFramebuffer framebuffer : vk_framebuffers)
			if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(context.device, framebuffer, nullptr);
		if (context.render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(context.device, context.render_pass, nullptr);
		destroyDepthBuffer();
		for (VkImageView view : vk_swapchain_image_views)
			if (view != VK_NULL_HANDLE) vkDestroyImageView(context.device, view, nullptr);
		if (vk_swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(context.device, vk_swapchain, nullptr);
		if (context.allocator != VK_NULL_HANDLE) vmaDestroyAllocator(context.allocator);
		vkDestroyDevice(context.device, nullptr);
	}
	if (vk_surface != VK_NULL_HANDLE && vk_instance != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(vk_instance, vk_surface, nullptr);
	if (vk_debug_messenger != VK_NULL_HANDLE && vk_instance != VK_NULL_HANDLE) {
		vkb::destroy_debug_utils_messenger(vk_instance, vk_debug_messenger);
		vk_debug_messenger = VK_NULL_HANDLE;
	}
	if (vk_instance != VK_NULL_HANDLE) vkDestroyInstance(vk_instance, nullptr);
	context = {};
	vk_instance = VK_NULL_HANDLE;
	vk_surface = VK_NULL_HANDLE;
	vk_swapchain = VK_NULL_HANDLE;
}

void resize(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0) {
		return;
	}

	vk_swapchain_resize_width = width;
	vk_swapchain_resize_height = height;

	vk_swapchain_resize_require = true;
}

FrameData prepare() {
	if (context.device == VK_NULL_HANDLE || vk_swapchain == VK_NULL_HANDLE || vk_framebuffers.empty()) return {};
	if (vkWaitForFences(context.device, 1, &vk_fence_frame_in_flight, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
		std::cerr << "Failed to wait for in-flight Vulkan frame\n";
		vk_fatal_error = true;
		return {};
	}

	for (int attempt = 0; attempt < 2; ++attempt) {
		VkResult result = vkAcquireNextImageKHR(context.device, vk_swapchain, UINT64_MAX,
		                                        vk_semaphore_image_available, VK_NULL_HANDLE,
		                                        &vk_swapchain_current_image);
		if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
			if (result == VK_SUBOPTIMAL_KHR) vk_swapchain_resize_require = true;
			if (vk_swapchain_current_image >= vk_framebuffers.size()) {
				std::cerr << "Vulkan returned an invalid swapchain image index\n";
				vk_fatal_error = true;
				return {};
			}
			if (vkResetFences(context.device, 1, &vk_fence_frame_in_flight) != VK_SUCCESS) {
				vk_fatal_error = true;
				return {};
			}
			return {.framebuffer = vk_framebuffers[vk_swapchain_current_image], .command_buffer = vk_command_buffer};
		}
		if (result == VK_ERROR_OUT_OF_DATE_KHR && rebuildSwapchain(vk_swapchain_resize_width, vk_swapchain_resize_height)) continue;
		std::cerr << "Failed to acquire Vulkan swapchain image: " << result << '\n';
		vk_fatal_error = true;
		return {};
	}
	vk_fatal_error = true;
	return {};
}

void requestScreenshot(const char* path) { capture_path = path; }

void submitAndPresent() {
	if (vk_swapchain_current_image == UINT32_MAX || vk_swapchain_current_image >= vk_semaphores_image_finished.size()) {
		vk_fatal_error = true;
		return;
	}
	drawImGUI();

	const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	const VkCommandBuffer command_buffers[] = {
		vk_command_buffer,
		vk_imgui_command_buffer,
	};

	const VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vk_semaphore_image_available,
		.pWaitDstStageMask = &stage,
		.commandBufferCount = sizeof(command_buffers) / sizeof(command_buffers[0]),
		.pCommandBuffers = command_buffers,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &vk_semaphores_image_finished[vk_swapchain_current_image],
	};

	const VkResult submit_result = vkQueueSubmit(context.graphics_queue, 1, &submit, vk_fence_frame_in_flight);
	if (submit_result != VK_SUCCESS) {
		std::cerr << "Failed to submit Vulkan frame: " << submit_result << '\n';
		vk_fatal_error = true;
		return;
	}

	if (!capture_path.empty()) {
		if (!vk_capture_supported || !saveFrame(vk_swapchain_images[vk_swapchain_current_image],capture_path.c_str())) {
			std::cerr << "Failed to capture rendered frame\n"; vk_fatal_error = true;
		} else std::cout << "Screenshot saved: " << capture_path << '\n';
		capture_path.clear();
	}
	const VkPresentInfoKHR present = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vk_semaphores_image_finished[vk_swapchain_current_image],
		.swapchainCount = 1,
		.pSwapchains = &vk_swapchain,
		.pImageIndices = &vk_swapchain_current_image,
	};

	VkResult result = vkQueuePresentKHR(context.graphics_queue, &present);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || vk_swapchain_resize_require) {
		if (!rebuildSwapchain(vk_swapchain_resize_width, vk_swapchain_resize_height)) vk_fatal_error = true;
	} else if (result != VK_SUCCESS) {
		std::cerr << "Failed to present Vulkan swapchain image: " << result << '\n';
		vk_fatal_error = true;
	}
}

bool hasFatalError() {
	return vk_fatal_error;
}

} // namespace graphics::internal
