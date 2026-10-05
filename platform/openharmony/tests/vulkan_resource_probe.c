/* Standalone SDK diagnostic. No Godot, C#, window, or engine workarounds. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef VULKAN_PROBE_VOLK
#include <volk.h>
#else
#include <vulkan/vulkan.h>
#endif

#ifdef VULKAN_PROBE_HAP
#include <hilog/log.h>
#include <stdarg.h>
static int probe_printf(const char *format, ...) {
	char message[2048];
	va_list args;
	va_start(args, format);
	int result = vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	OH_LOG_Print(LOG_APP, LOG_INFO, 0x3200, "VULKAN_PROBE", "%{public}s", message);
	return result;
}
#define printf probe_printf
#define main vulkan_resource_probe_main
#endif

#define CHECK(call) \
	do { \
		VkResult result = (call); \
		printf("%s: %d\n", #call, result); \
		if (result != VK_SUCCESS) { \
			exit(1); \
		} \
	} while (0)

static uint32_t memory_type(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags required) {
	VkPhysicalDeviceMemoryProperties properties = { 0 };
	vkGetPhysicalDeviceMemoryProperties(physical, &properties);
	for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
		if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & required) == required) {
			return i;
		}
	}
	fprintf(stderr, "No suitable memory type, bits=%x required=%x\n", bits, required);
	exit(1);
}

static void clear_and_readback(VkPhysicalDevice physical, VkDevice device, uint32_t family, int persistent, int dedicated) {
	VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = { 16, 16, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkImage image = VK_NULL_HANDLE;
	CHECK(vkCreateImage(device, &image_info, NULL, &image));
	VkMemoryRequirements image_requirements = { 0 };
	vkGetImageMemoryRequirements(device, image, &image_requirements);
	printf("Image requirements size=%llu alignment=%llu types=%x\n",
			(unsigned long long)image_requirements.size, (unsigned long long)image_requirements.alignment, image_requirements.memoryTypeBits);
	VkMemoryDedicatedAllocateInfo dedicated_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
		.image = image,
	};
	VkMemoryAllocateInfo image_alloc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = dedicated ? &dedicated_info : NULL,
		.allocationSize = image_requirements.size,
		.memoryTypeIndex = memory_type(physical, image_requirements.memoryTypeBits, 0),
	};
	VkDeviceMemory image_memory = VK_NULL_HANDLE;
	CHECK(vkAllocateMemory(device, &image_alloc, NULL, &image_memory));
	CHECK(vkBindImageMemory(device, image, image_memory, 0));

	VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = 16 * 16 * 4,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkBuffer buffer = VK_NULL_HANDLE;
	CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
	VkMemoryRequirements buffer_requirements = { 0 };
	vkGetBufferMemoryRequirements(device, buffer, &buffer_requirements);
	VkMemoryAllocateInfo buffer_alloc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = buffer_requirements.size,
		.memoryTypeIndex = memory_type(physical, buffer_requirements.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
	};
	VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
	CHECK(vkAllocateMemory(device, &buffer_alloc, NULL, &buffer_memory));
	CHECK(vkBindBufferMemory(device, buffer, buffer_memory, 0));
	void *mapped = NULL;
	if (persistent) {
		CHECK(vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
		memset(mapped, 0, 16 * 16 * 4);
	}

	VkCommandPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family };
	VkCommandPool pool = VK_NULL_HANDLE;
	CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
	VkCommandBufferAllocateInfo command_alloc = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer command = VK_NULL_HANDLE;
	CHECK(vkAllocateCommandBuffers(device, &command_alloc, &command));
	VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	CHECK(vkBeginCommandBuffer(command, &begin));
	VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = range,
	};
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
	VkClearColorValue color = { .float32 = { 0, 1, 0, 1 } };
	vkCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
	VkBufferImageCopy copy = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { 16, 16, 1 } };
	vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
	VkMemoryBarrier host_barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_HOST_READ_BIT,
	};
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_barrier, 0, NULL, 0, NULL);
	CHECK(vkEndCommandBuffer(command));
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, family, 0, &queue);
	VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fence = VK_NULL_HANDLE;
	CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
	VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command };
	CHECK(vkQueueSubmit(queue, 1, &submit, fence));
	CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull));
	if (!persistent) {
		CHECK(vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
	}
	const uint8_t *pixels = mapped;
	unsigned failures = 0;
	for (unsigned i = 0; i < 16 * 16; ++i) {
		if (pixels[i * 4] != 0 || pixels[i * 4 + 1] != 255 || pixels[i * 4 + 2] != 0 || pixels[i * 4 + 3] != 255) {
			++failures;
		}
	}
	printf("READBACK first=%u,%u,%u,%u mismatched_pixels=%u/256\n", pixels[0], pixels[1], pixels[2], pixels[3], failures);
	vkUnmapMemory(device, buffer_memory);
	vkDestroyFence(device, fence, NULL);
	vkDestroyCommandPool(device, pool, NULL);
	vkDestroyBuffer(device, buffer, NULL);
	vkFreeMemory(device, buffer_memory, NULL);
	vkDestroyImage(device, image, NULL);
	vkFreeMemory(device, image_memory, NULL);
	if (failures) {
		exit(2);
	}
}

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, 0);
#ifdef VULKAN_PROBE_VOLK
	CHECK(volkInitialize());
	printf("DISPATCH Volk instance/device entry points\n");
#endif
	VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "OHOS Vulkan resource probe", .apiVersion = VK_API_VERSION_1_1 };
	VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
	VkInstance instance = VK_NULL_HANDLE;
	CHECK(vkCreateInstance(&info, NULL, &instance));
#ifdef VULKAN_PROBE_VOLK
	volkLoadInstance(instance);
#endif
	uint32_t count = 0;
	CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
	if (!count) {
		return 1;
	}
	VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
	if (!devices) {
		return 1;
	}
	CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
	VkPhysicalDevice physical = devices[0];
	free(devices);
	VkPhysicalDeviceProperties properties = { 0 };
	vkGetPhysicalDeviceProperties(physical, &properties);
	printf("DEVICE %s API %u.%u.%u\n", properties.deviceName, VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
	const VkFormat formats[] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_R8_UINT, VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT };
	for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
		VkFormatProperties legacy = { 0 };
		vkGetPhysicalDeviceFormatProperties(physical, formats[i], &legacy);
		VkFormatProperties2 modern = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
		vkGetPhysicalDeviceFormatProperties2(physical, formats[i], &modern);
		VkImageFormatProperties image = { 0 };
		VkResult result = vkGetPhysicalDeviceImageFormatProperties(physical, formats[i], VK_IMAGE_TYPE_2D,
				VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &image);
		printf("FORMAT %d legacy=%x,%x,%x modern=%x,%x,%x sampled_image_result=%d max=%ux%u\n", formats[i],
				legacy.linearTilingFeatures, legacy.optimalTilingFeatures, legacy.bufferFeatures,
				modern.formatProperties.linearTilingFeatures, modern.formatProperties.optimalTilingFeatures, modern.formatProperties.bufferFeatures,
				result, image.maxExtent.width, image.maxExtent.height);
	}
	if (argc > 1 && (strcmp(argv[1], "--image") == 0 || strcmp(argv[1], "--image-persistent") == 0 || strcmp(argv[1], "--image-dedicated") == 0)) {
		uint32_t family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
		VkQueueFamilyProperties *families = calloc(family_count, sizeof(*families));
		if (!families) {
			return 1;
		}
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families);
		uint32_t family = 0;
		while (family < family_count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
			++family;
		}
		free(families);
		if (family == family_count) {
			return 1;
		}
		float priority = 1.0f;
		VkDeviceQueueCreateInfo queue_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
		VkDeviceCreateInfo device_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info };
		VkDevice device = VK_NULL_HANDLE;
		CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
#ifdef VULKAN_PROBE_VOLK
		volkLoadDevice(device);
#endif
		clear_and_readback(physical, device, family, strcmp(argv[1], "--image-persistent") == 0, strcmp(argv[1], "--image-dedicated") == 0);
		vkDestroyDevice(device, NULL);
	}
	vkDestroyInstance(instance, NULL);
	printf("PROBE COMPLETE\n");
	return 0;
}
