#include "RendererPch.h"
#include "VkBuildHelpers.h"

#if defined(_WIN32) || defined(WIN32)
	#define NOMINMAX
	#include <Windows.h>
	#include <vulkan/vulkan_win32.h>
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	#include <vulkan/vulkan_android.h>
#elif defined(__linux__)
	#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
		#include <vulkan/vulkan_wayland.h>
	#else
		#include <vulkan/vulkan_xlib.h>
	#endif
#elif defined(__APPLE__)
	#include <vulkan/vulkan_macos.h>
#endif

namespace vk
{

//-------------------------------------------------------------------------
// Instance
//-------------------------------------------------------------------------
InstanceBuilder::InstanceBuilder()
{
	// **
	// Set default values
	// **
	m_ValidationLayers = { "VK_LAYER_KHRONOS_validation" };
	m_ExtensionLayers = 
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#if defined(_WIN32)
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
		VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#elif defined(__linux__)
	#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
		VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
	#else
		VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
	#endif
#elif defined(__APPLE__)
	VK_EXT_LAYER_SETTINGS_EXTENSION_NAME,
	VK_MVK_MACOS_SURFACE_EXTENSION_NAME,
#endif
#ifdef _DEBUG
		"VK_EXT_debug_report",
#endif
	};

	m_AppInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	m_AppInfo.pApplicationName   = "VkRenderer";
	m_AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	m_AppInfo.pEngineName        = "VkRenderer";
	m_AppInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
	m_AppInfo.apiVersion         = VK_API_VERSION_1_3;
}

InstanceBuilder& InstanceBuilder::AddValidationLayer(const char* layerString)
{
	m_ValidationLayers.push_back(layerString);
	return *this;
}

InstanceBuilder& InstanceBuilder::AddExtensionLayer(const char* layerString)
{
	m_ExtensionLayers.push_back(layerString);
	return *this;
}

InstanceBuilder& InstanceBuilder::SetApiVersion(u32 version)
{
	m_AppInfo.apiVersion = version;
	return *this;
}

InstanceBuilder& InstanceBuilder::SetValidationFeatureEnable(const std::vector< VkValidationFeatureEnableEXT >& features)
{
	VkValidationFeaturesEXT validationFeatures        = {};
	validationFeatures.sType                          = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
	validationFeatures.enabledValidationFeatureCount  = static_cast<u32>(features.size());
	validationFeatures.pEnabledValidationFeatures     = features.data();
	validationFeatures.disabledValidationFeatureCount = 0;
	validationFeatures.pDisabledValidationFeatures    = nullptr;

	m_ValidationFeatures = validationFeatures;
	return *this;
}

VkInstance InstanceBuilder::Build()
{
	VkInstance instance = VK_NULL_HANDLE;

	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pNext                   = m_ValidationFeatures.enabledValidationFeatureCount > 0 ? &m_ValidationFeatures.enabledValidationFeatureCount : nullptr;
	instanceInfo.flags                   = 0; //VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	instanceInfo.pApplicationInfo        = &m_AppInfo;
	instanceInfo.enabledExtensionCount   = static_cast<u32>(m_ExtensionLayers.size());
	instanceInfo.ppEnabledExtensionNames = m_ExtensionLayers.data();
#ifdef _DEBUG
	instanceInfo.enabledLayerCount   = static_cast<u32>(m_ValidationLayers.size());
	instanceInfo.ppEnabledLayerNames = m_ValidationLayers.data();
#endif

	VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &instance));
	return instance;
}


//-------------------------------------------------------------------------
// DebugMessenger
//-------------------------------------------------------------------------
DebugMessengerBuilder::DebugMessengerBuilder()
{
	// **
	// Set default values
	// **
	m_DebugMessengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
}

DebugMessengerBuilder& DebugMessengerBuilder::SetDebugMessageCallback(std::function<
	VkBool32(VkDebugUtilsMessageSeverityFlagBitsEXT Severity,
		VkDebugUtilsMessageTypeFlagsEXT Type,
		const VkDebugUtilsMessengerCallbackDataEXT* CallbackData, void* UserData) > callback)
{
	using TCallback = VkBool32(*)(
		VkDebugUtilsMessageSeverityFlagBitsEXT,
		VkDebugUtilsMessageTypeFlagsEXT,
		const VkDebugUtilsMessengerCallbackDataEXT*, 
		void*);

	m_DebugMessengerInfo.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
	m_DebugMessengerInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	m_DebugMessengerInfo.pfnUserCallback = *callback.target< TCallback >();

	return *this;
}

VkDebugUtilsMessengerEXT DebugMessengerBuilder::Build(VkInstance instance)
{
	VkDebugUtilsMessengerEXT outDebugMessenger = VK_NULL_HANDLE;

	auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &m_DebugMessengerInfo, nullptr, &outDebugMessenger));
	return outDebugMessenger;
}


//-------------------------------------------------------------------------
// Device
//-------------------------------------------------------------------------
DeviceBuilder::DeviceBuilder()
{
	// **
	// Set default values
	// **
	physicalDeviceFeatures2.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	physicalDevice11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	physicalDevice12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	physicalDevice13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

	m_PhysicalRequirements.apiVersion  = VK_API_VERSION_1_3;
	m_PhysicalRequirements.deviceType  = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	m_PhysicalRequirements.featureBits = 0LL;

	m_LogicalDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	m_LogicalDeviceExtensions.push_back(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
}

DeviceBuilder& DeviceBuilder::SetMinimumApiVersion(u32 version)
{
	m_PhysicalRequirements.apiVersion = version;
	return *this;
}

DeviceBuilder& DeviceBuilder::SetPhysicalDeviceType(VkPhysicalDeviceType type)
{
	m_PhysicalRequirements.deviceType = type;
	return *this;
}

DeviceBuilder& DeviceBuilder::AddPhysicalDeviceFeature(u8 featureBit)
{
	assert(featureBit != ePhysicalDeviceFeature_Core_1_X && featureBit != ePhysicalDeviceFeature_Extension);

	m_PhysicalRequirements.featureBits |= 1LL << featureBit;
	return *this;
}

DeviceBuilder& DeviceBuilder::AddDeviceExtension(const char* extension)
{
	m_LogicalDeviceExtensions.push_back(extension);
	return *this;
}

VkDevice DeviceBuilder::Build(VkInstance instance)
{
	VkDevice outDevice = VK_NULL_HANDLE;

	// **
	// Physical device
	// **
	VK_CHECK(queryPhysicalDevice(instance));


	// **
	// Queue
	// **
	queueFamilyIndices = getQueueFamilyIndex();
	assert(queueFamilyIndices.graphicsQueueIndex != UINT_MAX && queueFamilyIndices.computeQueueIndex != UINT_MAX && queueFamilyIndices.transferQueueIndex != UINT_MAX);
	BB_ASSERT(queueFamilyIndices.transferQueueIndex != queueFamilyIndices.graphicsQueueIndex && queueFamilyIndices.transferQueueIndex != queueFamilyIndices.computeQueueIndex, "Transfer queue should be stand-alone!");

	constexpr float queuePriority = 1.0f;
	std::vector< VkDeviceQueueCreateInfo > queueInfos;

	VkDeviceQueueCreateInfo graphicsQueueInfo = {};
	graphicsQueueInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	graphicsQueueInfo.queueFamilyIndex        = queueFamilyIndices.graphicsQueueIndex;
	graphicsQueueInfo.queueCount              = 1;
	graphicsQueueInfo.pQueuePriorities        = &queuePriority;

	queueInfos.push_back(graphicsQueueInfo);

	if (queueFamilyIndices.graphicsQueueIndex != queueFamilyIndices.computeQueueIndex) 
	{
		VkDeviceQueueCreateInfo computeQueueInfo = {};
		computeQueueInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		computeQueueInfo.queueFamilyIndex        = queueFamilyIndices.computeQueueIndex;
		computeQueueInfo.queueCount              = 1;
		computeQueueInfo.pQueuePriorities        = &queuePriority;

		queueInfos.push_back(computeQueueInfo);
	}

	VkDeviceQueueCreateInfo transferQueueInfo = {};
	transferQueueInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	transferQueueInfo.queueFamilyIndex        = queueFamilyIndices.transferQueueIndex;
	transferQueueInfo.queueCount              = 1;
	transferQueueInfo.pQueuePriorities        = &queuePriority;

	queueInfos.push_back(transferQueueInfo);


	// **
	// Core features
	// **
	FeatureChain< 32 > featureChain = {};
	if (m_PhysicalRequirements.featureBits << (ePhysicalDeviceFeature_Extension - 1) != 0)
	{
		if (m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_IndirectRendering))
		{
			physicalDevice11Features.shaderDrawParameters                          = VK_TRUE;
			physicalDevice12Features.drawIndirectCount                             = VK_TRUE;
			physicalDevice12Features.runtimeDescriptorArray                        = VK_TRUE;
			physicalDeviceFeatures.multiDrawIndirect                               = VK_TRUE;
			physicalDeviceFeatures.drawIndirectFirstInstance                       = VK_TRUE;
			physicalDevice12Features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
			physicalDevice12Features.descriptorBindingSampledImageUpdateAfterBind  = VK_TRUE;
			physicalDevice12Features.descriptorBindingPartiallyBound               = VK_TRUE;
		}

		if (m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_DescriptorIndexing))
		{
			physicalDevice12Features.descriptorIndexing = VK_TRUE;
		}

		if (m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_DynamicIndexing))
		{
			physicalDevice12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
			physicalDevice12Features.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
		}

		if (m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_SamplerAnistropy))
		{
			physicalDeviceFeatures.samplerAnisotropy = VK_TRUE;
		}

		if (m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_DeviceAddress))
		{
			physicalDevice12Features.bufferDeviceAddress = VK_TRUE;
		}

		if (m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_DynamicRendering))
		{
			physicalDevice13Features.dynamicRendering = VK_TRUE;
		}

		if (m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_Sync2))
		{
			physicalDevice13Features.synchronization2 = VK_TRUE;
		}

		physicalDeviceFeatures2.features = physicalDeviceFeatures;

		featureChain.bind(physicalDeviceFeatures2);
		featureChain.bind(physicalDevice11Features);
		featureChain.bind(physicalDevice12Features);
		featureChain.bind(physicalDevice13Features);
	}


	// **
	// Extension features
	// **
	if (m_PhysicalRequirements.featureBits >> ePhysicalDeviceFeature_Extension)
	{
		if (featureChain.head == nullptr)
			featureChain.bind(physicalDeviceFeatures2);

		if (m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_IndexTypeUint8))
		{
			VkPhysicalDeviceFeatures2 features2 = {};
			features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

			VkPhysicalDeviceIndexTypeUint8FeaturesEXT IndexU8Features = {};
			IndexU8Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT;

			features2.pNext = &IndexU8Features;
			vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

			if (IndexU8Features.indexTypeUint8)
				featureChain.bind(IndexU8Features);
		}

		if (m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_SwapChainMaintenance))
		{
			VkPhysicalDeviceFeatures2 features2 = {};
			features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

			VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapChainMaintenance1Features = {};
			swapChainMaintenance1Features.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;

			features2.pNext = &swapChainMaintenance1Features;
			vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

			if (swapChainMaintenance1Features.swapchainMaintenance1)
				featureChain.bind(swapChainMaintenance1Features);
		}

		// More extension features ...
	}


	// **
	// Logical device
	// **
	VkDeviceCreateInfo deviceInfo      = {};
	deviceInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.pNext                   = featureChain.head;
	deviceInfo.queueCreateInfoCount    = static_cast<u32>(queueInfos.size());
	deviceInfo.pQueueCreateInfos       = queueInfos.data();
	deviceInfo.enabledExtensionCount   = static_cast<u32>(m_LogicalDeviceExtensions.size());
	deviceInfo.ppEnabledExtensionNames = m_LogicalDeviceExtensions.data();
	VK_CHECK(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &outDevice));

	return outDevice;
}

VkResult DeviceBuilder::queryPhysicalDevice(VkInstance instance)
{
	u32 gpuCount = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
	assert(gpuCount > 0);

	std::vector< VkPhysicalDevice > gpuCandidates(gpuCount);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &gpuCount, gpuCandidates.data()));

	const bool bAdditionalShader = m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_AdditionalShader);
	const bool bMultiViewport = m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_MultiViewport);
	const bool bDynamicIndexing = m_PhysicalRequirements.featureBits & (1LL << ePhysicalDeviceFeature_DynamicIndexing);

	for (const auto& gpu : gpuCandidates)
	{
		VkPhysicalDeviceProperties gpuProperties;
		vkGetPhysicalDeviceProperties(gpu, &gpuProperties);

		VkPhysicalDeviceFeatures gpuFeatures;
		vkGetPhysicalDeviceFeatures(gpu, &gpuFeatures);

		if (gpuProperties.apiVersion < m_PhysicalRequirements.apiVersion)
			continue;

		if (bAdditionalShader && !(gpuFeatures.geometryShader || gpuFeatures.tessellationShader))
			continue;

		if (bMultiViewport && !(gpuFeatures.multiViewport))
			continue;

		if (bDynamicIndexing && !(gpuFeatures.shaderUniformBufferArrayDynamicIndexing ||
			gpuFeatures.shaderSampledImageArrayDynamicIndexing ||
			gpuFeatures.shaderStorageBufferArrayDynamicIndexing ||
			gpuFeatures.shaderStorageImageArrayDynamicIndexing))
			continue;

		if (gpuProperties.deviceType == m_PhysicalRequirements.deviceType) 
		{
			physicalDevice = gpu;
			physicalDeviceProperties = gpuProperties;

			return VK_SUCCESS;
		}
	}

	return VK_ERROR_INITIALIZATION_FAILED;
}

DeviceBuilder::QueueFamilyIndex DeviceBuilder::getQueueFamilyIndex() const
{
	DeviceBuilder::QueueFamilyIndex queueIndices;

	u32 familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);

	std::vector< VkQueueFamilyProperties > families(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

	// VK_QUEUE_GRAPHICS_BIT :			Support graphics op
	// VK_QUEUE_COMPUTE_BIT:			Support compute op
	// VK_QUEUE_TRANSFER_BIT :			Support transfer op
	// VK_QUEUE_SPARSE_BINDING_BIT :	Support sparse resource(or virtual texture) op

	// QueueFamily : A grouping of queues with the same bit flag configuration
	// Each QueueFamily has queueCount. These queues can be mapped 1:1 to threads, and the management is up to the application
	for (u32 i = 0; i != families.size(); i++)
	{
		if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			queueIndices.graphicsQueueIndex = i;
		}

		// Find first a queue that has just the transfer bit set
		if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 &&
			(families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0 &&
			(families[i].queueFlags & VK_QUEUE_TRANSFER_BIT))
		{
			queueIndices.transferQueueIndex = i;
		}

		// Find first a queue that has just the compute bit set
		if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 &&
			(families[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
		{
			queueIndices.computeQueueIndex = i;
		}

		// Get queue that is shared 
		if (queueIndices.computeQueueIndex == UINT_MAX && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
		{
			queueIndices.computeQueueIndex = i;
		}

		// Get queue that is shared 
		if (queueIndices.transferQueueIndex == UINT_MAX && (families[i].queueFlags & VK_QUEUE_TRANSFER_BIT))
		{
			queueIndices.transferQueueIndex = i;
		}
	}

	return queueIndices;
}


//-------------------------------------------------------------------------
// Swap chain
//-------------------------------------------------------------------------
SwapChainBuilder::SwapChainBuilder(VkSurfaceKHR surface, VkPhysicalDevice physicalDevice)
	:m_vkSurface(surface), m_vkPhysicalDevice(physicalDevice)
{
	// **
	// Set default values
	// **
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vkPhysicalDevice, m_vkSurface, &capabilities));

	u32 formatCount;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_vkPhysicalDevice, m_vkSurface, &formatCount, NULL));
	assert(formatCount > 0);

	m_SurfaceFormats.resize(formatCount);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_vkPhysicalDevice, m_vkSurface, &formatCount, m_SurfaceFormats.data()));

	selectedSurface = m_SurfaceFormats.front();
	m_ImageUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	m_Extent = { capabilities.currentExtent.width, capabilities.currentExtent.height };
}

SwapChainBuilder& SwapChainBuilder::SetDesiredImageFormat(VkFormat format)
{
	const auto it = std::ranges::find_if(m_SurfaceFormats.begin(), m_SurfaceFormats.end(), [format](const VkSurfaceFormatKHR& surfaceFormat)
		{
			return surfaceFormat.format == format;
		});

	if (it != m_SurfaceFormats.end())
		selectedSurface = *it;

	return *this;
}

SwapChainBuilder& SwapChainBuilder::SetDesiredImageResolution(u32 width, u32 height)
{
	assert(width > 0 && height > 0);
	m_Extent.width = std::min(capabilities.maxImageExtent.width, width);
	m_Extent.height = std::min(capabilities.maxImageExtent.height, height);

	return *this;
}

SwapChainBuilder& SwapChainBuilder::SetDesiredImageCount(u32 count)
{
	imageCount = std::clamp(count, capabilities.minImageCount, capabilities.maxImageCount);
	return *this;
}

SwapChainBuilder& SwapChainBuilder::AddImageUsage(VkImageUsageFlagBits usageBit)
{
	m_ImageUsageFlags |= usageBit;
	return *this;
}

SwapChainBuilder& SwapChainBuilder::SetVSync(bool vSync)
{
	m_vSync = vSync;
	return *this;
}

VkSwapchainKHR SwapChainBuilder::Build(VkDevice device, u32 queueFamilyIndex, VkSwapchainKHR oldSwapChain)
{
	VkSwapchainKHR vkSwapChain = VK_NULL_HANDLE;
	if (m_Extent.width == 0 || m_Extent.height == 0)
		return vkSwapChain;

	VkSwapchainCreateInfoKHR swapChainInfo{};
	swapChainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapChainInfo.surface = m_vkSurface;
	swapChainInfo.minImageCount = imageCount;
	swapChainInfo.imageFormat = selectedSurface.format;
	swapChainInfo.imageColorSpace = selectedSurface.colorSpace;
	swapChainInfo.imageExtent = m_Extent;
	swapChainInfo.imageArrayLayers = 1;
	swapChainInfo.imageUsage = m_ImageUsageFlags;
	swapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapChainInfo.queueFamilyIndexCount = 1;
	swapChainInfo.pQueueFamilyIndices = &queueFamilyIndex;
	swapChainInfo.preTransform = capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ?
		VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
	swapChainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapChainInfo.presentMode = m_vSync ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
	swapChainInfo.clipped = VK_TRUE;
	swapChainInfo.oldSwapchain = oldSwapChain;

	VK_CHECK(vkCreateSwapchainKHR(device, &swapChainInfo, nullptr, &vkSwapChain));
	return vkSwapChain;
}


} // namespace vk