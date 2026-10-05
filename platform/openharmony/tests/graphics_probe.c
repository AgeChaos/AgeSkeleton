#include <EGL/egl.h>
#include <stdio.h>
#include <vulkan/vulkan.h>

int main(void) {
	uint32_t version = VK_API_VERSION_1_0;
	PFN_vkEnumerateInstanceVersion enumerate_version = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
	if (enumerate_version) {
		enumerate_version(&version);
	}
	printf("Loader API: %u.%u.%u\n", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
	for (unsigned minor = 0; minor <= 3; minor++) {
		VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "OHOS graphics probe", .apiVersion = VK_MAKE_VERSION(1, minor, 0) };
		VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
		VkInstance instance = VK_NULL_HANDLE;
		VkResult result = vkCreateInstance(&info, NULL, &instance);
		printf("Vulkan 1.%u create: %d\n", minor, result);
		if (result == VK_SUCCESS) {
			uint32_t count = 0;
			result = vkEnumeratePhysicalDevices(instance, &count, NULL);
			printf("  devices: %u result: %d\n", count, result);
			vkDestroyInstance(instance, NULL);
		}
	}
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major = 0, minor = 0;
	EGLBoolean initialized = eglInitialize(display, &major, &minor);
	printf("EGL initialize: %u error: 0x%x version: %d.%d\n", initialized, eglGetError(), major, minor);
	if (initialized) {
		printf("EGL vendor: %s\n", eglQueryString(display, EGL_VENDOR));
		eglTerminate(display);
	}
	return 0;
}
