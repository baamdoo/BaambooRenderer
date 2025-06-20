#pragma once
#include <any>
#include <array>

namespace vk
{

template< size_t NumElement >
struct FeatureChain
{
	auto& bind(auto nextFeature)
	{
		BB_ASSERT(count < NumElement, "Chain is full");
		data[count] = nextFeature;

		auto& next = std::any_cast<decltype(nextFeature)&>(data[count]);
		next.pNext = std::exchange(head, &next);
		count++;

		return next;
	}

	auto& tail()
	{
		if (count == 0)
			std::any();

		return data[count - 1];
	}

	std::array< std::any, NumElement > data;
	u32 count;

	void* head = nullptr;
};

//-------------------------------------------------------------------------
// Instance
//-------------------------------------------------------------------------
class InstanceBuilder
{
public:
	explicit InstanceBuilder();

	InstanceBuilder& AddValidationLayer(const char* layerString);
	InstanceBuilder& AddExtensionLayer(const char* layerString);

	InstanceBuilder& SetApiVersion(u32 version);

	InstanceBuilder& SetValidationFeatureEnable(const std::vector< VkValidationFeatureEnableEXT >& features);

	VkInstance Build();

private:
	std::vector< const char* > m_ValidationLayers;
	std::vector< const char* > m_ExtensionLayers;

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
enum : u8
{
	// **
	// 0 ~ 7 : Core Features
	// **
	ePhysicalDeviceFeature_Core_1_X = 0,
	ePhysicalDeviceFeature_AdditionalShader,
	ePhysicalDeviceFeature_MultiViewport,
	ePhysicalDeviceFeature_DynamicIndexing,
	ePhysicalDeviceFeature_IndirectRendering,
	ePhysicalDeviceFeature_DescriptorIndexing,
	ePhysicalDeviceFeature_DeviceAddress,
	ePhysicalDeviceFeature_SamplerAnistropy,

	// **
	// 32 ~ 63 : More Extension Features
	// **
	ePhysicalDeviceFeature_Extension = 32,
	ePhysicalDeviceFeature_Sync2,
	ePhysicalDeviceFeature_IndexTypeUint8,
	ePhysicalDeviceFeature_SwapChainMaintenance,
};
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
	DeviceBuilder& AddPhysicalDeviceFeature(u8 featureBit);

	VkPhysicalDevice					     physicalDevice           = VK_NULL_HANDLE;
	VkPhysicalDeviceFeatures			     physicalDeviceFeatures   = VkPhysicalDeviceFeatures();
	VkPhysicalDeviceFeatures2			     physicalDeviceFeatures2  = VkPhysicalDeviceFeatures2();
	VkPhysicalDeviceVulkan11Features	     physicalDevice11Features = VkPhysicalDeviceVulkan11Features();
	VkPhysicalDeviceVulkan12Features	     physicalDevice12Features = VkPhysicalDeviceVulkan12Features();
	VkPhysicalDeviceProperties			     physicalDeviceProperties = VkPhysicalDeviceProperties();

public:
	//-------------------------------------------------------------------------
	// Logical Device
	//-------------------------------------------------------------------------
	DeviceBuilder& AddDeviceExtension(const char* extension);
	VkDevice Build(VkInstance instance);

	struct QueueFamilyIndex
	{
		u32	graphicsQueueIndex = UINT_MAX;
		u32	computeQueueIndex = UINT_MAX;
		u32	transferQueueIndex = UINT_MAX;
	} queueFamilyIndices;

private:
	VkResult queryPhysicalDevice(VkInstance instance);
	QueueFamilyIndex getQueueFamilyIndex() const;

private:
	struct
	{
		u32 apiVersion;
		i64 featureBits;
		VkPhysicalDeviceType deviceType;
	} m_PhysicalRequirements;

	std::vector< const char* > m_LogicalDeviceExtensions;
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

	VkImageUsageFlags m_ImageUsageFlags;

	VkExtent2D m_Extent;
	bool m_vSync = true;
};

} // namespace vk