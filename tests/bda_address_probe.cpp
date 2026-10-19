// Independent-process address restoration and no-overwrite check for Mesa 26.2.5.
#include <vulkan/vulkan.h>
#include <sys/mman.h>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool value, const char *message)
{
  if(!value) throw std::runtime_error(message);
}
static void check(VkResult result, const char *message)
{
  if(result != VK_SUCCESS)
    throw std::runtime_error(std::string(message) + ": " + std::to_string(result));
}
int main(int argc, char **argv)
{
  try
  {
    require(argc == 3, "usage: bda_address_probe capture|replay address-file");
    bool replay = std::string(argv[1]) == "replay";
    require(replay || std::string(argv[1]) == "capture", "invalid mode");
    uint64_t opaque = 0, expected = 0;
    if(replay)
    {
      std::ifstream input(argv[2]);
      require(bool(input >> opaque >> expected), "invalid address file");
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    VkInstance instance;
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "create instance");
    uint32_t count = 1;
    VkPhysicalDevice physical;
    check(vkEnumeratePhysicalDevices(instance, &count, &physical), "enumerate device");
    VkPhysicalDeviceBufferDeviceAddressFeatures bda{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &bda;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    require(bda.bufferDeviceAddressCaptureReplay, "capture replay unavailable without opt-in");
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &bda;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queue;
    VkDevice device;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "create device");
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.flags = VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
    bufferInfo.size = 8192;
    bufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer buffer;
    check(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer), "create buffer");
    VkMemoryOpaqueCaptureAddressAllocateInfo addressInfo{
        VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO};
    addressInfo.opaqueCaptureAddress = opaque;
    VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
    flags.pNext = &addressInfo;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.pNext = &flags;
    allocation.allocationSize = 16384;
    VkDeviceMemory memory;
    if(replay)
    {
      void *sentinel = mmap(reinterpret_cast<void *>(opaque), 16384, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
      require(sentinel == reinterpret_cast<void *>(opaque), "reserve occupied address");
      *static_cast<uint64_t *>(sentinel) = UINT64_C(0x123456789abcdef0);
      VkResult result = vkAllocateMemory(device, &allocation, nullptr, &memory);
      require(result == VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS, "occupied address was not rejected");
      require(*static_cast<uint64_t *>(sentinel) == UINT64_C(0x123456789abcdef0), "sentinel overwritten");
      munmap(sentinel, 16384);
    }
    check(vkAllocateMemory(device, &allocation, nullptr, &memory), "allocate captured memory");
    check(vkBindBufferMemory(device, buffer, memory, 4096), "bind at nonzero offset");
    VkDeviceMemoryOpaqueCaptureAddressInfo memoryInfo{
        VK_STRUCTURE_TYPE_DEVICE_MEMORY_OPAQUE_CAPTURE_ADDRESS_INFO};
    memoryInfo.memory = memory;
    VkBufferDeviceAddressInfo bufferAddressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    bufferAddressInfo.buffer = buffer;
    uint64_t actualOpaque = vkGetDeviceMemoryOpaqueCaptureAddress(device, &memoryInfo);
    uint64_t actualBuffer = vkGetBufferDeviceAddress(device, &bufferAddressInfo);
    require(actualOpaque && actualBuffer == actualOpaque + 4096, "incorrect buffer binding offset");
    if(replay)
      require(actualOpaque == opaque && actualBuffer == expected, "address changed in new process");
    else
    {
      std::ofstream output(argv[2]);
      output << actualOpaque << ' ' << actualBuffer << '\n';
      require(bool(output), "write addresses");
    }
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    std::cout << "PASS: " << (replay ? "restored addresses, binding offset and collision rejection" : "saved capture addresses") << '\n';
    return 0;
  }
  catch(const std::exception &error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
