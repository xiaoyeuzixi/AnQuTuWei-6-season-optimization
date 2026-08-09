# 暗区突围 UAGame.exe_DMA | 第 6 赛季优化版

> ## 交流群：835775657
> 欢迎加入交流群，获取版本更新、问题交流与使用反馈。

面向《暗区突围》`UAGame.exe` 的 DMA 读取工程，基于 Visual Studio 2022 / C++17 / x64 Windows 构建。本版本针对第 6 赛季进行更新与发布优化，包含：

- DX11 + Dear ImGui Overlay
- 多线程数据采集与渲染
- DMA/VMM/LeechCore/FTDI 依赖
- `core/` 状态、配置、解密和性能模块
- `threads/` 相机、Actor、骨骼、物资、雷达等线程模块

## 主要内容

- 面向 `UAGame.exe` 的进程定位与 DMA 读取流程
- 针对暗区突围第 6 赛季的偏移、状态与性能优化
- DX11 + Dear ImGui 的可视化与交互界面

## 构建

使用 Visual Studio 2022 打开 `DMA_AnQu.sln`，优先选择 `Debug|x64` 或 `Release|x64`。
运行时需要项目配套的 x64 DLL、VC++ 运行库、相关驱动和目标运行环境。
如需启用服务器验证，请设置环境变量 `DMA_ANQU_API_KEY`，不要把密钥直接写入源码。

## 目录约定

- `DMA_AnQu/`：源码与工程文件
- `PhysX-4.1/`：PhysX 相关头文件
- `ImGui/`：Dear ImGui 源码
- `.gitignore`：排除 VS 缓存、构建产物、备份和本地配置

## 版本管理

主分支为 `main`。每次修改请使用独立提交，必要时通过 `git revert` 回滚。


