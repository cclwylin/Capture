// Linux x86-64 integration probe for the experimental lavapipe implementation.
#include <vulkan/vulkan.h>
#include <sys/mman.h>
#include <array>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool pass, const char *message)
{
  if(!pass) throw std::runtime_error(message);
}
static void check(VkResult result, const char *message)
{
  if(result != VK_SUCCESS)
    throw std::runtime_error(std::string(message) + ": VkResult=" + std::to_string(result));
}

int main(int argc, char **argv)
{
  try
  {
    require(argc == 4, "usage: capture_replay_probe capture|replay address-file noop.rgen.spv");
    const bool replay = std::string(argv[1]) == "replay";
    require(replay || std::string(argv[1]) == "capture", "invalid mode");
    uint64_t opaque = 0, expectedBuffer = 0, expectedAS = 0;
    if(replay)
    {
      std::ifstream file(argv[2]);
      require(bool(file >> opaque >> expectedBuffer >> expectedAS), "invalid address file");
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "lavapipe capture replay probe";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    VkInstance instance;
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "create instance");
    uint32_t count = 1;
    VkPhysicalDevice physical;
    check(vkEnumeratePhysicalDevices(instance, &count, &physical), "enumerate devices");
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    std::cout << "device: " << properties.deviceName << '\n';
    VkPhysicalDeviceBufferDeviceAddressFeatures bda{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    bda.pNext = &as;
    as.pNext = &rt;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &bda;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    require(bda.bufferDeviceAddressCaptureReplay && as.accelerationStructureCaptureReplay &&
            rt.rayTracingPipelineShaderGroupHandleCaptureReplay, "capture replay features unavailable");

    bda = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    bda.bufferDeviceAddress = VK_TRUE;
    bda.bufferDeviceAddressCaptureReplay = VK_TRUE;
    as = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    as.accelerationStructure = VK_TRUE;
    as.accelerationStructureCaptureReplay = VK_TRUE;
    rt = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    rt.rayTracingPipeline = VK_TRUE;
    rt.rayTracingPipelineShaderGroupHandleCaptureReplay = VK_TRUE;
    bda.pNext = &as;
    as.pNext = &rt;
    const char *extensions[] = {VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                               VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                               VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                               VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME};
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = 0;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &bda;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 4;
    deviceInfo.ppEnabledExtensionNames = extensions;
    VkDevice device;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create device");
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.flags = VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
    bufferInfo.size = 8192;
    bufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    VkBuffer buffer;
    check(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer), "create buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryOpaqueCaptureAddressAllocateInfo addressInfo{
        VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO};
    addressInfo.opaqueCaptureAddress = opaque;
    VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT |
                  VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
    if(replay) flags.pNext = &addressInfo;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.pNext = &flags;
    allocation.allocationSize = requirements.size + 256;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
      if(requirements.memoryTypeBits & (1u << i)) { allocation.memoryTypeIndex = i; break; }
    VkDeviceMemory memory;
    if(replay)
    {
      void *occupied = mmap(reinterpret_cast<void *>(opaque), allocation.allocationSize,
                            PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0);
      require(occupied == reinterpret_cast<void *>(opaque), "reserve collision address");
      *static_cast<unsigned char *>(occupied) = 0x6d;
      require(vkAllocateMemory(device, &allocation, nullptr, &memory) ==
              VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS, "occupied address was not rejected");
      require(*static_cast<unsigned char *>(occupied) == 0x6d, "occupied memory was overwritten");
      require(munmap(occupied, allocation.allocationSize) == 0, "release collision address");
      std::cout << "PASS: occupied address rejected without overwriting memory\n";
    }
    check(vkAllocateMemory(device, &allocation, nullptr, &memory), "allocate capture memory");
    VkDeviceMemoryOpaqueCaptureAddressInfo opaqueInfo{
        VK_STRUCTURE_TYPE_DEVICE_MEMORY_OPAQUE_CAPTURE_ADDRESS_INFO};
    opaqueInfo.memory = memory;
    uint64_t restoredOpaque = vkGetDeviceMemoryOpaqueCaptureAddress(device, &opaqueInfo);
    check(vkBindBufferMemory(device, buffer, memory, 256), "bind buffer at nonzero offset");
    VkBufferDeviceAddressInfo bufferAddressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    bufferAddressInfo.buffer = buffer;
    uint64_t bufferAddress = vkGetBufferDeviceAddress(device, &bufferAddressInfo);
    auto createAS = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
    auto getASAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));
    auto destroyAS = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
    require(createAS && getASAddress && destroyAS, "AS entrypoints unavailable");
    VkAccelerationStructureCreateInfoKHR asInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    asInfo.createFlags = VK_ACCELERATION_STRUCTURE_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR;
    asInfo.buffer = buffer;
    asInfo.offset = 256;
    asInfo.size = 4096;
    asInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    asInfo.deviceAddress = expectedAS;
    VkAccelerationStructureKHR accelerationStructure;
    check(createAS(device, &asInfo, nullptr, &accelerationStructure), "create AS at captured address");
    VkAccelerationStructureDeviceAddressInfoKHR asAddressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    asAddressInfo.accelerationStructure = accelerationStructure;
    uint64_t asAddress = getASAddress(device, &asAddressInfo);
    require(restoredOpaque && bufferAddress && asAddress, "zero capture address");
    if(replay)
    {
      require(restoredOpaque == opaque && bufferAddress == expectedBuffer && asAddress == expectedAS,
              "addresses changed between capture and replay processes");
      std::cout << "PASS: memory, buffer, and AS addresses restored in independent process\n";
    }
    else
    {
      std::ofstream file(argv[2]);
      require(bool(file << restoredOpaque << ' ' << bufferAddress << ' ' << asAddress << '\n'),
              "write address file");
      std::cout << "PASS: capture addresses saved\n";
    }
    destroyAS(device, accelerationStructure, nullptr);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);

    // Deliberately create pipelines in a different order in the second process.
    // Sequential IDs happening to match is insufficient evidence of restoration.
    std::ifstream shaderFile(argv[3], std::ios::binary | std::ios::ate);
    require(bool(shaderFile), "open raygen SPIR-V");
    auto shaderSize = shaderFile.tellg();
    require(shaderSize > 0 && shaderSize % 4 == 0, "invalid SPIR-V size");
    std::vector<uint32_t> code(static_cast<size_t>(shaderSize) / 4);
    shaderFile.seekg(0);
    require(bool(shaderFile.read(reinterpret_cast<char *>(code.data()), shaderSize)), "read shader");
    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = static_cast<size_t>(shaderSize);
    moduleInfo.pCode = code.data();
    VkShaderModule module;
    check(vkCreateShaderModule(device, &moduleInfo, nullptr, &module), "create shader module");
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout;
    check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout), "create layout");
    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stage.module = module;
    stage.pName = "main";
    VkRayTracingShaderGroupCreateInfoKHR group{
        VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = 0;
    group.closestHitShader = group.anyHitShader = group.intersectionShader = VK_SHADER_UNUSED_KHR;
    VkRayTracingPipelineCreateInfoKHR pipelineInfo{
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    pipelineInfo.flags = VK_PIPELINE_CREATE_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR;
    pipelineInfo.stageCount = pipelineInfo.groupCount = 1;
    pipelineInfo.pStages = &stage;
    pipelineInfo.pGroups = &group;
    pipelineInfo.layout = layout;
    pipelineInfo.maxPipelineRayRecursionDepth = 1;
    auto createPipeline = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
        vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR"));
    auto getHandle = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
        vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR"));
    auto getCaptureHandle = reinterpret_cast<PFN_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR>(
        vkGetDeviceProcAddr(device, "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR"));
    require(createPipeline && getHandle && getCaptureHandle, "RT entrypoints unavailable");
    VkPipeline pipeline;
    for(unsigned i = 0; i < (replay ? 1u : 4u); ++i)
    {
      check(createPipeline(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                           &pipeline), "create unrelated RT pipeline");
      vkDestroyPipeline(device, pipeline, nullptr);
    }
    std::array<uint8_t, 32> captureHandle{}, originalHandle{}, actualHandle{};
    const std::string handlePath = std::string(argv[2]) + ".shader";
    if(replay)
    {
      std::ifstream file(handlePath, std::ios::binary);
      require(bool(file.read(reinterpret_cast<char *>(captureHandle.data()), captureHandle.size())
                       .read(reinterpret_cast<char *>(originalHandle.data()), originalHandle.size())),
              "read captured shader handles");
      group.pShaderGroupCaptureReplayHandle = captureHandle.data();
    }
    check(createPipeline(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                         &pipeline), "create capture/replay RT pipeline");
    check(getHandle(device, pipeline, 0, 1, actualHandle.size(), actualHandle.data()), "get shader handle");
    if(replay)
    {
      require(actualHandle == originalHandle, "shader handles changed with pipeline creation order");
      std::cout << "PASS: shader group handle restored despite different pipeline creation order\n";
    }
    else
    {
      check(getCaptureHandle(device, pipeline, 0, 1, captureHandle.size(), captureHandle.data()),
            "get capture/replay shader handle");
      std::ofstream file(handlePath, std::ios::binary);
      require(bool(file.write(reinterpret_cast<char *>(captureHandle.data()), captureHandle.size())
                       .write(reinterpret_cast<char *>(actualHandle.data()), actualHandle.size())),
              "write captured shader handles");
    }
    vkDestroyPipeline(device, pipeline, nullptr);
    if(replay)
    {
      group.pShaderGroupCaptureReplayHandle = nullptr;
      check(createPipeline(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                           &pipeline), "create new RT pipeline after restoration");
      check(getHandle(device, pipeline, 0, 1, actualHandle.size(), actualHandle.data()), "get fresh handle");
      require(actualHandle != originalHandle, "new handle collides with restored handle");
      vkDestroyPipeline(device, pipeline, nullptr);
      captureHandle[0] ^= 1;
      group.pShaderGroupCaptureReplayHandle = captureHandle.data();
      require(createPipeline(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                             &pipeline) == VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS,
              "invalid capture handle was not rejected");
      std::cout << "PASS: new IDs do not collide; invalid capture handle rejected\n";
    }
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyShaderModule(device, module, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
  }
  catch(const std::exception &error)
  {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
