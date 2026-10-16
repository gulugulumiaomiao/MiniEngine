# TextureFormat 扩展计划

## 执行约定

每一步实施、验证并报告后暂停，用户确认后仅提交该步修改，再执行下一步。
不自动 push，不将开始前已有的未提交修改混入各步提交。

## 步骤与验收

1. **现有格式属性统一**：在 ResourceDesc.h 中维护唯一的静态格式属性表，提供块尺寸、块字节数、
   通道数、数值类型及 sRGB/depth/stencil 分类。保留现有枚举编号；现有格式的
   分类和上传行为不变。验证未知格式及 Undefined 的拒绝行为。
2. **常用非压缩格式**：增加 R8Unorm、Rg8Unorm、R16Float、Rg16Float、Rgba16Float、
   R32Float、Rg32Float、Rgba32Float、Depth16Unorm，贯通 engine/RHI/Vulkan 映射、
   创建校验和默认 view。维持旧格式编号；验证逐项映射和创建路径。
3. **推导 colorSpace**：engine 查询复用 RHI 属性表，移除 TextureDesc 和 Texture 中
   的重复 colorSpace 存储，更新构造函数、池 key、调用方及序列化。
   资产版本升为 5，不再保存 color_space；直接拒绝旧版本，不添加兼容读取或迁移。
   验证新资产往返、旧版本拒绝及从源文件重新导入。
4. **统一数据布局**：按格式属性计算 mip 偏移、尺寸和上传数据长度，校验溢出及边界。
   运行时布局支持与资产导入输出能力分开；不支持的资产编码明确拒绝。
   验证单通道、双通道、HDR、奇数尺寸、多 mip 和真实 GPU 上传/回读。
5. **设备能力及渲染验证**：查询 format/type/tiling/usage/sample 组合的支持情况，
   区分采样、过滤、附件及混合能力；不支持时明确返回失败。
   验证 RenderGraph 的 HDR/深度附件、动态渲染、传统 renderpass 和跨帧缓存。
6. **整数附件**：增加 R32Uint、相应整数清屏表示及 Vulkan 转换，检查着色器输出和
   混合约束，以整数 ID 渲染和 GPU 回读验证整条调用链。

每一步只运行与改动相关的测试；影响共享创建、同步或上传路径时执行相关回归。
每步报告实际结果及验证限制，不以枚举存在或编译通过替代运行时支持。

## 后续范围

深度加 stencil、BC 等压缩格式及对应资产导入另行实施。块属性预留其数据布局基础，
不代表当前已经支持相关纹理的创建、上传、同步或渲染。

## 第四步实现说明

- `ResourceDesc.h` 提供格式字节数及二维 mip 布局计算，检查尺寸、mip 数、对齐和整数溢出。
- CPU mip 链紧密排列；Vulkan staging 单独对齐偏移，不要求调用方填充数据。
- 运行时上传支持当前全部 14 种非压缩格式，推导 color/depth aspect；完整 mip 链可乱序提供，拒绝缺失、重复及尺寸或长度不匹配。
- `updateImage` 验证目标 mip、layer、sample、usage、区域边界及数据长度；已有内容的更新仍由调用方显式同步。
- 上传范围保持单层二维、单采样、新图像首次上传，要求 Sampled 和 TransferDestination；资产校验 `validateTextureAsset` 接受当前全部非压缩格式的原始像素，源文件导入器维持原有解码范围。
- 覆盖奇数尺寸、多 mip、溢出、非法 region，以及所有当前格式的真实 Vulkan 上传、局部更新和逐 mip 字节回读。
- 验证：完整 Debug 构建通过；开启 Vulkan 同步验证后全量 CTest 291/291 通过，涵盖 14 种格式的资产 Binary/JSON 往返、长度校验及真实 GPU 上传回读，无跳过。Release 本步未运行。
- 状态：第四步已实现，等待用户确认后提交；第五步尚未开始。
