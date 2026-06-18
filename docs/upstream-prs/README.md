# Upstream PR drafts for bgfx

Drafts of fixes we maintain locally as `patches/bgfx_*.patch` that look worth
proposing to bgfx upstream.  These are intended for the integration team to
review and submit as separate PRs to https://github.com/bkaradzic/bgfx.

Each draft contains a PR title/commit message, problem statement, the patch
itself (anchored against bgfx master, not against our local patched tree),
alternatives considered, and a test plan.

## Drafts

- [`bgfx-treat-suboptimal-as-success.md`](./bgfx-treat-suboptimal-as-success.md) — split `VK_SUBOPTIMAL_KHR` from `VK_ERROR_OUT_OF_DATE_KHR` and treat the soft-hint as success.  Pixel 9 / Android 16 returns it every frame, causing a per-frame swapchain rebuild loop.  Standard pattern in Unreal/Unity/Godot.
- [`bgfx-shadow-requested-resolution.md`](./bgfx-shadow-requested-resolution.md) — shadow `m_lastRequestedWidth/Height` in `RendererContextVK` so the next-frame "did the app ask for a different size" comparison compares request-vs-request, not clamped-actual-vs-request.  Closes a separate rebuild loop on devices where the driver permanently clamps the requested swapchain extent (display-cutout devices).

## Upstream check status

Verified against bgfx master `HEAD` (2026-06-18 snapshot): both fixes are still
absent upstream.  Recent upstream commits worth re-checking at our next bgfx
version bump:

- `1d8f6d8` (2026-06-17) "Vulkan: do not block acquiring window swap chain images"
- `edbec2f` (2026-06-17) "Vulkan: recreate window swap chains flagged for recreation"

Neither names SUBOPTIMAL or the resolution comparison; the case blocks our
patches modify still match upstream master byte-for-byte at the time of writing.
But they touch nearby swapchain-recreation logic — re-read those diffs at bump
time before re-applying our patches.

## Not in this drafts directory

The other four bgfx patches we maintain (`bgfx_emulator_compat`,
`bgfx_mali_shadow_fix`, `bgfx_imgui_default_font`, `bgfx_android_mailbox_present`)
are also still needed but either narrower in scope (`mailbox` is a one-line
reorder we may upstream later) or environment-specific (`imgui_default_font` is
a workaround for an arm64 macOS codegen bug in the bgfx example tree, unlikely
to be wanted upstream).  These can be drafted later if there's appetite; for
now the SUBOPTIMAL + clamped-resolution pair are the highest-value upstream
proposals.
