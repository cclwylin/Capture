# Lavapipe Vulkan capture/replay 驗證成果

固定版本 Sascha Willems Vulkan 的 99 個 examples 中，96 個成功 capture 並在獨立程序用 Lavapipe replay。
95 張由 replay texture 匯出的 PNG 與原始參考 RGB 逐像素一致；`computeheadless` 沒有影像。
94 張視窗影像為 640×360；`renderheadless` 為 1024×1024。

全部 9 個 `raytracing*` 與 `rayquery` 通過。
`descriptorbuffer`、`descriptorheap`、`descriptorheapuntyped` 也已完成單 frame capture / replay / PNG 比較。

| 檔案 | 內容 |
| --- | --- |
| `vulkan_verified_captures.tar.gz` | 96 份有效 RDC 與批次結果 |
| `vulkan_verified_pngs.zip` | 95 張 replay PNG |
| 各 example 的 `.png` asset | 可單獨下載的同一張 replay PNG |
| `vulkan_verified_references.zip` | 94 份 capture-time lossless PNG 與 1 份 native headless PPM 原始參考 |
| `vulkan_descriptor_captures.zip` | 三個新增 descriptor 範例的 RDC / PNG / 參考 / log / patch |
| `vulkan_native_verification.zip` | 未修補 Mesa 26.2.5 原生六項測試的資料、三張原生截圖與 log |
| `lavapipe_renderdoc_fix.tar.gz` | 功能實作基準 `f50a9ce` 的 source helpers、patch 與驗證報告 |
| `SHA256SUMS` | 各個附件的 SHA-256 |

版本：RenderDoc **1.47**（基準 `45e91661ee457e64279ff86265abdbe214aa6be0`），
Mesa **25.0.7**（原先 93 個，含 RT/query）與 **26.2.5**（三個 descriptor 範例），LLVM **19.1.7**。
examples commit 為 `1cbc8d87b731b438f47dea00a2e083ba7d59051d`。

這些 capture 需要本 repository 的實驗修補版。
RT / query 使用 `source /workspace/Capture/env.sh`；三個 descriptor capture 使用 `source /workspace/Capture/descriptor-env.sh`。
兩個 profile 都需要 `LVP_CAPTURE_REPLAY=1`，descriptor profile 另需 `RDOC_LVP_DESCRIPTOR_REPLAY=1`。
Descriptor 做法是在 replay 重新產生 process-local native descriptor，沒有將 Mesa descriptor capture/replay feature bit 改成 true。

尚餘 `conservativeraster`、`fragmentshaderbarycentrics`、`variablerateshading` 因 Lavapipe 缺少原生 extension 失敗，沒有有效 RDC。
這三項是光柵化功能。未宣稱完整 CTS、其他平台、任意 descriptor 更新或完整 heap debugging 支援。

完整流程、軟體版本、patch 套用指令、逐項 log 與檔案 hash 請見 repository 的
`docs/WORKFLOW.zh-TW.md`、`results/software-versions.json`、`results/report.md`、`results/artifacts.json`。
