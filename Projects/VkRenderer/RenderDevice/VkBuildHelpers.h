#pragma once
#include <functional>
#include <vector>

namespace vk
{

//-------------------------------------------------------------------------
// Instance
//-------------------------------------------------------------------------
class InstanceBuilder
{
public:
	explicit InstanceBuilder();

	InstanceBuilder& AddValidationLayer(const char* layerString);
	InstanceBuilder& AddExtensionLayer(const char* layerString);
	InstanceBuilder& AddOptionalExtensionLayer(const char* layerString);

	InstanceBuilder& SetApiVersion(u32 version);

	InstanceBuilder& SetValidationFeatureEnable(const std::vector< VkValidationFeatureEnableEXT >& features);

	VkInstance Build();

	bool bValidationEnabled         = false;
	bool bValidationFeaturesEnabled = false;
	bool bDebugUtilsEnabled         = false;
	bool bSurfaceMaintenanceEnabled = false;

private:
	std::vector< const char* > m_RequestedValidationLayers;
	std::vector< const char* > m_EnabledValidationLayers;
	std::vector< const char* > m_RequiredExtensionLayers;
	std::vector< const char* > m_OptionalExtensionLayers;
	std::vector< const char* > m_EnabledExtensionLayers;
	std::vector< VkValidationFeatureEnableEXT > m_ValidationFeatureEnables;

	VkApplicationInfo       m_AppInfo = VkApplicationInfo();
	VkValidationFeaturesEXT m_ValidationFeatures = VkValidationFeaturesEXT();
};


//-------------------------------------------------------------------------
// Debug Messenger
//-------------------------------------------------------------------------
class DebugMessengerBuilder
{
public:
	explicit DebugMessengerBuilder();
	DebugMessengerBuilder& SetDebugMessageCallback(std::function<
		VkBool32(
			VkDebugUtilsMessageSeverityFlagBitsEXT Severity,
			VkDebugUtilsMessageTypeFlagsEXT Type,
			const VkDebugUtilsMessengerCallbackDataEXT* CallbackData, void* UserData) > fnCallback);

	VkDebugUtilsMessengerEXT Build(VkInstance instance);

private:
	VkDebugUtilsMessengerCreateInfoEXT	m_DebugMessengerInfo = VkDebugUtilsMessengerCreateInfoEXT();
};


//-------------------------------------------------------------------------
// Device
//-------------------------------------------------------------------------
class DeviceBuilder
{
public:
	explicit DeviceBuilder();

public:
	//-------------------------------------------------------------------------
	// Physical Device
	//-------------------------------------------------------------------------
	DeviceBuilder& SetMinimumApiVersion(u32 version);
	DeviceBuilder& SetPhysicalDeviceType(VkPhysicalDeviceType type);
	DeviceBuilder& RequireMeshShader(bool bRequired);
	DeviceBuilder& EnableSwapchainMaintenance(bool bInstanceSurfaceMaintenanceEnabled);

	VkPhysicalDevice                       physicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties             physicalDeviceProperties = {};
	VkPhysicalDeviceMaintenance3Properties physicalDeviceMaintenance3Properties = {};
	VkPhysicalDeviceFeatures               enabledPhysicalDeviceFeatures = {};

public:
	//-------------------------------------------------------------------------
	// Logical Device
	//-------------------------------------------------------------------------
	VkDevice Build(VkInstance instance);

	struct QueueFamilyIndex
	{
		u32 graphicsQueueIndex = UINT_MAX;
		u32 computeQueueIndex = UINT_MAX;
		u32 transferQueueIndex = UINT_MAX;
		u32 graphicsTimestampValidBits = 0;
		u32 computeTimestampValidBits = 0;
	} queueFamilyIndices;

public:
	//-------------------------------------------------------------------------
	// Device Capabilities
	//-------------------------------------------------------------------------
	bool bMeshShaderEnabled           = false;
	bool bSwapchainMaintenanceEnabled = false;
	bool bMemoryBudgetEnabled         = false;
	bool bPipelineStatisticsEnabled   = false;

private:
	VkResult queryPhysicalDevice(VkInstance instance);
	QueueFamilyIndex getQueueFamilyIndex() const;
	bool supportsDeviceExtension(const char* extensionName) const;

private:
	u32 m_MinimumApiVersion = VK_API_VERSION_1_3;
	VkPhysicalDeviceType m_PreferredDeviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	bool m_bRequireMeshShader = false;
	bool m_bInstanceSurfaceMaintenanceEnabled = false;

	VkPhysicalDeviceVulkan11Features m_Supported11Features = {};
	VkPhysicalDeviceVulkan12Features m_Supported12Features = {};
	VkPhysicalDeviceVulkan13Features m_Supported13Features = {};
	VkPhysicalDeviceMeshShaderFeaturesEXT m_SupportedMeshShaderFeatures = {};
	VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT m_SupportedSwapchainMaintenanceFeatures = {};
	VkPhysicalDeviceSubgroupProperties m_SubgroupProperties = {};
	VkPhysicalDeviceDescriptorIndexingProperties m_DescriptorIndexingProperties = {};
	VkPhysicalDevicePushDescriptorPropertiesKHR m_PushDescriptorProperties = {};
	std::vector< VkExtensionProperties > m_SelectedDeviceExtensions;
};


//-------------------------------------------------------------------------
// Swap Chain
//-------------------------------------------------------------------------
class SwapChainBuilder
{
public:
	explicit SwapChainBuilder(VkSurfaceKHR surface, VkPhysicalDevice physicalDevice);

	SwapChainBuilder& SetDesiredImageCount(u32 count);
	SwapChainBuilder& SetDesiredImageFormat(VkFormat format);
	SwapChainBuilder& SetDesiredImageResolution(u32 width, u32 height);
	[[nodiscard]]
	inline VkExtent2D Extent() const { return m_Extent; }
	SwapChainBuilder& AddImageUsage(VkImageUsageFlagBits usageBit);

	SwapChainBuilder& SetVSync(bool vSync);

	VkSwapchainKHR Build(VkDevice device, u32 queueFamilyIndex, VkSwapchainKHR oldSwapChain = VK_NULL_HANDLE);

	VkSurfaceFormatKHR			selectedSurface;
	VkSurfaceCapabilitiesKHR	capabilities;
	u32							imageCount = 2;

private:
	VkSurfaceKHR	 m_vkSurface = VK_NULL_HANDLE;
	VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;

	std::vector< VkSurfaceFormatKHR > m_SurfaceFormats;
	std::vector< VkPresentModeKHR > m_PresentModes;

	VkImageUsageFlags m_ImageUsageFlags;

	VkExtent2D m_Extent;
	bool m_vSync = true;
};

} // namespace vk