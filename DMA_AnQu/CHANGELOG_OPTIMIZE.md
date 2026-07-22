# CHANGELOG_OPTIMIZE

## 2026-07-08 低配专属优化方案

- UI 增加 `低配专属优化方案 / Low Spec Optimization` 开关。
- 开启低配模式后 Overlay 固定以 `120FPS` 运行，优先级高于普通 FPS 限制。
- 低配模式开启后自动保持 `FPS Limit = 120`，关闭后恢复普通 FPS 控制。
- 新增低配配置保存/加载：`LowSpecMode`、`LowSpecFPS`。
- 低配模式不再限制真人、AI、DrawAll 兜底目标、附近物资的绘制数量或绘制距离。
- 低配模式不再跳过 AI 骨骼、射线或雷达轨迹，避免目标闪烁和跳帧。
- 物资读取不再受 `g_ShowNearbyItems` 显示开关限制，扫描开启后持续保持缓存热态。
- 对比调试版后恢复更稳定的线程节奏：`ThreadActors` 1ms、`ThreadItems` 50ms、`ThreadEncItems` 30ms、`ThreadBoneDMA` 5ms、`ThreadBones` 1ms，避免高频物资 DMA 抢占渲染线程。
- 普通模式默认关闭 FPS Limit，和调试版一致；只有低配模式强制 120FPS，或用户手动开启 FPS Limit 时才限帧。
- Overlay 120FPS 等待改为绝对时间轴 + 细粒度自旋，移除尾段 `Sleep(1)` 引起的明显顿挫。
- 优化物资绘制热路径：缓存物资名称/宽度，物资文字改为轻量阴影，稀有度数字改为彩色短条，先过滤稀有度再 W2S，并跳过离屏/无效物资。
- 抽取 `DrawSkeletonLines()`，去掉真人/AI 骨骼绘制重复代码。
- Release/Debug x64 构建通过。

## 2026-07-08 第一轮低风险优化

- 新增 core/Runtime.h，统一线程生命周期。
- 所有工作线程循环接入 Runtime::IsRunning()。
- Throttler 改为 steady_clock + 可唤醒休眠。
- Overlay 主循环增加 240FPS 安全上限，隐藏时降到 20FPS。
- 默认 FPS 限制改为开启，默认 144FPS。
- Overlay 隐藏时跳过 UI/ESP/物资绘制。
- 物资、雷达、威胁、战斗、信息、骨骼线程增加功能关闭时的降频/暂停逻辑。
- 增加全局缓存 reserve，减少运行期 rehash。
- 修复 ThreadActors classCache 空 GC 风险。
- 修复 ThreadBoneDMA 中被乱码注释吞掉的函数声明/cleanupCounter 声明问题。
- 修复 x64 Debug 运行库配置，Debug/Release 均可构建。



- ThreadActors 角色 Mesh/PlayerState/TeamId 改为批量 Scatter 读取，减少 DMA 往返。
