# RenderDoc replay on Mesa lavapipe

完整繁體中文紀錄：[工作流程、各軟體版本、patch 套用、建置、capture/replay 與驗證結果](docs/WORKFLOW.zh-TW.md)。

| 文件 / 成果 | 位置 |
| --- | --- |
| 實際版本、來源 commit、建置參數、執行檔 hash | [software-versions.json](results/software-versions.json) |
| 四份修補檔 | [patches/](patches/) |
| 99 個範例的結果與文字 log | [驗證報告](results/report.md) |
| 95 張 replay PNG 的下載索引 | [PNG 索引](results/gallery.md) |
| RDC / PNG 的檔案大小與 SHA-256 | [artifacts.json](results/artifacts.json) |
| RDC / PNG Release 的發佈狀態 | [release.json](results/release.json) |

二進位 RDC、PNG 與原始參考以 GitHub Release 提供；`main` 保存流程、版本、patch、helper、manifest 與文字證據。

This cloud workspace now captures and replays the Sascha Willems Vulkan examples,
including Vulkan ray tracing. PNGs are saved from the replayed texture through
`IReplayController::SaveTexture`, rather than extracted capture thumbnails.

## Verified results

- 99 examples attempted: 96 captured and replayed successfully.
- 95 image-producing examples exported PNGs. Their RGB pixels match the original
  capture-time image exactly; the headless renderer matches its native PPM output.
- `computeheadless` executes a compute dispatch and has no colour image to export.
- All nine `raytracing*` examples and `rayquery` passed independent-process replay
  and exact RGB comparison at 640×360.
- `descriptorbuffer`, `descriptorheap`, and `descriptorheapuntyped` now each have
  one complete captured frame, independent-process replay, and an exact RGB PNG.
- Three examples remain unavailable, with the reasons listed below.

See `/workspace/verified-batch/report.md` and `results.json` for per-example
results, actual exit codes, timings, images, and capture paths.

## What changed

`patches/lavapipe-capture-replay.patch` adds an **experimental opt-in Linux 64-bit**
implementation of the driver features RenderDoc requires:

- `bufferDeviceAddressCaptureReplay`: capture-address memory uses page-aligned
  anonymous mappings. Replay requests the recorded address with
  `MAP_FIXED_NOREPLACE`; conflicting mappings are rejected without overwrite.
  `vkGetDeviceMemoryOpaqueCaptureAddress` returns the address for those mappings.
- `accelerationStructureCaptureReplay`: restoring backing memory restores the AS
  address. Mesa's existing AS creation path checks the requested address.
- `rayTracingPipelineShaderGroupHandleCaptureReplay`: group IDs are serialized in
  a versioned opaque handle and restored before shader compilation. New IDs advance
  past restored IDs; invalid handles are rejected.

Enable this implementation with `LVP_CAPTURE_REPLAY=1`. It stays disabled by
default. Mixed captured/uncaptured shader handles, external-memory combinations,
other platforms, and full Vulkan CTS conformance are not claimed.

`patches/renderdoc-replay-export-png.patch` adds:

```sh
renderdoccmd replay --export-png output.png capture.rdc
renderdoccmd replay --verify capture.rdc
```

These modes replay through the final event, check fatal replay errors, and exit
with a meaningful status. Empty/initialization-only captures fail validation.
The PNG mode chooses the final presented image or colour attachment and reads it
back after replay; the verify mode also handles compute-only captures.

`tools/frame_capture_hook.cpp` triggers one complete frame at a presentation
boundary. `run_batch.py` runs capture and replay in separate processes, explicitly
selects the patched lavapipe ICD, saves lossless capture-time reference images via
RenderDoc's supported `Capture.IncludeExtendedThumbnail` setting, and compares
the resulting RGB pixels. References and replay exports have separate paths.

The culling/LOD sample needs buffered UBOs to initialize before capture because
it submits asynchronous compute before its graphics path updates the UBO. It is
captured at frame 4; other windowed examples use frame 1. Each RDC holds one frame.
Headless examples are instrumented in generated copies; upstream sources stay intact.

## Descriptor examples on Mesa 26.2.5

The three descriptor examples use a separate Mesa 26.2.5 lavapipe profile. The
original 93 captures continue to use the Mesa 25.0.7 profile, including its ray
tracing capture/replay patch. `run_batch.py` selects the profile for each example.

`patches/lavapipe-26.2.5-bda-capture-replay.patch` ports the opt-in Linux 64-bit BDA
address restoration to the current driver's memory object layout. It preserves
addresses in an independent process and rejects an occupied address without
overwriting the existing mapping. The native baseline driver is retained at
`/workspace/lavapipe-26.2.5-native/libvulkan_lvp.so` for the earlier native tests.

`patches/renderdoc-lavapipe-descriptors.patch` adds an experimental lavapipe path
enabled with `RDOC_LVP_DESCRIPTOR_REPLAY=1`:

- Record descriptor descriptions and their capture-time bytes; regenerate native
  descriptor bytes in the replay process and replace matching bytes within bound
  descriptor buffer/heap memory ranges before upload.
- Record/replay resource and sampler descriptor writes, heap binds, push data,
  shader set/binding mappings, and the untyped-pointers feature structure.
- Keep images, views, samplers, and indirect buffer dependencies in the capture.
  Heap draws execute the native `VK_EXT_descriptor_heap` commands and original
  SPIR-V shaders, including the untyped-pointers example.
- Check descriptor sizes and ambiguous byte patterns. Heap replay without the
  experimental mode fails with an explanatory error.

This path does not enable Mesa's `descriptorBufferCaptureReplay` or
`descriptorHeapCaptureReplay` feature bits. It regenerates the descriptors whose
CPU/JIT pointers cannot be reused across processes. The opt-in extension exposure
is limited to the lavapipe driver. Verification covers these examples' CPU-written
descriptor buffers and full-frame replay/PNG export; GPU-written or transformed
descriptors, arbitrary descriptor updates, partial replay state restoration,
heap inspection, shader debugging, other drivers, and Vulkan CTS conformance are
not claimed. Heap descriptor-write pNext chains are rejected in this prototype.

```sh
cd /workspace/Capture
./rebuild.sh
./start.sh
python3 run_batch.py --refresh descriptorbuffer descriptorheap descriptorheapuntyped
./verify_descriptors.sh
source ./descriptor-env.sh
renderdoccmd replay --export-png /workspace/descriptorheap.png \
  /workspace/verified-batch/captures/descriptorheap_frame1.rdc
```

The descriptor verification also checks BDA address restoration with a nonzero
binding offset, occupied-address rejection, opt-in disabled behavior, and replay
rejection without descriptor regeneration.

## Run in the prepared cloud snapshot

```sh
cd /workspace/Capture
./rebuild.sh
./start.sh
./verify.sh
source ./env.sh
renderdoccmd replay --export-png /workspace/raytracing-reflections.png \
  /workspace/verified-batch/captures/raytracingreflections_frame1.rdc
```

`start.sh` starts Xvfb when needed, tests the X connection, and renders a short
triangle benchmark on lavapipe. Processes must be restarted after environment
restoration. `env.sh` selects the driver explicitly and clears capture injection
variables before replay. Capture injection is scoped to each child process.

To recapture and verify the whole collection:

```sh
python3 /workspace/Capture/run_batch.py --refresh
python3 /workspace/Capture/report.py
python3 /workspace/Capture/tools/publish_results.py
```

The scripts use the tools and dependencies retained in `/workspace/sysroot` and
`/workspace/mesa-python`; they are tested against this prepared snapshot, not a
fresh operating-system install. Mesa was built with Meson, LLVM 19 shared
libraries, `vulkan-drivers=swrast`, and `gallium-drivers=llvmpipe`. RenderDoc uses
its Vulkan command-line build, with Qt/Python and OpenGL disabled. The extracted
Debian development dependencies include LLVM 19, libdrm, XCB, bison, flex, zlib,
libelf, and glslang-tools. Python generators use Meson, Mako, packaging, and PyYAML.

## Integration checks

`tests/capture_replay_probe.cpp` runs in two independent processes. It verifies
memory/buffer/AS addresses, including nonzero binding offsets; rejects occupied
addresses without overwriting a sentinel; restores shader handles with a different
pipeline creation order; checks new IDs do not collide; and rejects invalid handles.
`verify.sh` additionally checks opt-in is required, rejects a legacy initialization
capture, replays all ten ray tracing/query captures, and compares every RGB pixel.

## Remaining limitations

| Example | Observed blocker |
| --- | --- |
| conservativeraster | Native lavapipe, also tested on Mesa 26.2.5, lacks `VK_EXT_conservative_rasterization` |
| fragmentshaderbarycentrics | Native lavapipe, also tested on Mesa 26.2.5, lacks `VK_KHR_fragment_shader_barycentric` |
| variablerateshading | Native lavapipe, also tested on Mesa 26.2.5, lacks `VK_KHR_fragment_shading_rate` |

These are not ray-tracing failures and do not have usable RDCs in the verified set.
Replaying ray-tracing captures requires this patched driver and opt-in; stock Mesa
25.0.7 does not expose the required capture/replay features. No claim of upstream
acceptance or support on other drivers is made.

## Source baselines

| Component | Checkout | Commit |
| --- | --- | --- |
| Mesa graphics | `/workspace/mesa-graphics` | `742a20f48c59e8649533c84c4d49dd95b403f5da` (25.0.7) |
| Mesa descriptor profile | `/workspace/mesa-26.2.5` | `ddaf85e353442d9d7f192fdcf1823e652e3f2ee3` (26.2.5) |
| RenderDoc | `/workspace/renderdoc` | `45e91661ee457e64279ff86265abdbe214aa6be0` |
| Sascha Willems examples | `/workspace/vulkan` | `1cbc8d87b731b438f47dea00a2e083ba7d59051d` |
| Vulkan assets submodule | `/workspace/vulkan/assets` | `a27c0e584434d59b7c7a714e9180eefca6f0ec4b` |

The old `/workspace/mesa` checkout is the unrelated Python Mesa project; it is
preserved and is not used by this graphics workflow.
