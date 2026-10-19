// Trigger exactly one complete frame at the first presentation boundary.
// Initialization submits are intentionally excluded.
#include <vulkan/vulkan.h>
#include "/workspace/renderdoc/renderdoc/api/app/renderdoc_app.h"
#include <replay/renderdoc_replay.h>
#include <replay/structured_data.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cstdio>
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *info)
{
  static auto present = reinterpret_cast<PFN_vkQueuePresentKHR>(dlsym(RTLD_NEXT, "vkQueuePresentKHR"));
  static bool triggered = false;
  static unsigned presentations = 0;
  const char *after = std::getenv("RDOC_CAPTURE_AFTER");
  const unsigned captureAfter = after ? std::strtoul(after, nullptr, 10) : 1;
  if(!present) return VK_ERROR_INITIALIZATION_FAILED;
  if(!triggered && ++presentations >= captureAfter)
  {
    triggered = true;
    auto handle = dlopen("/workspace/renderdoc-build/lib/librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    auto getAPI = handle ? reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(handle, "RENDERDOC_GetAPI")) : nullptr;
    RENDERDOC_API_1_6_0 *api = nullptr;
    if(getAPI && getAPI(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void **>(&api)))
    {
      if(std::getenv("RDOC_LOSSLESS_REFERENCE"))
      {
        SDObject *setting = RENDERDOC_SetConfigSetting("Capture.IncludeExtendedThumbnail");
        if(setting) setting->data.basic.b = true;
      }
      const char *path = std::getenv("RDOC_CAPTURE_PATH");
      if(path) api->SetCaptureFilePathTemplate(path);
      api->TriggerCapture();
      std::fprintf(stderr, "Queued one RenderDoc frame capture at presentation boundary.\n");
    }
    else std::fprintf(stderr, "RenderDoc capture API unavailable.\n");
  }
  return present(queue, info);
}
