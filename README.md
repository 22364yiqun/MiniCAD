# MiniCAD Qt

这是一个使用 C++20 和 Qt 6 Widgets 实现的二维 MiniCAD 项目。界面基于 `QMainWindow`、`QToolBar`、`QGraphicsView` 和 `QGraphicsScene`。

## 已实现功能

- 创建、移动、旋转、镜像和删除直线；
- 使用操作数和双十字链表实现 Undo/Redo，鼠标操作与窗口内文本命令共用历史核心；
- 网格和 XY 坐标轴；
- 画布平移和缩放；
- Qt 底部面板支持直接输入命令修改当前画布。

## 环境要求

- Windows 10/11；
- CMake 3.16 或更高版本；
- Qt 6 Widgets；
- 与 Qt SDK 匹配的 MinGW 编译器；
- CLion 2025 或其他支持 CMake 的 IDE。

当前项目使用 Qt 6.8.3 MinGW，默认路径为：

```text
qt/6.8.3/mingw_64
```

Qt SDK 不提交到 Git，新工作区需要单独安装。

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

## CLion 配置

打开项目后，在 `Settings -> Build, Execution, Deployment -> Toolchains` 中选择与 Qt 匹配的 MinGW。当前环境使用：

```text
D:/JetBrains/CLion 2025.2.4/bin/mingw
```

CMake 配置中的 Qt 路径为：

```text
CMAKE_PREFIX_PATH=C:/Users/yiqun/Desktop/Computer Graphics/homework1/qt/6.8.3/mingw_64
```

项目的 `CMakeLists.txt` 会自动查找项目内的 Qt 路径。重新加载 CMake 后，选择 `minicad_qt` 作为运行目标。

## 构建 Qt 程序

在项目根目录执行：

```powershell
cmake -S . -B cmake-build-qt-jb -G Ninja `
  "-DCMAKE_PREFIX_PATH=$PWD/qt/6.8.3/mingw_64" `
  "-DCMAKE_C_COMPILER=D:/JetBrains/CLion 2025.2.4/bin/mingw/bin/gcc.exe" `
  "-DCMAKE_CXX_COMPILER=D:/JetBrains/CLion 2025.2.4/bin/mingw/bin/g++.exe"
cmake --build cmake-build-qt-jb --target minicad_qt
```

生成文件：

```text
cmake-build-qt-jb/minicad_qt.exe
```

如果需要部署 Qt DLL：

```powershell
qt/6.8.3/mingw_64/bin/windeployqt.exe --no-translations cmake-build-qt-jb/minicad_qt.exe
```

运行程序：

```powershell
.\cmake-build-qt-jb\minicad_qt.exe
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

### CLion 使用旧构建目录

将 CMake 构建目录改为 `cmake-build-qt-jb`，并将运行目标改为 `minicad_qt`。旧的 `homework1_viewer` 已经移除。

## 目录说明

```text
src/qt_main.cpp    Qt MiniCAD 图形界面及窗口内命令输入
src/version_history.h  系统链、实体链和版本管理
tests/            历史核心与 Qt 交互测试
CMakeLists.txt     CMake 和 Qt 配置
docs/              作业要求、实现说明和课程总结
qt/                本地 Qt SDK，不提交到 Git
```

构建后可执行 `ctest --test-dir cmake-build-qt-jb --output-on-failure` 运行测试，终端 `PATH` 需包含 Qt 和 MinGW 的 `bin`。详细设计见 `docs/实现说明.md`。
