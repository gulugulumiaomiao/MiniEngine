# ImGui 集成

编辑器 UI 基于 Dear ImGui。本文描述 ImGui 与引擎的分层边界、帧序、overlay 契约
和 `ImGuiRenderer` 的实现选择。入口与总览见 [Editor.md](Editor.md)。

## 分层边界

```text
tools/editor/backend/ImGuiLayer      IFrameOverlay 的唯一实现：ImGui 上下文、Win32 后端、帧序
tools/editor/backend/ImGuiRenderer   ImDrawData -> RHI：字体图集、UI 管线、几何上传
tools/editor/backend/ImGuiStyleConfig  imgui_style.json 的解析与应用：字体 + 全部 ImGuiStyle 显示字段
src/render/Renderer                  只认识 IFrameOverlay 接口，完全不感知 ImGui
```

- `IFrameOverlay` 是 `Renderer` 模块定义的纯虚接口，只有 `recordOverlay` 与
  `onSwapchainRecreated` 两个函数，是渲染管线结束后向同一命令缓冲区追加绘制的
  标准化扩展点。`ImGuiLayer` 是它在 `tools/editor/` 的唯一实现。
- `ImGuiRenderer` 取代官方 `imgui_impl_vulkan` 后端，绘制只经过 RHI，因此
  `tools/editor/` 里没有任何直接的 Vulkan 调用；只有 Win32 平台后端
  `imgui_impl_win32` 被 vendored 使用（`MiniImGui` 静态库）。
- `ImGuiStyleConfig` 编入 `MiniEditor`（依赖 imgui 与引擎 JSON 序列化，不进
  `MiniEngineEditor` 库），负责 `imgui_style.json` 的读、默认文档生成与应用。
- 这一抽象维持了 render/rhi 的分层边界：`src/render` 不引入任何 ImGui 头文件。

## ImGuiLayer 的生命周期

| 方法 | 职责 |
|---|---|
| `attach(renderer, window)` | 创建 ImGui 上下文；配置 IO（键盘导航、Docking、ini 路径）；加载 `imgui_style.json` 并应用字体与样式（见下节）；初始化 Win32 后端与 `ImGuiRenderer`；注册窗口消息处理器；把自己设为 renderer 的 overlay |
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

## 样式与字体配置（imgui_style.json）

ImGui 的字体与全部 `ImGuiStyle` 显示参数收在独立的手调文件
`<CWD>/editor/config/imgui_style.json`（`EditorApplication` 构造时把路径设给
`ImGuiLayer::setStylePath`）。文件缺失时 `ImGuiStyleConfig::load` 会生成一份
全字段默认文档供手改参考；它是纯手工配置，引擎退出时的配置写回不会触碰它。

```json
{
  "schema_version": 1,
  "font": {
    "file": "../../fonts/NotoSansSC-Regular.ttf",
    "size": 16.0,
    "glyph_ranges": "chinese-common",
    "oversample_h": 2, "oversample_v": 1, "pixel_snap_h": true
  },
  "style": { "window_padding": [9, 9], "scrollbar_size": 17.0, "...": "ImGuiStyle 全部字段，蛇形命名" },
  "colors": { "Text": [1, 1, 1, 1], "WindowBg": [0.06, 0.06, 0.06, 0.94] }
}
```

合成语义在 `load` 内一次做完，`apply` 只是拷贝样式 + 装字体：

1. 起点 = ImGui 工厂 `ImGuiStyle` + `StyleColorsDark` 调色板；
2. `ScaleAllSizes(size / 13.0f)`——13 是内置 ProggyClean 位图字体的字号，默认
   样式的间距与控件尺寸都按它调配，比例缩放让 16px 中文下的布局自动协调。
   `ScaleAllSizes` 用 ImTrunc 截断，且不缩放 Alpha/DisabledAlpha/抗锯齿开关等
   非尺寸字段；
3. `style` / `colors` 段中**显式出现**的字段最后覆盖。

读取是宽容的：字段或段落缺失、类型不符、颜色名未知都只是保留该字段的当前值，
手写半份 JSON 合法；JSON 语法损坏或 `schema_version` 不认识则整份按默认
（16px + 比例缩放）启动并保留原文件。字段读写共用同一张静态表（`ImGuiStyleConfig.cpp`
的 float/Vec2/bool/Dir 四张成员指针表），新字段不会读写漂移。

字体规则：

- `font.file` 绝对路径直接用；相对路径按 **imgui_style.json 自身所在目录**解析
  （不能按进程 CWD：引擎 initialize 会把 CWD 改写到 engine.json 的
  `working_directory`，attach 晚于它执行）。默认的 `../../fonts/NotoSansSC-Regular.ttf`
  从 `editor/config/` 上两级指向可执行文件旁的 `fonts/`，由构建目标
  `MiniCopyEditorFonts` 从 `third_party/fonts/` 拷贝（Noto Sans SC，SIL OFL 许可，
  许可文件与字体同目录）；文档里保留相对形式，`load` 解析成绝对路径供 `apply` 使用。
- imgui 未集成 FreeType，stb_truetype 只认 TrueType 轮廓的 `.ttf`；CFF 轮廓的
  `.otf`（如思源黑体 OTF 版）会被加载前的魔数预检拒绝。任何加载失败（缺失、
  非 TrueType、解析失败）都回退 `AddFontDefault` + 未缩放的工厂样式，保证字体
  与比例一致，编辑器照常启动。
- `glyph_ranges`：`"default"`（基本拉丁）或 `"chinese-common"`（常用简体约 2500 字
  + ASCII + 假名，ImGui 内置范围表）。

生效时机：`attach()` 在 `CreateContext()` 之后、字体图集构建（renderer 初始化里的
`GetTexDataAsRGBA32`）之前执行 load + apply；项目切换会 detach→attach 重建 ImGui
上下文并**重读文件**——手改 JSON 后切换项目即可生效，无需重启进程。

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
     有 UI 则 ImGuiRenderer::render(commandBuffer, drawData, frameIndex)
     endRendering
     barrier(ColorAttachment -> Present)
```

`backBufferWritten()` 由真正画进 backbuffer 的 pass 设置，不能靠“场景里有没有物体”
猜测：场景可以有对象却产不出 draw item。

## ImGuiRenderer 的实现选择

- **字体图集**：上传为 `Rgba8Unorm` 纹理 + `ClampToEdge` 采样器（nearest wrap 会让
  图集边缘字形互相渗色），随后 `ClearTexData()` 释放 CPU 侧副本。
- **ImTextureID**：是 64 位整数，直接编码 RHI 的 `BindGroupHandle`
  （`generation << 32 | index`）。Scene View 使用 generation 为零的保留 ID `1`，
  在录制时解析到实际获取帧的场景 BindGroup。活句柄的 generation 恒非零，所以被清零的
  `ImDrawCmd::TextureId` 不会与真实句柄冲突，普通纹理无需旁路映射表即可用。
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

`ImGuiRendererTest` 直接编译 `tools/editor/backend/ImGuiRenderer.cpp`（UI shader 已内联其中），
依赖 `MiniImGui`、链接普通 `MiniEngine`（UI 后端不需要编辑器变体），覆盖几何上传、
管线记录与 sRGB 变体选择。

`ImGuiStyleConfigTest` 同样直接编译 `tools/editor/backend/ImGuiStyleConfig.cpp`，
覆盖默认文档生成与 round-trip、半份 JSON 的宽容读取、字号比例缩放（尺寸动、
Alpha 不动）、相对字体路径按文件目录解析、单项颜色覆盖、损坏 JSON 回退，以及
真实 TTF 装载（源码树里的 Noto Sans SC，含 CJK 字形光栅化验证）与不可用字体
（含默认相对路径落空）的工厂回退。

## 场景离屏输出与帧序

编辑器 attach 后启用离屏场景模式，beginFrame 把请求尺寸归零，由可见的
SceneViewPanel 提交本帧尺寸。detach 在 GPU idle 后释放 UI BindGroup，并恢复默认
swapchain 输出；项目切换继续遵循先 detach、再 open/close、再 attach 的顺序。

```text
Application::onUpdate：生成 UI，提交 Scene View 的像素尺寸
  -> Scene::buildRenderScene：使用 Renderer::sceneAspectRatio
  -> swapchain.beginFrame：获取实际帧并等待该 slot 的 fence
  -> 准备该 slot 的颜色与深度目标（尺寸变化时重建）
  -> 场景管线：离屏颜色附件 -> ShaderRead
  -> recordOverlay：绑定实际 slot 的场景纹理，清理主窗口并绘制 UI
  -> backbuffer -> Present
```

颜色目标使用 swapchain 的颜色格式与 Sampled usage。sRGB 目标写入时编码，ImGui
采样时由硬件解码，随后主窗口附件编码，保持原来的场景色彩。场景目标不会设置
backBufferWritten；该标记仍只描述真正的主窗口写入。

ImGuiRenderer 按双帧缓存场景纹理 BindGroup，比较包含 generation 的完整 view 句柄，
重建仅发生于相应 fence 完成后，shutdown 释放全部绑定。零尺寸时跳过场景管线，
overlay 继续保证合法的主窗口清屏与 Present。ImGuiRendererTest 已迁移为 GoogleTest。