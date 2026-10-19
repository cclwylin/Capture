#!/usr/bin/env bash
set -euo pipefail
cd /workspace/Capture
source ./env.sh
export PKG_CONFIG_SYSROOT_DIR=/workspace/sysroot
export PKG_CONFIG_PATH=/workspace/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:/workspace/sysroot/usr/share/pkgconfig

# The cloud snapshot retains the source checkouts and extracted Debian tools.
# Check the baselines rather than resetting a checkout containing user changes.
test "$(git -C /workspace/mesa-graphics rev-parse HEAD)" = 742a20f48c59e8649533c84c4d49dd95b403f5da
test "$(git -C /workspace/renderdoc rev-parse HEAD)" = 45e91661ee457e64279ff86265abdbe214aa6be0
test "$(git -C /workspace/vulkan rev-parse HEAD)" = 1cbc8d87b731b438f47dea00a2e083ba7d59051d
apply_if_needed() {
  local checkout="$1" patch_file="$2"
  if git -C "$checkout" apply --reverse --check "$patch_file" 2>/dev/null; then
    return
  fi
  git -C "$checkout" apply --check "$patch_file"
  git -C "$checkout" apply "$patch_file"
}
apply_if_needed /workspace/mesa-graphics /workspace/Capture/patches/lavapipe-capture-replay.patch
apply_if_needed /workspace/renderdoc /workspace/Capture/patches/renderdoc-replay-export-png.patch

if [ ! -f /workspace/mesa-build/build.ninja ]; then
  python3 -m mesonbuild.mesonmain setup /workspace/mesa-build /workspace/mesa-graphics \
    --native-file /workspace/Capture/mesa-native.ini --buildtype=release -Db_ndebug=true \
    -Dgallium-drivers=llvmpipe -Dvulkan-drivers=swrast -Dplatforms=x11 \
    -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Degl=disabled -Dglx=disabled \
    -Dgbm=disabled -Dllvm=enabled -Dshared-llvm=enabled -Dgallium-vdpau=disabled \
    -Dgallium-va=disabled -Dvideo-codecs= -Dbuild-tests=false \
    -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled
fi
ninja -C /workspace/mesa-build -j4 src/gallium/targets/lavapipe/libvulkan_lvp.so
ninja -C /workspace/renderdoc-build -j4 renderdoccmd
ninja -C /workspace/vulkan-xcb-build -j4
ninja -C /workspace/vulkan-build -j4 base/libbase.a
g++ -shared -fPIC -std=c++17 -DRENDERDOC_PLATFORM_LINUX=1 \
  -I/workspace/sysroot/usr/include -I/workspace/renderdoc/renderdoc/api \
  tools/frame_capture_hook.cpp -o /workspace/libframe_capture_hook.so \
  -L/workspace/renderdoc-build/lib -Wl,-rpath,/workspace/renderdoc-build/lib -lrenderdoc -ldl
g++ -std=c++17 -I/workspace/sysroot/usr/include tests/capture_replay_probe.cpp \
  /usr/lib/x86_64-linux-gnu/libvulkan.so.1 -o /workspace/capture_replay_probe
glslangValidator --target-env vulkan1.2 -V tests/noop.rgen -o /workspace/noop.rgen.spv
python3 build_headless.py
