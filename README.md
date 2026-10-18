# MiniCAD Qt

这是一个使用 C++20 和 Qt 6 Widgets 实现的二维 MiniCAD 项目。界面基于 `QMainWindow`、`QToolBar`、`QGraphicsView` 和 `QGraphicsScene`。图形界面的鼠标操作和底部文本命令共用 `src/line_model.hpp` 中的直线模型、五种命令和撤销重做历史。

## 已实现功能

- 创建、移动、旋转、镜像和删除直线；
- Undo/Redo；
- 网格和 XY 坐标轴；
- 画布平移和缩放；
- Qt 底部面板支持直接输入命令修改当前画布。

## 环境要求

- Windows 10/11；
- CMake 3.16 或更高版本；
- Qt 6 Widgets；
- 与 Qt SDK 匹配的 MinGW 编译器；
- VS Code（安装 CMake Tools 和 C/C++ 扩展）或其他支持 CMake 的 IDE。

当前项目使用 Qt 6.8.3 MinGW，默认路径为：

```text
qt/6.8.3/mingw_64
```

Qt SDK 与构建工具不提交到 Git，新工作区需要单独安装。

## 安装 Qt

可以使用 Qt 官方安装器，也可以使用 `aqtinstall`：

```powershell
winget install --id miurahr.aqtinstall --source winget
aqt install-qt windows desktop 6.8.3 win64_mingw -O qt
```

安装完成后应存在：

```text
qt/6.8.3/mingw_64/lib/cmake/Qt6/Qt6Config.cmake
```

再安装与 Qt 6.8.3 匹配的 MinGW 13.1，以及独立的 CMake 和 Ninja：

```powershell
aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 -O qt
aqt install-tool windows desktop tools_cmake qt.tools.cmake -O qt
aqt install-tool windows desktop tools_ninja qt.tools.ninja -O qt
```

## VS Code 配置

用 VS Code 打开项目根目录，安装工作区推荐的 **CMake Tools** 和 **C/C++** 扩展。项目已有 `CMakePresets.json` 和 `.vscode/` 配置，打开后选择 `vscode-debug` 预设；CMake Tools 会自动配置项目。

- `Ctrl+Shift+B`：构建项目；
- `F5`：构建并调试 `minicad_qt.exe`；
- 不调试时运行 `cmake-build-vscode/minicad_qt.exe`；
- 测试可在 CMake Tools 中运行，或在项目根目录执行下面的命令。

```powershell
& './qt/Tools/CMake_64/bin/ctest.exe' --preset vscode-debug
```

VS Code 配置使用项目内 `qt/Tools` 中的 MinGW 13.1、CMake、Ninja 和 GDB，与 Qt 6.8.3 MinGW SDK 匹配。将项目移到另一台电脑时，按上面的命令安装 Qt 和工具后即可使用。

## 构建 Qt 程序

在项目根目录执行：

```powershell
$cmake = './qt/Tools/CMake_64/bin/cmake.exe'
& $cmake --preset vscode-debug
& $cmake --build --preset vscode-debug --target minicad_qt
```

生成文件：

```text
cmake-build-vscode/minicad_qt.exe
```

如果需要部署 Qt DLL：

```powershell
qt/6.8.3/mingw_64/bin/windeployqt.exe --no-translations cmake-build-vscode/minicad_qt.exe
```

运行程序：

```powershell
.\cmake-build-vscode\minicad_qt.exe
```

## 窗口内命令

在 Qt 窗口底部输入框按 Enter 执行命令，操作直接更新当前画布：

```text
create x1 y1 x2 y2
delete id
move id dx dy
rotate id cx cy degrees
mirror id ax ay bx by
undo | redo | list | help
```

例如 `create 0 0 100 0` 创建一条直线，随后 `move 1 20 10` 移动它。直线编号可用 `list` 查看。

## 常见问题

### 找不到 Qt6Config.cmake

确认 `qt/6.8.3/mingw_64/lib/cmake/Qt6/Qt6Config.cmake` 存在。Qt 安装在其他位置时，重新配置 `CMAKE_PREFIX_PATH`。

### Qt 与编译器链接失败

Qt 的 MinGW 版本必须与 CMake 使用的编译器 ABI 匹配。当前 Qt 6.8.3 `win64_mingw` 应使用 MinGW 13.1，不要混用 MSYS2 UCRT64 GCC 16，否则可能出现 `__imp___argc` 等链接错误。

## 目录说明

想系统理解架构、各功能的实现和撤销重做的数据流，可阅读 [项目详细说明](docs/项目详细说明.md)。

```text
src/line_model.hpp  直线模型、五种命令和 History
src/qt_main.cpp    Qt MiniCAD 图形界面及窗口内命令输入
tests/qt_history_test.cpp  Qt 鼠标与文本命令交互测试
CMakeLists.txt     CMake 和 Qt 配置
docs/              作业要求、实现说明和课程总结
qt/                本地 Qt SDK，不提交到 Git
```
