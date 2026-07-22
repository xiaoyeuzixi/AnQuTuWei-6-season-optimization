# DMA_AnQu

这是一个 Visual Studio 2022 / C++17 / x64 Windows 工程，包含：

- DX11 + Dear ImGui Overlay
- 多线程数据采集与渲染
- DMA/VMM/LeechCore/FTDI 依赖
- `core/` 状态、配置、解密和性能模块
- `threads/` 相机、Actor、骨骼、物资、雷达等线程模块

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
