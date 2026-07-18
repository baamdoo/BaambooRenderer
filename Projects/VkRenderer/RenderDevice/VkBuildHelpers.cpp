#include "RendererPch.h"
#include "VkBuildHelpers.h"

#include <limits>

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
#ifdef _DEBUG
	m_RequestedValidationLayers.push_back("VK_LAYER_KHRONOS_validation");
	m_OptionalExtensionLayers.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	m_OptionalExtensionLayers.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
#endif

	m_RequiredExtensionLayers.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(_WIN32)
	m_RequiredExtensionLayers.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	m_RequiredExtensionLayers.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
	#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	m_RequiredExtensionLayers.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
	#else
	m_RequiredExtensionLayers.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
	#endif
#elif defined(__APPLE__)
	m_RequiredExtensionLayers.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#endif

	// VK_EXT_surface_maintenance1 depends on VK_KHR_get_surface_capabilities2.
	// Treat them as one optional capability instead of enabling half the contract.
	m_OptionalExtensionLayers.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
	m_OptionalExtensionLayers.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);

	m_AppInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	m_AppInfo.pApplicationName   = "VkRenderer";
	m_AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	m_AppInfo.pEngineName        = "VkRenderer";
	m_AppInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
	m_AppInfo.apiVersion         = VK_API_VERSION_1_3;
}

InstanceBuilder& InstanceBuilder::AddValidationLayer(const char* layerString)
{
#ifdef _DEBUG
	m_RequestedValidationLayers.push_back(layerString);
#else
	UNUSED(layerString);
#endif
	return *this;
}

InstanceBuilder& InstanceBuilder::AddExtensionLayer(const char* layerString)
{
	m_RequiredExtensionLayers.push_back(layerString);
	return *this;
}

InstanceBuilder& InstanceBuilder::AddOptionalExtensionLayer(const char* layerString)
{
	m_OptionalExtensionLayers.push_back(layerString);
	return *this;
}

InstanceBuilder& InstanceBuilder::SetApiVersion(u32 version)
{
	m_AppInfo.apiVersion = version;
	return *this;
}

InstanceBuilder& InstanceBuilder::SetValidationFeatureEnable(const std::vector< VkValidationFeatureEnableEXT >& features)
{
#ifdef _DEBUG
	m_ValidationFeatureEnables = features;
#else
	UNUSED(features);
#endif
	return *this;
}

VkInstance InstanceBuilder::Build()
{
	u32 extensionCount = 0;
	VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	BB_ASSERT(result == VK_SUCCESS, "Failed to enumerate Vulkan instance extensions (%d).", static_cast<i32>(result));

	std::vector< VkExtensionProperties > availableExtensions(extensionCount);
	result = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
	BB_ASSERT(result == VK_SUCCESS, "Failed to enumerate Vulkan instance extensions (%d).", static_cast<i32>(result));

	u32 layerCount = 0;
	result = vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
	BB_ASSERT(result == VK_SUCCESS, "Failed to enumerate Vulkan instance layers (%d).", static_cast<i32>(result));

	std::vector< VkLayerProperties > availableLayers(layerCount);
	result = vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
	BB_ASSERT(result == VK_SUCCESS, "Failed to enumerate Vulkan instance layers (%d).", static_cast<i32>(result));

	m_EnabledValidationLayers.clear();
	for (const char* requested : m_RequestedValidationLayers)
	{
		const bool bAvailable = std::ranges::any_of(availableLayers,
			[requested](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, requested) == 0; });
		if (bAvailable)
			m_EnabledValidationLayers.push_back(requested);
		else
			fprintf(stderr, "Optional Vulkan validation layer is unavailable: %s\n", requested);
	}
	bValidationEnabled = !m_EnabledValidationLayers.empty();

	std::vector< VkExtensionProperties > availableLayerExtensions;
	for (const char* enabledLayer : m_EnabledValidationLayers)
	{
		u32 layerExtensionCount = 0;
		result = vkEnumerateInstanceExtensionProperties(enabledLayer, &layerExtensionCount, nullptr);
		BB_ASSERT(result == VK_SUCCESS, "Failed to enumerate extensions for Vulkan layer %s (%d).",
			enabledLayer, static_cast<i32>(result));
		if (layerExtensionCount == 0)
			continue;

		const size_t firstExtension = availableLayerExtensions.size();
		availableLayerExtensions.resize(firstExtension + layerExtensionCount);
		result = vkEnumerateInstanceExtensionProperties(
			enabledLayer, &layerExtensionCount, availableLayerExtensions.data() + firstExtension);
		BB_ASSERT(result == VK_SUCCESS, "Failed to enumerate extensions for Vulkan layer %s (%d).",
			enabledLayer, static_cast<i32>(result));
		availableLayerExtensions.resize(firstExtension + layerExtensionCount);
	}

	auto hasExtension = [&availableExtensions, &availableLayerExtensions](const char* name)
	{
		const auto matchesName = [name](const VkExtensionProperties& extension)
		{
			return std::strcmp(extension.extensionName, name) == 0;
		};
		return std::ranges::any_of(availableExtensions, matchesName) ||
			std::ranges::any_of(availableLayerExtensions, matchesName);
	};
	auto addEnabledExtension = [this](const char* name)
	{
		if (std::ranges::find_if(m_EnabledExtensionLayers,
			[name](const char* enabled) { return std::strcmp(enabled, name) == 0; }) == m_EnabledExtensionLayers.end())
		{
			m_EnabledExtensionLayers.push_back(name);
		}
	};

	m_EnabledExtensionLayers.clear();
	for (const char* required : m_RequiredExtensionLayers)
	{
		BB_ASSERT(hasExtension(required), "Required Vulkan instance extension is unavailable: %s", required);
		addEnabledExtension(required);
	}

	const bool bSurfaceMaintenanceSupported =
		hasExtension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) &&
		hasExtension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
	if (bSurfaceMaintenanceSupported)
	{
		addEnabledExtension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
		addEnabledExtension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
	}
	bSurfaceMaintenanceEnabled = bSurfaceMaintenanceSupported;

	for (const char* optional : m_OptionalExtensionLayers)
	{
		const bool bSurfaceDependency =
			std::strcmp(optional, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) == 0 ||
			std::strcmp(optional, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME) == 0;
		const bool bValidationDependency =
			std::strcmp(optional, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0;
		const bool bValidationDependencySatisfied =
			!bValidationDependency || (bValidationEnabled && !m_ValidationFeatureEnables.empty());
		if (!bSurfaceDependency && bValidationDependencySatisfied && hasExtension(optional))
			addEnabledExtension(optional);
	}
	bDebugUtilsEnabled = std::ranges::any_of(m_EnabledExtensionLayers,
		[](const char* enabled) { return std::strcmp(enabled, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0; });
	bValidationFeaturesEnabled = bValidationEnabled && !m_ValidationFeatureEnables.empty() &&
		std::ranges::any_of(m_EnabledExtensionLayers,
			[](const char* enabled) { return std::strcmp(enabled, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0; });


	m_ValidationFeatures = {};
	m_ValidationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
	m_ValidationFeatures.enabledValidationFeatureCount = static_cast<u32>(m_ValidationFeatureEnables.size());
	m_ValidationFeatures.pEnabledValidationFeatures = m_ValidationFeatureEnables.data();

	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pNext                   = bValidationEnabled && bValidationFeaturesEnabled &&
		!m_ValidationFeatureEnables.empty() ? &m_ValidationFeatures : nullptr;
	instanceInfo.pApplicationInfo        = &m_AppInfo;
	instanceInfo.enabledExtensionCount   = static_cast<u32>(m_EnabledExtensionLayers.size());
	instanceInfo.ppEnabledExtensionNames = m_EnabledExtensionLayers.data();
	instanceInfo.enabledLayerCount       = static_cast<u32>(m_EnabledValidationLayers.size());
	instanceInfo.ppEnabledLayerNames     = m_EnabledValidationLayers.data();

	VkInstance instance = VK_NULL_HANDLE;
	result = vkCreateInstance(&instanceInfo, nullptr, &instance);
	BB_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan instance (%d).", static_cast<i32>(result));
	return instance;
}


//-------------------------------------------------------------------------
// Debug Messenger
//-------------------------------------------------------------------------
DebugMessengerBuilder::DebugMessengerBuilder()
{
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

	const auto pCallback = callback.target< TCallback >();
	BB_ASSERT(pCallback != nullptr, "Vulkan debug callback must be a function pointer.");

	m_DebugMessengerInfo.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
	m_DebugMessengerInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	m_DebugMessengerInfo.pfnUserCallback = *pCallback;

	return *this;
}

VkDebugUtilsMessengerEXT DebugMessengerBuilder::Build(VkInstance instance)
{
	VkDebugUtilsMessengerEXT outDebugMessenger = VK_NULL_HANDLE;
#ifdef _DEBUG
	const auto createDebugMessenger =
		reinterpret_cast< PFN_vkCreateDebugUtilsMessengerEXT >(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
	if (createDebugMessenger == nullptr)
		return outDebugMessenger;

	const VkResult result = createDebugMessenger(instance, &m_DebugMessengerInfo, nullptr, &outDebugMessenger);
	BB_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan debug messenger (%d).", static_cast<i32>(result));
#else
	UNUSED(instance);
#endif
	return outDebugMessenger;
}


//-------------------------------------------------------------------------
// Device
//-------------------------------------------------------------------------
DeviceBuilder::DeviceBuilder()
{
	physicalDeviceMaintenance3Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
	m_Supported11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	m_Supported12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	m_Supported13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	m_SupportedMeshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
	m_SupportedSwapchainMaintenanceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
	m_SubgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
	m_DescriptorIndexingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
	m_PushDescriptorProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR;
}

DeviceBuilder& DeviceBuilder::SetMinimumApiVersion(u32 version)
{
	m_MinimumApiVersion = version;
	return *this;
}

DeviceBuilder& DeviceBuilder::SetPhysicalDeviceType(VkPhysicalDeviceType type)
{
	m_PreferredDeviceType = type;
	return *this;
}

DeviceBuilder& DeviceBuilder::RequireMeshShader(bool bRequired)
{
	m_bRequireMeshShader = bRequired;
	return *this;
}

DeviceBuilder& DeviceBuilder::EnableSwapchainMaintenance(bool bInstanceSurfaceMaintenanceEnabled)
{
	m_bInstanceSurfaceMaintenanceEnabled = bInstanceSurfaceMaintenanceEnabled;
	return *this;
}

VkDevice DeviceBuilder::Build(VkInstance instance)
{
	const VkResult queryResult = queryPhysicalDevice(instance);
	BB_ASSERT(queryResult == VK_SUCCESS,
		"No Vulkan physical device satisfies the renderer's required Vulkan 1.3 capability contract.");

	queueFamilyIndices = getQueueFamilyIndex();
	BB_ASSERT(queueFamilyIndices.graphicsQueueIndex != UINT_MAX &&
		queueFamilyIndices.computeQueueIndex != UINT_MAX &&
		queueFamilyIndices.transferQueueIndex != UINT_MAX,
		"The selected Vulkan device does not expose the required graphics/compute/transfer queues.");

	constexpr float queuePriority = 1.0f;
	std::vector< VkDeviceQueueCreateInfo > queueInfos;
	const std::array< u32, 3 > queueFamilies =
	{
		queueFamilyIndices.graphicsQueueIndex,
		queueFamilyIndices.computeQueueIndex,
		queueFamilyIndices.transferQueueIndex,
	};
	for (const u32 familyIndex : queueFamilies)
	{
		const bool bAlreadyAdded = std::ranges::any_of(queueInfos,
			[familyIndex](const VkDeviceQueueCreateInfo& info) { return info.queueFamilyIndex == familyIndex; });
		if (bAlreadyAdded)
			continue;

		VkDeviceQueueCreateInfo queueInfo = {};
		queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = familyIndex;
		queueInfo.queueCount       = 1;
		queueInfo.pQueuePriorities = &queuePriority;
		queueInfos.push_back(queueInfo);
	}

	std::vector< const char* > enabledExtensions =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
	};

	bMeshShaderEnabled = m_bRequireMeshShader;
	if (bMeshShaderEnabled)
		enabledExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);

	bMemoryBudgetEnabled = supportsDeviceExtension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	if (bMemoryBudgetEnabled)
		enabledExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

	bSwapchainMaintenanceEnabled =
		m_bInstanceSurfaceMaintenanceEnabled &&
		supportsDeviceExtension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) &&
		m_SupportedSwapchainMaintenanceFeatures.swapchainMaintenance1 == VK_TRUE;
	if (bSwapchainMaintenanceEnabled)
		enabledExtensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);

	bPipelineStatisticsEnabled = enabledPhysicalDeviceFeatures.pipelineStatisticsQuery == VK_TRUE;

	VkPhysicalDeviceVulkan11Features enabled11 = {};
	enabled11.sType                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	enabled11.shaderDrawParameters = VK_TRUE;

	VkPhysicalDeviceVulkan12Features enabled12 = {};
	enabled12.sType                                           = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	enabled12.drawIndirectCount                               = VK_TRUE;
	enabled12.descriptorIndexing                              = VK_TRUE;
	enabled12.shaderSampledImageArrayNonUniformIndexing       = VK_TRUE;
	enabled12.runtimeDescriptorArray                          = VK_TRUE;
	enabled12.descriptorBindingSampledImageUpdateAfterBind    = VK_TRUE;
	enabled12.descriptorBindingPartiallyBound                 = VK_TRUE;
	enabled12.descriptorBindingVariableDescriptorCount        = VK_TRUE;
	enabled12.samplerFilterMinmax                             = VK_TRUE;
	enabled12.bufferDeviceAddress                             = VK_TRUE;

	VkPhysicalDeviceVulkan13Features enabled13 = {};
	enabled13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	enabled13.dynamicRendering = VK_TRUE;
	enabled13.synchronization2 = VK_TRUE;
	enabled13.maintenance4 = VK_TRUE;

	VkPhysicalDeviceMeshShaderFeaturesEXT enabledMeshShader = {};
	enabledMeshShader.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
	enabledMeshShader.taskShader = bMeshShaderEnabled ? VK_TRUE : VK_FALSE;
	enabledMeshShader.meshShader = bMeshShaderEnabled ? VK_TRUE : VK_FALSE;

	VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT enabledSwapchainMaintenance = {};
	enabledSwapchainMaintenance.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
	enabledSwapchainMaintenance.swapchainMaintenance1 = bSwapchainMaintenanceEnabled ? VK_TRUE : VK_FALSE;

	VkPhysicalDeviceFeatures2 enabledFeatures2 = {};
	enabledFeatures2.sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	enabledFeatures2.features = enabledPhysicalDeviceFeatures;
	enabledFeatures2.pNext    = &enabled11;
	enabled11.pNext            = &enabled12;
	enabled12.pNext            = &enabled13;

	void** ppNext = &enabled13.pNext;
	if (bMeshShaderEnabled)
	{
		*ppNext = &enabledMeshShader;
		ppNext = &enabledMeshShader.pNext;
	}
	if (bSwapchainMaintenanceEnabled)
		*ppNext = &enabledSwapchainMaintenance;

	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.pNext                   = &enabledFeatures2;
	deviceInfo.queueCreateInfoCount    = static_cast<u32>(queueInfos.size());
	deviceInfo.pQueueCreateInfos       = queueInfos.data();
	deviceInfo.enabledExtensionCount   = static_cast<u32>(enabledExtensions.size());
	deviceInfo.ppEnabledExtensionNames = enabledExtensions.data();

	VkDevice outDevice = VK_NULL_HANDLE;
	const VkResult result = vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &outDevice);
	BB_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan logical device (%d).", static_cast<i32>(result));
	return outDevice;
}

VkResult DeviceBuilder::queryPhysicalDevice(VkInstance instance)
{
	u32 gpuCount = 0;
	VkResult result = vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
	if (result != VK_SUCCESS || gpuCount == 0)
		return result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result;

	std::vector< VkPhysicalDevice > candidates(gpuCount);
	result = vkEnumeratePhysicalDevices(instance, &gpuCount, candidates.data());
	if (result != VK_SUCCESS)
		return result;

	i64 bestScore = std::numeric_limits<i64>::min();
	constexpr u32 kCommonDescriptorCount = kMaxBindlessDescriptorResourceCount + 13u;
	constexpr u32 kMaxComputeDescriptorCount = kCommonDescriptorCount + 15u;
	constexpr u32 kMaxComputeCombinedSamplerCount = kMaxBindlessDescriptorResourceCount + 10u;
	constexpr u32 kMaxPushDescriptorCount = 15u;
	constexpr u32 kMaxPushStorageBufferCount = 5u;
	constexpr u32 kMaxPushStorageImageCount = 13u;

	constexpr u32 kMaxComputeWorkgroupInvocations = 512u;
	constexpr u32 kMaxComputeWorkgroupSizeX = 256u;
	constexpr u32 kMaxComputeWorkgroupSizeY = 16u;
	constexpr u32 kMaxComputeWorkgroupSizeZ = 8u;
	constexpr u32 kMeshWorkgroupSize = 32u;
	constexpr u32 kTaskPayloadSize = 288u;
	constexpr u32 kTaskSharedMemorySize = 96u;
	constexpr u32 kMeshSharedMemorySize = 928u;
	constexpr u32 kMaxEmittedMeshWorkgroups = 10752u;
	constexpr u32 kMeshOutputVertexCount = 64u;
	constexpr u32 kMeshOutputPrimitiveCount = 124u;

	for (const VkPhysicalDevice gpu : candidates)
	{
		u32 extensionCount = 0;
		if (vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, nullptr) != VK_SUCCESS)
			continue;
		std::vector< VkExtensionProperties > extensions(extensionCount);
		if (vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, extensions.data()) != VK_SUCCESS)
			continue;

		auto hasExtension = [&extensions](const char* name)
		{
			return std::ranges::any_of(extensions,
				[name](const VkExtensionProperties& extension) { return std::strcmp(extension.extensionName, name) == 0; });
		};
		if (!hasExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
			!hasExtension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) ||
			(m_bRequireMeshShader && !hasExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME)))
		{
			continue;
		}

		VkPhysicalDeviceProperties baseProperties = {};
		vkGetPhysicalDeviceProperties(gpu, &baseProperties);
		if (baseProperties.apiVersion < m_MinimumApiVersion)
			continue;

		VkPhysicalDeviceMaintenance3Properties maintenance3 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES };
		VkPhysicalDeviceSubgroupProperties subgroup = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
		VkPhysicalDeviceDescriptorIndexingProperties descriptorIndexing = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES };
		VkPhysicalDevicePushDescriptorPropertiesKHR pushDescriptor = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR };
		VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT };
		maintenance3.pNext = &subgroup;
		subgroup.pNext = &descriptorIndexing;
		descriptorIndexing.pNext = &pushDescriptor;
		if (hasExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME))
			pushDescriptor.pNext = &meshProperties;

		VkPhysicalDeviceProperties2 properties2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		properties2.pNext = &maintenance3;
		vkGetPhysicalDeviceProperties2(gpu, &properties2);
		VkPhysicalDeviceVulkan11Features features11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
		VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceVulkan13Features features13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceMeshShaderFeaturesEXT meshShader = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
		VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchainMaintenance =
			{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT };

		VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		features2.pNext = &features11;
		features11.pNext = &features12;
		features12.pNext = &features13;
		void** ppNext = &features13.pNext;
		if (hasExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME))
		{
			*ppNext = &meshShader;
			ppNext = &meshShader.pNext;
		}
		if (hasExtension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME))
			*ppNext = &swapchainMaintenance;
		vkGetPhysicalDeviceFeatures2(gpu, &features2);

		const VkPhysicalDeviceFeatures& core = features2.features;
		const bool bRequiredCoreFeatures =
			core.multiDrawIndirect &&
			core.drawIndirectFirstInstance &&
			core.shaderInt64 &&
			core.shaderStorageImageExtendedFormats &&
			core.shaderStorageImageWriteWithoutFormat;
		const bool bRequired11Features = features11.shaderDrawParameters;
		const bool bRequired12Features =
			features12.drawIndirectCount &&
			features12.descriptorIndexing &&
			features12.shaderSampledImageArrayNonUniformIndexing &&
			features12.runtimeDescriptorArray &&
			features12.descriptorBindingSampledImageUpdateAfterBind &&
			features12.descriptorBindingPartiallyBound &&
			features12.descriptorBindingVariableDescriptorCount &&
			features12.samplerFilterMinmax &&
			features12.bufferDeviceAddress;
		const bool bRequired13Features =
			features13.dynamicRendering &&
			features13.synchronization2 &&
			features13.maintenance4;
		const auto& limits = properties2.properties.limits;
		const bool bDescriptorLimits =
			maintenance3.maxPerSetDescriptors >= kCommonDescriptorCount &&
			descriptorIndexing.maxPerStageUpdateAfterBindResources >= kMaxComputeDescriptorCount &&
			descriptorIndexing.maxPerStageDescriptorUpdateAfterBindSamplers >= kMaxComputeCombinedSamplerCount &&
			descriptorIndexing.maxPerStageDescriptorUpdateAfterBindSampledImages >= kMaxComputeCombinedSamplerCount &&
			descriptorIndexing.maxDescriptorSetUpdateAfterBindSamplers >= kMaxComputeCombinedSamplerCount &&
			descriptorIndexing.maxDescriptorSetUpdateAfterBindSampledImages >= kMaxComputeCombinedSamplerCount &&
			descriptorIndexing.maxUpdateAfterBindDescriptorsInAllPools >=
				kCommonDescriptorCount * kMaxFramesInFlight &&
			limits.maxPerStageResources >= kMaxPushDescriptorCount &&
			limits.maxPerStageDescriptorStorageBuffers >= kMaxPushStorageBufferCount &&
			limits.maxDescriptorSetStorageBuffers >= kMaxPushStorageBufferCount &&
			limits.maxPerStageDescriptorStorageImages >= kMaxPushStorageImageCount &&
			limits.maxDescriptorSetStorageImages >= kMaxPushStorageImageCount &&
			pushDescriptor.maxPushDescriptors >= kMaxPushDescriptorCount;
		const bool bComputeLimits =
			limits.maxComputeWorkGroupInvocations >= kMaxComputeWorkgroupInvocations &&
			limits.maxComputeWorkGroupSize[0] >= kMaxComputeWorkgroupSizeX &&
			limits.maxComputeWorkGroupSize[1] >= kMaxComputeWorkgroupSizeY &&
			limits.maxComputeWorkGroupSize[2] >= kMaxComputeWorkgroupSizeZ;
		const bool bComputeSubgroup =
			(subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0 &&
			(subgroup.supportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT) != 0;
		const bool bMeshFeatures =
			!m_bRequireMeshShader || (meshShader.taskShader && meshShader.meshShader);
		const bool bMeshLimits =
			!m_bRequireMeshShader ||
			(meshProperties.maxTaskWorkGroupInvocations >= kMeshWorkgroupSize &&
			 meshProperties.maxTaskWorkGroupSize[0] >= kMeshWorkgroupSize &&
			 meshProperties.maxTaskWorkGroupSize[1] >= 1 &&
			 meshProperties.maxTaskWorkGroupSize[2] >= 1 &&
			 meshProperties.maxTaskPayloadSize >= kTaskPayloadSize &&
			 meshProperties.maxTaskSharedMemorySize >= kTaskSharedMemorySize &&
			 meshProperties.maxTaskPayloadAndSharedMemorySize >= kTaskPayloadSize + kTaskSharedMemorySize &&
			 meshProperties.maxMeshWorkGroupInvocations >= kMeshWorkgroupSize &&
			 meshProperties.maxMeshWorkGroupSize[0] >= kMeshWorkgroupSize &&
			 meshProperties.maxMeshWorkGroupSize[1] >= 1 &&
			 meshProperties.maxMeshWorkGroupSize[2] >= 1 &&
			 meshProperties.maxMeshSharedMemorySize >= kMeshSharedMemorySize &&
			 meshProperties.maxMeshPayloadAndSharedMemorySize >= kTaskPayloadSize + kMeshSharedMemorySize &&
			 meshProperties.maxMeshWorkGroupCount[0] >= kMaxEmittedMeshWorkgroups &&
			 meshProperties.maxMeshWorkGroupTotalCount >= kMaxEmittedMeshWorkgroups &&
			 meshProperties.maxMeshOutputVertices >= kMeshOutputVertexCount &&
			 meshProperties.maxMeshOutputPrimitives >= kMeshOutputPrimitiveCount);
		const VkSubgroupFeatureFlags requiredTaskOps =
			VK_SUBGROUP_FEATURE_BASIC_BIT |
			VK_SUBGROUP_FEATURE_BALLOT_BIT |
			VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;
		const bool bTaskSubgroup =
			!m_bRequireMeshShader ||
			((subgroup.supportedStages & VK_SHADER_STAGE_TASK_BIT_EXT) != 0 &&
			 (subgroup.supportedOperations & requiredTaskOps) == requiredTaskOps &&
			 subgroup.subgroupSize >= kMeshWorkgroupSize);

		if (!bRequiredCoreFeatures || !bRequired11Features || !bRequired12Features ||
			!bRequired13Features || !bDescriptorLimits || !bComputeLimits ||
			!bComputeSubgroup || !bMeshFeatures || !bMeshLimits || !bTaskSubgroup)
		{
			continue;
		}

		i64 score = static_cast<i64>(properties2.properties.limits.maxImageDimension2D);
		if (properties2.properties.deviceType == m_PreferredDeviceType)
			score += 1'000'000;
		if (properties2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			score += 100'000;
		if (score <= bestScore)
			continue;

		bestScore = score;
		physicalDevice = gpu;
		physicalDeviceProperties = properties2.properties;
		physicalDeviceMaintenance3Properties = maintenance3;
		physicalDeviceMaintenance3Properties.pNext = nullptr;
		m_SubgroupProperties = subgroup;
		m_SubgroupProperties.pNext = nullptr;
		m_DescriptorIndexingProperties = descriptorIndexing;
		m_DescriptorIndexingProperties.pNext = nullptr;
		m_PushDescriptorProperties = pushDescriptor;
		m_PushDescriptorProperties.pNext = nullptr;
		m_Supported11Features = features11;
		m_Supported11Features.pNext = nullptr;
		m_Supported12Features = features12;
		m_Supported12Features.pNext = nullptr;
		m_Supported13Features = features13;
		m_Supported13Features.pNext = nullptr;
		m_SupportedMeshShaderFeatures = meshShader;
		m_SupportedMeshShaderFeatures.pNext = nullptr;
		m_SupportedSwapchainMaintenanceFeatures = swapchainMaintenance;
		m_SupportedSwapchainMaintenanceFeatures.pNext = nullptr;
		m_SelectedDeviceExtensions = std::move(extensions);

		enabledPhysicalDeviceFeatures = {};
		enabledPhysicalDeviceFeatures.multiDrawIndirect = VK_TRUE;
		enabledPhysicalDeviceFeatures.drawIndirectFirstInstance = VK_TRUE;
		enabledPhysicalDeviceFeatures.logicOp = core.logicOp;
		enabledPhysicalDeviceFeatures.imageCubeArray = core.imageCubeArray;
		enabledPhysicalDeviceFeatures.textureCompressionBC = core.textureCompressionBC;
		enabledPhysicalDeviceFeatures.textureCompressionETC2 = core.textureCompressionETC2;
		enabledPhysicalDeviceFeatures.textureCompressionASTC_LDR = core.textureCompressionASTC_LDR;
		enabledPhysicalDeviceFeatures.shaderInt64 = VK_TRUE;
		enabledPhysicalDeviceFeatures.shaderStorageImageExtendedFormats = VK_TRUE;
		enabledPhysicalDeviceFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;
		enabledPhysicalDeviceFeatures.pipelineStatisticsQuery = core.pipelineStatisticsQuery;
	}

	return physicalDevice != VK_NULL_HANDLE ? VK_SUCCESS : VK_ERROR_FEATURE_NOT_PRESENT;
}

bool DeviceBuilder::supportsDeviceExtension(const char* extensionName) const
{
	return std::ranges::any_of(m_SelectedDeviceExtensions,
		[extensionName](const VkExtensionProperties& extension)
		{
			return std::strcmp(extension.extensionName, extensionName) == 0;
		});
}

DeviceBuilder::QueueFamilyIndex DeviceBuilder::getQueueFamilyIndex() const
{
	QueueFamilyIndex indices;

	u32 familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
	std::vector< VkQueueFamilyProperties > families(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

	for (u32 i = 0; i < familyCount; ++i)
	{
		const VkQueueFlags flags = families[i].queueFlags;
		if (indices.graphicsQueueIndex == UINT_MAX && (flags & VK_QUEUE_GRAPHICS_BIT))
			indices.graphicsQueueIndex = i;
		if ((flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT))
			indices.computeQueueIndex = i;
		if ((flags & VK_QUEUE_TRANSFER_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT) && !(flags & VK_QUEUE_COMPUTE_BIT))
			indices.transferQueueIndex = i;
	}

	for (u32 i = 0; i < familyCount; ++i)
	{
		const VkQueueFlags flags = families[i].queueFlags;
		if (indices.computeQueueIndex == UINT_MAX && (flags & VK_QUEUE_COMPUTE_BIT))
			indices.computeQueueIndex = i;
		if (indices.transferQueueIndex == UINT_MAX && (flags & VK_QUEUE_TRANSFER_BIT))
			indices.transferQueueIndex = i;
	}

	if (indices.graphicsQueueIndex != UINT_MAX)
		indices.graphicsTimestampValidBits = families[indices.graphicsQueueIndex].timestampValidBits;
	if (indices.computeQueueIndex != UINT_MAX)
		indices.computeTimestampValidBits = families[indices.computeQueueIndex].timestampValidBits;

	return indices;
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

	u32 presentModeCount = 0;
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(m_vkPhysicalDevice, m_vkSurface, &presentModeCount, nullptr));
	m_PresentModes.resize(presentModeCount);
	BB_ASSERT(presentModeCount > 0, "The Vulkan surface exposes no present mode.");
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
		m_vkPhysicalDevice, m_vkSurface, &presentModeCount, m_PresentModes.data()));

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
	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		m_Extent = capabilities.currentExtent;
	}
	else
	{
		m_Extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		m_Extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
	}

	return *this;
}

SwapChainBuilder& SwapChainBuilder::SetDesiredImageCount(u32 count)
{
	imageCount = std::max(count, capabilities.minImageCount);
	if (capabilities.maxImageCount > 0)
		imageCount = std::min(imageCount, capabilities.maxImageCount);
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
	const auto supportsPresentMode = [this](VkPresentModeKHR mode)
	{
		return std::ranges::find(m_PresentModes, mode) != m_PresentModes.end();
	};
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if (m_vSync && supportsPresentMode(VK_PRESENT_MODE_MAILBOX_KHR))
		presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
	else if (!m_vSync && supportsPresentMode(VK_PRESENT_MODE_IMMEDIATE_KHR))
		presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	else if (!m_vSync && supportsPresentMode(VK_PRESENT_MODE_MAILBOX_KHR))
		presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
	swapChainInfo.presentMode = presentMode;
	swapChainInfo.clipped = VK_TRUE;
	swapChainInfo.oldSwapchain = oldSwapChain;

	VK_CHECK(vkCreateSwapchainKHR(device, &swapChainInfo, nullptr, &vkSwapChain));
	return vkSwapChain;
}


} // namespace vk