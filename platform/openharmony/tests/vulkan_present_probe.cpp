/* Standalone render-pass clear probe: no Godot, shaders, or C# runtime. */
#define VK_USE_PLATFORM_OHOS
#include <hilog/log.h>
#include <native_window/external_window.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#define PROBE_LOG(...) OH_LOG_Print(LOG_APP, LOG_INFO, 0x3200, "VULKAN_PRESENT", __VA_ARGS__)
#define VK_CHECK(call) \
	do { \
		VkResult result = (call); \
		if (result != VK_SUCCESS) { \
			PROBE_LOG("%{public}s failed: %{public}d", #call, result); \
			return; \
		} \
	} while (0)

struct PresentationState {
	VkInstance instance = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkRenderPass render_pass = VK_NULL_HANDLE;
	VkCommandPool pool = VK_NULL_HANDLE;
	VkSemaphore acquired = VK_NULL_HANDLE;
	std::vector<VkSemaphore> rendered;
	std::vector<VkImageView> views;
	std::vector<VkFramebuffer> framebuffers;
	~PresentationState() {
		if (device) {
			vkDeviceWaitIdle(device);
			for (VkFramebuffer framebuffer : framebuffers) {
				vkDestroyFramebuffer(device, framebuffer, nullptr);
			}
			for (VkImageView view : views) {
				vkDestroyImageView(device, view, nullptr);
			}
			if (render_pass) {
				vkDestroyRenderPass(device, render_pass, nullptr);
			}
			if (pool) {
				vkDestroyCommandPool(device, pool, nullptr);
			}
			if (acquired) {
				vkDestroySemaphore(device, acquired, nullptr);
			}
			for (VkSemaphore semaphore : rendered) {
				vkDestroySemaphore(device, semaphore, nullptr);
			}
			if (swapchain) {
				vkDestroySwapchainKHR(device, swapchain, nullptr);
			}
			vkDestroyDevice(device, nullptr);
		}
		if (surface) {
			vkDestroySurfaceKHR(instance, surface, nullptr);
		}
		if (instance) {
			vkDestroyInstance(instance, nullptr);
		}
	}
};

void run_vulkan_present_probe(OHNativeWindow *window, uint32_t width, uint32_t height) {
	PROBE_LOG("START %{public}ux%{public}u", width, height);
	PresentationState state;
	VkApplicationInfo app = {};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "OHOS Vulkan presentation probe";
	app.apiVersion = VK_API_VERSION_1_1;
	const char *extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_OHOS_SURFACE_EXTENSION_NAME };
	VkInstanceCreateInfo instance_info = {};
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &app;
	instance_info.enabledExtensionCount = 2;
	instance_info.ppEnabledExtensionNames = extensions;
	VK_CHECK(vkCreateInstance(&instance_info, nullptr, &state.instance));
	VkSurfaceCreateInfoOHOS surface_info = {};
	surface_info.sType = VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS;
	surface_info.window = window;
	VK_CHECK(vkCreateSurfaceOHOS(state.instance, &surface_info, nullptr, &state.surface));
	uint32_t count = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(state.instance, &count, nullptr));
	if (!count) {
		return;
	}
	std::vector<VkPhysicalDevice> devices(count);
	VK_CHECK(vkEnumeratePhysicalDevices(state.instance, &count, devices.data()));
	VkPhysicalDevice physical = devices[0];
	vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
	std::vector<VkQueueFamilyProperties> families(count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
	uint32_t family = UINT32_MAX;
	for (uint32_t i = 0; i < count; ++i) {
		VkBool32 present = VK_FALSE;
		VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, state.surface, &present));
		if (present && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
			family = i;
			break;
		}
	}
	if (family == UINT32_MAX) {
		PROBE_LOG("No graphics/present queue");
		return;
	}
	float priority = 1;
	VkDeviceQueueCreateInfo queue_info = {};
	queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info.queueFamilyIndex = family;
	queue_info.queueCount = 1;
	queue_info.pQueuePriorities = &priority;
	const char *swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	VkDeviceCreateInfo device_info = {};
	device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue_info;
	device_info.enabledExtensionCount = 1;
	device_info.ppEnabledExtensionNames = &swapchain_extension;
	VK_CHECK(vkCreateDevice(physical, &device_info, nullptr, &state.device));
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(state.device, family, 0, &queue);
	VkSurfaceCapabilitiesKHR caps = {};
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, state.surface, &caps));
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, state.surface, &count, nullptr));
	if (!count || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
		return;
	}
	std::vector<VkSurfaceFormatKHR> formats(count);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, state.surface, &count, formats.data()));
	VkSurfaceFormatKHR format = formats[0];
	if (format.format == VK_FORMAT_UNDEFINED) {
		format.format = VK_FORMAT_B8G8R8A8_UNORM;
	}
	VkExtent2D extent = caps.currentExtent;
	if (extent.width == UINT32_MAX) {
		extent.width = std::max(caps.minImageExtent.width, std::min(width, caps.maxImageExtent.width));
		extent.height = std::max(caps.minImageExtent.height, std::min(height, caps.maxImageExtent.height));
	}
	VkSwapchainCreateInfoKHR swap_info = {};
	swap_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swap_info.surface = state.surface;
	swap_info.minImageCount = caps.minImageCount + 1;
	if (caps.maxImageCount) {
		swap_info.minImageCount = std::min(swap_info.minImageCount, caps.maxImageCount);
	}
	swap_info.imageFormat = format.format;
	swap_info.imageColorSpace = format.colorSpace;
	swap_info.imageExtent = extent;
	swap_info.imageArrayLayers = 1;
	swap_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swap_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swap_info.preTransform = caps.currentTransform;
	swap_info.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1));
	swap_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	swap_info.clipped = VK_TRUE;
	VK_CHECK(vkCreateSwapchainKHR(state.device, &swap_info, nullptr, &state.swapchain));
	VK_CHECK(vkGetSwapchainImagesKHR(state.device, state.swapchain, &count, nullptr));
	std::vector<VkImage> images(count);
	VK_CHECK(vkGetSwapchainImagesKHR(state.device, state.swapchain, &count, images.data()));
	PROBE_LOG("SWAPCHAIN %{public}ux%{public}u images=%{public}u format=%{public}d", extent.width, extent.height, count, format.format);
	VkAttachmentDescription attachment = {};
	attachment.format = format.format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	VkAttachmentReference reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &reference;
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	VkRenderPassCreateInfo pass_info = {};
	pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	pass_info.attachmentCount = 1;
	pass_info.pAttachments = &attachment;
	pass_info.subpassCount = 1;
	pass_info.pSubpasses = &subpass;
	pass_info.dependencyCount = 1;
	pass_info.pDependencies = &dependency;
	VK_CHECK(vkCreateRenderPass(state.device, &pass_info, nullptr, &state.render_pass));
	for (VkImage image : images) {
		VkImageViewCreateInfo view_info = {};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image = image;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = format.format;
		view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VkImageView view = VK_NULL_HANDLE;
		VK_CHECK(vkCreateImageView(state.device, &view_info, nullptr, &view));
		state.views.push_back(view);
		VkFramebufferCreateInfo framebuffer_info = {};
		framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_info.renderPass = state.render_pass;
		framebuffer_info.attachmentCount = 1;
		framebuffer_info.pAttachments = &view;
		framebuffer_info.width = extent.width;
		framebuffer_info.height = extent.height;
		framebuffer_info.layers = 1;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		VK_CHECK(vkCreateFramebuffer(state.device, &framebuffer_info, nullptr, &framebuffer));
		state.framebuffers.push_back(framebuffer);
	}
	VkCommandPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = family;
	VK_CHECK(vkCreateCommandPool(state.device, &pool_info, nullptr, &state.pool));
	VkCommandBufferAllocateInfo command_info = {};
	command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_info.commandPool = state.pool;
	command_info.commandBufferCount = 1;
	command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	VkCommandBuffer command = VK_NULL_HANDLE;
	VK_CHECK(vkAllocateCommandBuffers(state.device, &command_info, &command));
	VkSemaphoreCreateInfo semaphore_info = {};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VK_CHECK(vkCreateSemaphore(state.device, &semaphore_info, nullptr, &state.acquired));
	for (size_t i = 0; i < images.size(); ++i) {
		VkSemaphore semaphore = VK_NULL_HANDLE;
		VK_CHECK(vkCreateSemaphore(state.device, &semaphore_info, nullptr, &semaphore));
		state.rendered.push_back(semaphore);
	}
	for (unsigned frame = 0; frame < 600; ++frame) {
		uint32_t index = 0;
		VK_CHECK(vkAcquireNextImageKHR(state.device, state.swapchain, 10000000000ull, state.acquired, VK_NULL_HANDLE, &index));
		VK_CHECK(vkResetCommandBuffer(command, 0));
		VkCommandBufferBeginInfo begin = {};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		VK_CHECK(vkBeginCommandBuffer(command, &begin));
		VkClearValue clear = {};
		clear.color.float32[frame < 300 ? 1 : 2] = 1;
		clear.color.float32[3] = 1;
		VkRenderPassBeginInfo pass = {};
		pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		pass.renderPass = state.render_pass;
		pass.framebuffer = state.framebuffers[index];
		pass.renderArea.extent = extent;
		pass.clearValueCount = 1;
		pass.pClearValues = &clear;
		vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdEndRenderPass(command);
		VK_CHECK(vkEndCommandBuffer(command));
		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submit = {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &state.acquired;
		submit.pWaitDstStageMask = &wait_stage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &command;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &state.rendered[index];
		VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
		VkPresentInfoKHR present = {};
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &state.rendered[index];
		present.swapchainCount = 1;
		present.pSwapchains = &state.swapchain;
		present.pImageIndices = &index;
		VK_CHECK(vkQueuePresentKHR(queue, &present));
		VK_CHECK(vkQueueWaitIdle(queue));
		if (frame % 100 == 0) {
			PROBE_LOG("PRESENT frame=%{public}u expected=%{public}s", frame, frame < 300 ? "green" : "blue");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
	PROBE_LOG("PRESENT COMPLETE");
}
