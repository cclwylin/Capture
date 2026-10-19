# RenderDoc replay on Mesa lavapipe

This cloud workspace now captures and replays the Sascha Willems Vulkan examples,
including Vulkan ray tracing. PNGs are saved from the replayed texture through
`IReplayController::SaveTexture`, rather than extracted capture thumbnails.

## Verified results

- 99 examples attempted: 93 captured and replayed successfully.
- 92 image-producing examples exported PNGs. Their RGB pixels match the original
  capture-time image exactly; the headless renderer matches its native PPM output.
- `computeheadless` executes a compute dispatch and has no colour image to export.
- All nine `raytracing*` examples and `rayquery` passed independent-process replay
  and exact RGB comparison at 640×360.
- Six examples remain unavailable, with the reasons listed below.

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
| conservativeraster | Native Mesa 25.0.7 lacks `VK_EXT_conservative_rasterization` |
| descriptorbuffer | Native extension exists, but `descriptorBufferCaptureReplay=false`; RenderDoc filters it |
| descriptorheap | Native driver lacks `VK_EXT_descriptor_heap` |
| descriptorheapuntyped | Native driver lacks descriptor heap and `VK_KHR_shader_untyped_pointers` |
| fragmentshaderbarycentrics | Native driver lacks `VK_KHR_fragment_shader_barycentric` |
| variablerateshading | Native driver lacks `VK_KHR_fragment_shading_rate` |

These are not ray-tracing failures and do not have usable RDCs in the verified set.
Replaying ray-tracing captures requires this patched driver and opt-in; stock Mesa
25.0.7 does not expose the required capture/replay features. No claim of upstream
acceptance or support on other drivers is made.

## Source baselines

| Component | Checkout | Commit |
| --- | --- | --- |
| Mesa graphics | `/workspace/mesa-graphics` | `742a20f48c59e8649533c84c4d49dd95b403f5da` (25.0.7) |
| RenderDoc | `/workspace/renderdoc` | `45e91661ee457e64279ff86265abdbe214aa6be0` |
| Sascha Willems examples | `/workspace/vulkan` | `1cbc8d87b731b438f47dea00a2e083ba7d59051d` |
| Vulkan assets submodule | `/workspace/vulkan/assets` | `a27c0e584434d59b7c7a714e9180eefca6f0ec4b` |

The old `/workspace/mesa` checkout is the unrelated Python Mesa project; it is
preserved and is not used by this graphics workflow.
