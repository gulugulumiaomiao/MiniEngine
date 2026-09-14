# ImGui 集成

编辑器 UI 基于 Dear ImGui。本文描述 ImGui 与引擎的分层边界、帧序、overlay 契约
和 `ImGuiRenderer` 的实现选择。入口与总览见 [Editor.md](Editor.md)。

## 分层边界

```text
tools/editor/ImGuiLayer      IFrameOverlay 的唯一实现：ImGui 上下文、Win32 后端、帧序
tools/editor/ImGuiRenderer   ImDrawData -> RHI：字体图集、UI 管线、几何上传
src/render/Renderer          只认识 IFrameOverlay 接口，完全不感知 ImGui
```

- `IFrameOverlay` 是 `Renderer` 模块定义的纯虚接口，只有 `recordOverlay` 与
  `onSwapchainRecreated` 两个函数，是渲染管线结束后向同一命令缓冲区追加绘制的
  标准化扩展点。`ImGuiLayer` 是它在 `tools/editor/` 的唯一实现。
- `ImGuiRenderer` 取代官方 `imgui_impl_vulkan` 后端，绘制只经过 RHI，因此
  `tools/editor/` 里没有任何直接的 Vulkan 调用；只有 Win32 平台后端
  `imgui_impl_win32` 被 vendored 使用（`MiniImGui` 静态库）。
- 这一抽象维持了 render/rhi 的分层边界：`src/render` 不引入任何 ImGui 头文件。

## ImGuiLayer 的生命周期

| 方法 | 职责 |
|---|---|
| `attach(renderer, window)` | 创建 ImGui 上下文；配置 IO（键盘导航、Docking、ini 路径）；初始化 Win32 后端与 `ImGuiRenderer`；注册窗口消息处理器；把自己设为 renderer 的 overlay |
| `beginFrame()` | `ImGui_ImplWin32_NewFrame()` + `ImGui::NewFrame()` |
| `endFrame()` | `ImGui::Render()`，**每帧都必须调用**，即使渲染器随后跳过这一帧，否则下一次 `NewFrame` 会触发 ImGui 的 forgot-to-render 检查 |
| `recordOverlay(context)` | 帧末把 ImDrawData 经 `ImGuiRenderer` 记录到命令缓冲区 |
| `onSwapchainRecreated()` | 只在颜色格式变化时重建管线 |
| `detach()` | 清掉 Win32 消息钩子（它捕获 `this`），再销毁上下文 |

输入路径：Win32 消息通过 `Window::setMessageHandler` 转发给
`ImGui_ImplWin32_WndProcHandler`。

`attach/detach` 与项目切换强耦合：`ENGINE.openProject` 会销毁旧 renderer 重建新的，
编辑器必须先 `detach()` 再打开、打开成功后 `attach(新 renderer, window)`，详见
[ProjectManagement.md](ProjectManagement.md) 的顺序约束。

## recordOverlay 契约

`Renderer::renderFrame` 在渲染管线跑完后调用 `overlay_->recordOverlay(context)`，
叠加绘制写进同一个命令缓冲区；没有 overlay 时 Renderer 自己负责把未触碰的
backbuffer 清成合法状态。

overlay 是 present 之前的最后写入者，必须把获取到的 backbuffer 留在
`PRESENT_SRC`。实现按 `RenderContext::backBufferWritten()` 分支：

```text
无 UI 且 backbuffer 已写 -> 直接返回
否则 barrier(Undefined 或 Present -> ColorAttachment)
     beginRendering(Load 或 Clear)
     有 UI 则 ImGuiRenderer::render(encoder, drawData, frameIndex)
     endRendering
     barrier(ColorAttachment -> Present)
```

`backBufferWritten()` 由真正画进 backbuffer 的 pass 设置，不能靠“场景里有没有物体”
猜测：场景可以有对象却产不出 draw item。

## ImGuiRenderer 的实现选择

- **字体图集**：上传为 `Rgba8Unorm` 纹理 + `ClampToEdge` 采样器（nearest wrap 会让
  图集边缘字形互相渗色），随后 `ClearTexData()` 释放 CPU 侧副本。
- **ImTextureID**：是 64 位整数，直接编码 RHI 的 `BindGroupHandle`
  （`generation << 32 | index`）。活句柄的 generation 恒非零，所以被清零的
  `ImDrawCmd::TextureId` 不会与真实句柄冲突，`ImGui::Image` 无需旁路映射表即可用。
- **UI 管线**：无深度测试与写入、`CullMode::None`（ImGui 两种绕序都会发）、Alpha
  混合、`colorFormats = {swapchain 格式}`。
- **sRGB**：sRGB 附件会选 `-DSRGB_TARGET` 编出的片元变体，在片元阶段把 ImGui 的
  sRGB 样式颜色解码到线性，避免硬件写入编码把整套主题提亮；逐片元而非逐顶点，
  渐变才留在 ImGui 期望的色彩空间。
- **顶点坐标**：拷贝时把 ImGui 的 display offset/scale 折成 clip space（Vulkan 与
  ImGui 同为 y-down，无需翻转），因此这条管线不需要任何 uniform buffer——RHI 也不
  暴露 push constant。
- **几何双缓冲**：按 `FrameGpuManager::kFramesInFlight` 双缓冲，扩容带 4096 顶点/
  索引余量；重建缓冲是安全的，因为 swapchain 已经等过这一帧的 fence。
- **大 draw list**：设置 `ImGuiBackendFlags_RendererHasVtxOffset`，超过 64K 顶点的
  draw list 也能继续用 16 位索引而不被 ImGui 拆分。
- **user callback**：只支持 `ImDrawCallback_ResetRenderState`，其他 callback 记
  warn 后忽略。

## UI shader

UI shader 绕过 ShaderLab 资产管线：两段 GLSL（`tools/editor/shaders/imgui.vert`、
`imgui.frag`）用 `glslc --target-env=vulkan1.3 -O -mfmt=num`（sRGB 变体加
`-DSRGB_TARGET`）离线编译，生成的 SPIR-V 字直接内联在 `ImGuiRenderer.cpp` 的
`constexpr uint32` 数组里（uint32 保证四字节对齐），对应 GLSL 源码作为注释保留在
同一处；因此不需要 shader 文件夹或构建期编译步骤。

`onSwapchainRecreated` 只在颜色格式变化时重建管线：几何缓冲和字体图集与分辨率无关，
管线用的是动态 viewport 与 scissor。

## 相关测试

`ImGuiRendererTest` 直接编译 `tools/editor/ImGuiRenderer.cpp`（UI shader 已内联其中），
依赖 `MiniImGui`、链接普通 `MiniEngine`（UI 后端不需要编辑器变体），覆盖几何上传、
管线记录与 sRGB 变体选择。
