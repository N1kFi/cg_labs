#pragma once
#include "graphics_internal.hpp"
#include <fstream>

namespace graphics::internal {
// Read back the actual rendered swapchain image, including ImGui, for reports.
inline bool saveFrame(VkImage image, const char* path) {
    const bool bgra = context.swapchain_format == VK_FORMAT_B8G8R8A8_SRGB || context.swapchain_format == VK_FORMAT_B8G8R8A8_UNORM;
    const bool rgba = context.swapchain_format == VK_FORMAT_R8G8B8A8_SRGB || context.swapchain_format == VK_FORMAT_R8G8B8A8_UNORM;
    if (!bgra && !rgba) return false;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    const auto cleanup = [&] {
        if (pool) vkDestroyCommandPool(context.device,pool,nullptr);
        if (buffer) vmaDestroyBuffer(context.allocator,buffer,allocation);
    };
    const VkDeviceSize bytes = VkDeviceSize(context.swapchain_extent.width)*context.swapchain_extent.height*4;
    const VkBufferCreateInfo bufferInfo{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,
        .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    const VmaAllocationCreateInfo allocInfo{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
    VmaAllocationInfo host{};
    if (vmaCreateBuffer(context.allocator,&bufferInfo,&allocInfo,&buffer,&allocation,&host)!=VK_SUCCESS) return false;
    const VkCommandPoolCreateInfo poolInfo{.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,.queueFamilyIndex=context.graphics_queue_index};
    if (vkCreateCommandPool(context.device,&poolInfo,nullptr,&pool)!=VK_SUCCESS) { cleanup(); return false; }
    VkCommandBuffer command;
    const VkCommandBufferAllocateInfo commandInfo{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    if (vkAllocateCommandBuffers(context.device,&commandInfo,&command)!=VK_SUCCESS) { cleanup(); return false; }
    const VkCommandBufferBeginInfo begin{.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vkBeginCommandBuffer(command,&begin)!=VK_SUCCESS) { cleanup(); return false; }
    VkImageMemoryBarrier barrier{.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,0,nullptr,0,nullptr,1,&barrier);
    const VkBufferImageCopy copy{.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
        .imageExtent={context.swapchain_extent.width,context.swapchain_extent.height,1}};
    vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask=0;
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    const VkBufferMemoryBarrier hostBarrier{.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .buffer=buffer,.offset=0,.size=bytes};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&hostBarrier,0,nullptr);
    if (vkEndCommandBuffer(command)!=VK_SUCCESS) { cleanup(); return false; }
    const VkSubmitInfo submit{.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
    if (vkQueueSubmit(context.graphics_queue,1,&submit,VK_NULL_HANDLE)!=VK_SUCCESS) { cleanup(); return false; }
    if (vkQueueWaitIdle(context.graphics_queue)!=VK_SUCCESS) { cleanup(); return false; }
    vmaInvalidateAllocation(context.allocator,allocation,0,bytes);
    std::ofstream output(path,std::ios::binary);
    output << "P6\n" << context.swapchain_extent.width << ' ' << context.swapchain_extent.height << "\n255\n";
    const auto* pixels=static_cast<const unsigned char*>(host.pMappedData);
    if (!pixels) { cleanup(); return false; }
    for (VkDeviceSize i=0;i<bytes;i+=4) {
        const char rgb[]={static_cast<char>(pixels[i+(bgra?2:0)]),static_cast<char>(pixels[i+1]),static_cast<char>(pixels[i+(bgra?0:2)])};
        output.write(rgb,3);
    }
    const bool ok=output.good(); cleanup(); return ok;
}
} // namespace graphics::internal
