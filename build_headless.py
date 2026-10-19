#!/usr/bin/env python3
"""Instrument local copies; preserve the upstream example sources."""
from pathlib import Path
import subprocess

ROOT = Path('/workspace')
GENERATED = ROOT / 'headless-capture-build'
GENERATED.mkdir(exist_ok=True)
for name in ('computeheadless', 'renderheadless'):
    source = (ROOT / 'vulkan/examples' / name / (name + '.cpp')).read_text()
    source = source.replace('#include <vulkan/vulkan.h>', '''#include <vulkan/vulkan.h>
#include "/workspace/renderdoc/renderdoc/api/app/renderdoc_app.h"
#include <dlfcn.h>
static RENDERDOC_API_1_6_0 *captureAPI = nullptr;
''', 1)
    anchor = 'VK_CHECK_RESULT(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));'
    assert source.count(anchor) == 1
    source = source.replace(anchor, anchor + '''
        void *handle = dlopen("/workspace/renderdoc-build/lib/librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
        auto getAPI = handle ? (pRENDERDOC_GetAPI)dlsym(handle, "RENDERDOC_GetAPI") : nullptr;
        if(getAPI && getAPI(eRENDERDOC_API_Version_1_6_0, (void **)&captureAPI)) {
            captureAPI->SetCaptureFilePathTemplate(std::getenv("RDOC_CAPTURE_PATH"));
            captureAPI->StartFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance), nullptr);
        }
''', 1)
    anchor = '~VulkanExample()\n\t{'
    assert source.count(anchor) == 1
    source = source.replace(anchor, anchor + '''
        vkDeviceWaitIdle(device);
        if(captureAPI) captureAPI->EndFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance), nullptr);
''', 1)
    source = source.replace('std::cin.get();', '')
    generated = GENERATED / (name + '.cpp')
    generated.write_text(source)
    subprocess.run(['g++', '-std=c++20', '-O2', '-DNDEBUG',
                    '-I/workspace/sysroot/usr/include', '-I/workspace/vulkan/base',
                    '-I/workspace/vulkan/external/glm', '-I/workspace/vulkan/external/ktx/include',
                    str(generated), '-o', str(GENERATED / name),
                    '/workspace/vulkan-build/base/libbase.a',
                    '/usr/lib/x86_64-linux-gnu/libvulkan.so.1', '-ldl', '-lpthread'], check=True)
