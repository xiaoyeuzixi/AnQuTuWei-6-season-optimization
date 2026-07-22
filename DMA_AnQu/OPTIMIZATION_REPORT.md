# DMA_AnQu 第一轮优化报告

生成时间：2026-07-08  
工作目录：D:\gongju\c_c++\AnQu_\DMA_AnQu - 6赛季更新 - 发布优化更新 - 发布版 - 优化 - 副本  
备份目录：D:\gongju\c_c++\AnQu_\DMA_AnQu - 6赛季更新 - 发布优化更新 - 发布版 - 优化 - 副本\_backup_before_optimize_20260708_214931

## 本轮目标

在不做大规模拆分的前提下，优先降低 CPU 空转、降低无效 DMA/渲染工作、增加线程可控退出能力，并保留现有功能行为。

## 已完成优化

### 1. 统一运行生命周期

新增：

- `DMA_AnQu/core/Runtime.h`

实现：

- `Runtime::Start()`
- `Runtime::Stop()`
- `Runtime::IsRunning()`
- `Runtime::SleepFor()`
- `Runtime::SleepUntil()`

所有 `threads/*.h` 中的 `while (true)` 已改为：

```cpp
while (Runtime::IsRunning())
```

退出 Overlay 后会先 `Runtime::Stop()`，再 `join()` 线程，避免旧版本退出时线程无法结束的问题。

### 2. 渲染线程 CPU 优化

修改：

- `DMA_AnQu/Overlay.h`
- `DMA_AnQu/main.cpp`
- `DMA_AnQu/core/Config.h`

优化点：

- 已按调试版手感调整：普通模式默认关闭 FPS 限制；低配模式开启后固定 `120 FPS`。
- 用户手动开启 FPS 限制时按 UI 上限运行；未开启 FPS 限制时不额外限帧，优先保证绘制跟手。
- Overlay 隐藏时降到 `20 FPS`，只保留热键响应。
- Overlay 隐藏时跳过 `DrawUI()`、`DrawESP()`、`DrawNearbyItems()`。
- 被踢下线时不再直接硬退出，而是通知 Runtime 停止并让主流程收尾。

### 3. 工作线程空转优化

修改线程：

- `ThreadCamera.h`
- `ThreadActors.h`
- `ThreadBoneDMA.h`
- `ThreadBones.h`
- `ThreadCombat.h`
- `ThreadEncItems.h`
- `ThreadEncPlayers.h`
- `ThreadInfo.h`
- `ThreadItems.h`
- `ThreadRadar.h`
- `ThreadThreat.h`

重点优化：

- 相机链路无效时等待从 `1ms` 提升到 `5ms`，降低加载/无效世界期间 CPU。
- 物资显示关闭时暂停物资线程并清理物资缓存。
- 加密物资线程在物资显示关闭时暂停，轮询频率降到 `100ms`。
- 骨骼 DMA 在线框/骨骼相关显示全部关闭时降频。
- 无玩家时骨骼 WorldEntry 只发布一次空快照，不再高频分配空数组。
- 雷达关闭且撤离点标记关闭时，雷达线程降频并只清理一次缓存。
- 战斗/威胁/信息线程在功能关闭时只清理一次缓存，避免反复加锁清空。

### 4. 缓存和内存分配优化

修改：

- `DMA_AnQu/main.h`
- `ThreadActors.h`
- `ThreadEncPlayers.h`
- `ThreadItems.h`

优化点：

- `ReserveContainers()` 增加更多全局 map 的预分配：
  - `playerInfo`
  - `combatData`
  - `radarData`
  - `threatData`
  - `itemPosCache`
- `ThreadActors` 的 `aliveAddrs` 改为 `thread_local` 复用，减少每轮堆分配。
- `ThreadActors` 中角色 `Mesh/PlayerState/TeamId` 改为批量 Scatter 读取，减少每个角色一次 `ExecuteScatter()` 的 DMA 往返。
- 修复 `classCache` 为空时 GC 可能对 `size()==0` 取模的问题。
- `actorPosCache` 增加预分配。
- `ThreadEncPlayers` 修复缓存 GC 时可能通过 `operator[]` 插入空值的问题。

### 5. 工程可维护性修复

修改：

- `DMA_AnQu/DMA_AnQu.vcxproj`
- `DMA_AnQu/DMA_AnQu.vcxproj.filters`

内容：

- 将 `core/Runtime.h` 加入工程和 VS 过滤器。
- 修正 `x64 Debug` 的运行库配置：`MultiThreaded` -> `MultiThreadedDebug`，Debug 构建现在可通过。

### 6. 低配专属优化方案（追加）

修改：

- `DMA_AnQu/core/Config.h`
- `DMA_AnQu/core/Config.cpp`
- `DMA_AnQu/Overlay.h`
- `DMA_AnQu/main.cpp`
- `DMA_AnQu/core/ESPDraw.h`

实现：

- UI 新增 `低配专属优化方案 / Low Spec Optimization` 开关。
- 开启后强制使用 `120 FPS`，优先级高于普通 FPS 限制。
- 开启后自动保持 `g_FpsLimitEnabled=true`、`g_FpsLimit=120`。
- 配置保存/加载新增：
  - `LowSpecMode`
  - `LowSpecFPS`
  - `FpsLimitEnabled`
  - `FpsLimit`
- 低配模式不再限制任何绘制对象：
  - 不限制真人绘制距离。
  - 不限制 AI/DrawAll 兜底类目标数量。
  - 不限制附近物资绘制数量。
  - 不额外限制物资绘制距离。
  - 不跳过 AI 骨骼、射线或雷达轨迹。
- 对比调试版后调整为更稳定的读取/渲染节奏：
  - `ThreadItems` 不再因为 `g_ShowNearbyItems` 关闭而暂停读取，但活跃节奏恢复为调试版 `50ms`，避免高频物资 DMA 抢占渲染线程。
  - `ThreadEncItems` 活跃节奏恢复为调试版 `30ms`。
  - `ThreadActors` 恢复调试版 `1ms` 节奏，保持 actor/物资请求队列跟手。
  - `ThreadBoneDMA` 恢复调试版 `5ms` 节奏，`ThreadBones` 发布节奏恢复 `1ms`。
- 普通模式默认关闭 FPS Limit，和调试版一致；低配模式仍强制 `120FPS`。Overlay 限帧路径保留绝对时间轴 + 细粒度自旋。
- 将重复的骨骼绘制代码抽成 `DrawSkeletonLines()`，降低维护成本。
- 追加物资绘制热路径优化：
  - 物资名称解析结果缓存，避免每帧对每个物资重复查表、裁剪前缀、替换下划线。
  - 物资文字宽度缓存/估算，避免每帧大量 `ImGui::CalcTextSize()`。
  - 物资文字从完整 5 层描边改为 1 层阴影 + 正文，显著减少 `AddText()` 次数。
  - 稀有度数字文字改为彩色短条，避免每个物资额外再绘制一组描边文字。
  - 先按稀有度过滤，再进行 W2S，减少无效投影计算。
  - 离屏物资增加边界剔除，避免屏幕外文字继续生成顶点。
  - `ThreadItems` 不再发布无效坐标/无稀有度条目，减少渲染线程遍历压力。

## 构建验证

已验证：

```txt
x64 Release: 通过
x64 Debug:   通过
```

生成文件：

- `x64/Release/DMA_AnQu.exe`
- `x64/Debug/DMA_AnQu.exe`

构建日志：

- `build_release_after_optimize.log`
- `build_debug_after_optimize.log`

现有第三方头文件仍有 `C4200` 零长数组 warning，来源为 `leechcore.h` / `vmmdll.h`，不影响构建。

## 预期收益

- Overlay 未限制帧率导致的 CPU 空转明显降低。
- 隐藏 Overlay 时 CPU/GPU 占用明显降低。
- 关闭物资、雷达、骨骼等功能时，对应线程不再高频工作。
- 退出流程更稳定，避免后台线程继续运行。
- 长时间运行时缓存增长更可控。

## 后续建议：第二轮优化

建议下一轮继续做：

1. 进一步把 `ThreadActors` 的类名解析做分帧/TTL 缓存。
2. `ThreadInfo` 中武器/护甲读取增加缓存和 TTL，避免重复读取静态装备。
3. `ThreadItems` 中 rarity/name 缓存改成统一缓存管理器。
4. `DrawESP()` 做渲染快照，进一步缩短持锁时间。
5. 逐步把线程实现从 `.h` 迁移到 `.cpp`。


