# 数学库

引擎数学层位于 `src/math/Math.h`，底层使用固定版本的 GLM 1.0.3。业务代码应使用 `engine::math` 中的名称，避免直接依赖 GLM 类型名。

## 坐标与角度约定

- 使用右手坐标系。
- 所有旋转函数使用弧度。
- 投影矩阵使用 Vulkan 的 `0..1` 深度范围。
- `perspective` 与 `orthographic` 已处理 Vulkan 裁剪空间的 Y 轴方向。
- 矩阵采用列主序，组合顺序为 `Translation * Rotation * Scale`。

## 类型

- `Vec2`、`Vec3`、`Vec4`
- `Mat33`、`Mat44`
- `Quat`

这些类型支持常规加减、标量乘除、向量分量访问以及矩阵和四元数乘法。

## 常用运算

- 向量：`length`、`lengthSquared`、`dot`、`cross`、`normalize`、`lerp`、`reflect`、`project`
- 四元数：`angleAxis`、`fromEuler`、`toEuler`、`normalize`、`conjugate`、`inverse`、`slerp`、`rotate`
- 变换：`translation`、`rotation`、`scaling`、`trs`、`transformPoint`、`transformVector`
- 矩阵：`transpose`、`inverse`、`normalMatrix`
- 相机：`lookAt`、`perspective`、`orthographic`
- 辅助：`radians`、`degrees`、`nearlyEqual`

`normalize` 对零长度向量进行了保护，可以传入 fallback；`transformPoint` 使用齐次坐标 `w=1`，`transformVector` 使用 `w=0`，因此方向不会受到平移影响。
