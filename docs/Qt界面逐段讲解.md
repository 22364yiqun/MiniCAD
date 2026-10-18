# `qt_main.cpp` 逐段讲解：从 Qt 入门到项目交互

这份文档专门解释 `src/qt_main.cpp`。它面向第一次接触 Qt Widgets 的读者，建议一边阅读一边按文中的行号查看源码。先理解 **Qt 怎样把鼠标和按钮事件交给程序**，再理解本项目如何把这些事件转换成直线命令。

## 1. 先建立一个整体印象

`qt_main.cpp` 主要定义了两个类：

```text
MainWindow：应用窗口和各种控件
  ├─ 工具栏：选择、画线、移动、旋转、镜像、删除、撤销、重做
  ├─ 底部命令面板：日志、提示、输入框
  ├─ 状态栏
  └─ CadView：中央绘图区域

CadView：绘图区域和交互控制器
  ├─ QGraphicsScene：当前显示的图形项
  ├─ LineModel：真正的直线数据
  ├─ History：撤销、重做历史
  ├─ lines：供绘制和预览使用的可见直线
  └─ 鼠标事件：把点击、移动、松开解释成具体操作
```

最重要的一点：**屏幕上看到的图形项不是正式模型本身**。正式的直线数据在 `LineModel model_` 中。`CadView` 把模型中的可见直线转成 Qt 坐标和图形项画到屏幕上。

## 2. Qt 程序为什么能响应鼠标和按钮

看文件末尾第 650～656 行：

```cpp
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
```

这里依次发生四件事：

1. `QApplication app` 初始化 Qt 图形应用。
2. `MainWindow window` 构造窗口以及它内部的画布、工具栏、输入框。
3. `window.show()` 告诉 Qt 显示窗口。
4. `app.exec()` 进入**事件循环**。程序不会执行完 `main()` 就退出，而是持续等待系统送来的事件。

比如用户点击鼠标时，Qt 把鼠标事件交给鼠标下方的控件。这里的绘图区域是 `CadView`，所以它的 `mousePressEvent()` 会被调用。用户按 Enter 时，输入框发出 `returnPressed` 信号，连接到该信号的代码会执行。

可以把事件循环想成一个不断运行的分发器：

```text
等待事件 → 判断事件属于哪个控件 → 调用对应处理代码 → 等待下一个事件
```

你不需要在 `main()` 中自己写一个 `while` 循环检查鼠标。

## 3. 几个 Qt 类分别是什么

| Qt 类 | 可以怎样理解 | 在项目里的作用 |
| --- | --- | --- |
| `QApplication` | 图形程序的运行环境 | 启动事件循环。 |
| `QMainWindow` | 带中央区域、工具栏、停靠面板、状态栏的窗口框架 | `MainWindow` 的基类。 |
| `QWidget` | 界面控件的基本类型 | 面板、输入框等都属于控件体系。 |
| `QGraphicsView` | 查看一块二维场景的“摄像机/画框” | `CadView` 的基类，负责视图变换和接收鼠标。 |
| `QGraphicsScene` | 放置线条、圆点等图形项的场景 | 保存当前要显示的 Qt 图形项。 |
| `QGraphicsLineItem` | 场景中的一条显示线 | `scene()->addLine()` 创建。 |
| `QToolBar` / `QAction` | 工具栏及其中的可点击动作 | 选择绘图工具、撤销重做。 |
| `QDockWidget` | 可停靠在窗口边缘的面板 | 底部命令区。 |
| `QLineEdit` | 单行文本输入框 | 接收 `create ...` 等命令。 |

这里有两种“直线”概念，不要混在一起：

```text
LineEntity（模型）  →  正式 CAD 数据，支持撤销重做
QGraphicsLineItem   →  画在屏幕上的 Qt 图形项
```

本项目没有把模型 ID 存到 `QGraphicsLineItem` 里。点击选线由 `CadView::hitLine()` 自己计算，而不是让 Qt 图形项负责业务选择。

## 4. 文件开头：辅助类型与显示文字（第 1～54 行）

第 1 行包含 `line_model.hpp`，因此这个文件可以使用 `minicad::LineModel`、`Command` 和五种具体命令。后面一串 `#include <Q...>` 引入 Qt 控件、事件和绘图类。

```cpp
struct CadLine { int id; QPointF a, b; };
```

`CadLine` 是**界面层的可见直线**：它保留模型 ID，但端点用 Qt 的 `QPointF`。`QPointF` 方便直接交给 Qt 绘图函数。它与模型层的 `LineEntity` 不同：没有 `deleted`，因为被删除的直线根本不会进入可见列表。

第 32～34 行的 `operator==` 让两条 `CadLine` 可以比较。它间接支持 `QVector<CadLine>` 的整体比较，松开鼠标时用 `lines != before_` 判断是否真的产生变化。

```cpp
enum class Tool { Select, Create, Move, Rotate, Mirror, Delete };
enum class MirrorAxis { XAxis, YAxis, DiagonalPositive,
                        DiagonalNegative, Custom };
```

`Tool` 表示当前鼠标是什么工具。同样一次左键点击，在 `Create` 模式表示指定端点，在 `Delete` 模式表示删除目标。`MirrorAxis` 表示镜像工具当前使用哪一种轴。

`pointText()` 和 `lineDetails()` 把点、端点、长度、角度整理成界面文字。它们只用于显示和日志，不修改模型。长度通过 `std::hypot(dx,dy)` 计算，角度通过 `atan2(dy,dx)` 计算。

## 5. `CadView` 构造函数：创建绘图区域（第 56～65 行）

```cpp
class CadView final : public QGraphicsView {
public:
    explicit CadView(QWidget* parent = nullptr) : QGraphicsView(parent) {
        setScene(new QGraphicsScene(this));
        setRenderHint(QPainter::Antialiasing);
        setDragMode(QGraphicsView::NoDrag);
        setSceneRect(-1000, -1000, 2000, 2000);
        setBackgroundBrush(QColor("#20242b"));
        setMouseTracking(true);
    }
```

`CadView` **继承** `QGraphicsView`，因此天然具有显示场景、缩放、滚动以及接收鼠标事件的能力。`final` 表示不再让其他类继承它。

构造函数做的设置：

- `setScene(new QGraphicsScene(this))`：创建场景并交给视图显示。`this` 作为 Qt 父对象，负责场景的生命周期。
- `Antialiasing`：让直线边缘更平滑。
- `NoDrag`：不使用 Qt 内置的拖动模式，项目自己解释拖动。
- `setSceneRect(...)`：给场景设定初始范围，从 `(-1000,-1000)` 到 `(1000,1000)`。
- `setBackgroundBrush(...)`：设置深色背景。
- `setMouseTracking(true)`：即使没有按住鼠标，也接收鼠标移动事件，用于画线橡皮筋和镜像轴预览。

**视图与场景的区别**：场景存放二维图形项；视图决定当前看场景的哪一块、放大多少，并把屏幕鼠标位置转换为场景坐标。

## 6. `CadView` 保存了哪些状态（第 67～71、518～527 行）

先看三组最重要的数据：

```cpp
QVector<CadLine> lines;       // 当前可见直线，供显示和预览
minicad::LineModel model_;   // 正式模型
minicad::History history_;   // 命令历史
```

`model_` 才是正式数据。`lines` 是从模型同步出来的界面副本。鼠标拖动过程中可以暂时修改 `lines`，让用户看到预览；鼠标松开后通过命令修改 `model_`。`history_` 保存已经提交的命令。

此外还有交互状态：

| 字段 | 意义 |
| --- | --- |
| `tool` | 当前选中的工具。 |
| `selected_` | 当前选中直线在 `lines` 中的下标，`-1` 表示没有。 |
| `active_` | 正在拖动的直线下标。 |
| `dragging_` | 是否正在拖动直线。 |
| `before_` | 拖动前的可见直线副本。 |
| `dragStart_` | 拖动开始时的场景坐标。 |
| `hasFirst_`、`first_` | 画线时是否已有第一点及其位置。 |
| `previewSecond_` | 画线预览中的第二点。 |
| `center_`、`startAngle_` | 鼠标旋转所需的中心和起始方向。 |
| `mirrorTarget_`、`mirrorAxisFirst_` | 自定义镜像轴的目标与第一点。 |
| `panning_`、`panLast_` | 画布平移状态和上一鼠标位置。 |

这些字段构成了一个简单的**交互状态机**。例如画线时，`hasFirst_ == false` 表示等待第一点；第一次点击后变为 `true`，下一次点击就被解释为第二点。

第 69～71 行的三个 `std::function` 是留给 `MainWindow` 填入的回调：

```cpp
changed     // 模型/历史变化，要求窗口更新按钮
command     // 产生一条日志消息
liveStatus  // 实时提示变化
```

这里名为 `command` 的**回调变量**只负责日志文字；它与模型层的 `minicad::Command` 抽象类不是同一个东西。

## 7. 工具切换、撤销、重做（第 73～96 行）

`setTool()` 改变当前工具，清除尚未完成的画线/预览和自定义镜像状态，再重画。这样用户画到一半切换工具时，不会意外完成上一个操作。

`setMirrorAxis()` 更换镜像轴类型，也会清除尚未完成的自定义镜像轴交互。

`undo()` 和 `redo()` 的路径是：

```text
History 恢复 LineModel
→ resetInteraction() 清除进行中的鼠标交互
→ syncFromModel() 用模型重新生成 lines 并重画
→ notify() 通知 MainWindow 更新按钮
```

`canUndo()` / `canRedo()` 查询两个历史栈是否为空，供工具栏按钮判断是否可用。

## 8. 文本命令解释器（第 98～203 行）

`executeTextCommand(text)` 把用户输入解释为模型操作。以 `move 1 20 10` 为例：

```text
输入字符串
→ simplified() 规范空白并用空格拆分
→ verb = "move"
→ 检查一共 4 个字段
→ 读取 ID=1，dx=20，dy=10
→ 检查 ID 1 是当前可见直线
→ 创建 MoveLineCommand
→ commit() 提交
→ 返回“已移动 ...”文字
```

函数中的几个小 lambda 分别用于参数个数检查、有限浮点数解析、正整数 ID 解析、可见 ID 查找。`try/catch` 把解析或模型抛出的异常转为以“错误：”开头的界面文字，避免错误命令直接终止程序。

`list` 和 `help` 只是查询，不产生历史操作。`undo`、`redo` 调用与工具栏相同的方法。`create/delete/move/rotate/mirror` 都构造模型层的具体命令，因此文本操作与鼠标操作修改同一个 `model_`。

## 9. 画布如何显示模型：`rebuild()`（第 205～240 行）

`rebuild()` 是“把当前显示状态重新画出来”。主要过程：

```cpp
scene()->clear();
for (每条可见 CadLine) {
    scene()->addLine(...);
    如果被选中，再画两个端点圆点;
}
如果正在画线，再画起点和虚线预览;
如果正在指定自定义镜像轴，再画轴线预览;
viewport()->update();
```

`scene()->clear()` 清除的是 Qt 场景里的旧**显示图形项**，不是 `LineModel` 里的正式直线。接下来 `scene()->addLine()` 创建新的显示项。选中线颜色不同，并显示端点圆点。

画线虚线、自定义镜像轴虚线也是显示项，但没有对应的正式 `LineEntity`。它们只表示当前鼠标交互进行到哪一步。

目前每次重画都清空并重建所有显示项，实现简单，但很多直线时会比只更新变化图形的方案慢。

## 10. 背景网格：`drawBackground()`（第 243～253 行）

这是重写 `QGraphicsView` 提供的绘制函数。Qt 要绘制背景时会调用它，并传入 `QPainter*` 和需要绘制的场景区域 `rect`。

它先填充深色背景，再按 20 个场景单位的间隔画横、竖网格线，最后在 `x=0`、`y=0` 的位置画两条坐标轴。网格和坐标轴是背景像素，不是模型实体，也不会被 `hitLine()` 选中。

Qt 绘图场景默认遵循屏幕坐标习惯：X 向右增加，Y 向下增加。不要直接把画布上的“向上”理解成数学坐标的正 Y 方向。

## 11. 鼠标坐标为什么要 `mapToScene()`

鼠标事件给出的 `event->pos()` 是**视图窗口中的像素位置**。模型端点使用的是**场景坐标**。画布缩放或平移后，两者不再相同。

```cpp
const QPointF p = mapToScene(event->pos());
```

这一步把鼠标位置转换为场景坐标。之后的画线、命中检测、移动位移和旋转中心都使用场景坐标。因此缩放画布后，用户仍能在鼠标所指的位置画线。

可以想象视图是一台看着纸面的摄像机：`event->pos()` 是显示器上的位置；`mapToScene()` 找出这个位置对应纸面上的哪一点。

## 12. 鼠标按下：`mousePressEvent()`（第 255～349 行）

这个函数首先把鼠标位置转成场景坐标。然后按优先顺序处理：

1. **中键或右键**：进入平移状态，记录上一次鼠标像素位置。
2. **非左键**：忽略。
3. **画线工具**：第一次点击记录第一点；第二次点击提交创建命令。
4. **自定义镜像轴正在选点**：记录轴第一点，或用第二点提交镜像。
5. **普通点击**：调用 `hitLine()` 找被点中的可见直线。
6. **删除或预设轴镜像**：命中后立即提交相应命令。
7. **选择、移动或旋转**：记录拖动起点和原状态，等待移动、松开事件。

### 12.1 画线为什么需要两次点击

第一次点击只保存 `first_`，设置 `hasFirst_ = true`，画出起点。第二次点击才根据 `first_` 和当前位置创建 `CreateLineCommand`，调用 `commit()`。中间移动鼠标时显示的线只是预览，尚未写入正式模型或历史。

### 12.2 命中后为什么先保存 `before_`

```cpp
active_ = index;
before_ = lines;
dragStart_ = p;
```

`before_` 是拖动开始前的画面状态。后续每次鼠标移动，都从 `before_` 计算预览，避免把一次次鼠标位移累加到上一次预览上。松开时也可以比较 `lines` 和 `before_`，判断是否真的移动了。

### 12.3 鼠标旋转如何确定中心与起始角

旋转工具命中后，中心设为该直线两个端点的中点。`atan2(鼠标Y-中心Y, 鼠标X-中心X)` 得到鼠标相对中心的初始方向。拖动时再求当前方向，两者之差就是预览旋转角度。

## 13. 鼠标移动：`mouseMoveEvent()`（第 350～400 行）

这个函数按当前状态决定鼠标移动的含义：

| 当前状态 | 鼠标移动做什么 |
| --- | --- |
| `panning_` | 改变视图滚动条，平移画布。 |
| 画线已指定第一点 | 更新第二点预览、长度和角度提示。 |
| 自定义镜像轴已指定第一点 | 更新镜像轴虚线和轴长提示。 |
| 正在拖动移动或选择中的直线 | 用 `before_` 的端点加鼠标位移，修改 `lines` 作为预览。 |
| 正在拖动旋转 | 根据鼠标当前方向计算角度，修改 `lines` 作为预览。 |
| 都不是 | 不处理。 |

特别注意第 382～383 行和第 395～396 行：它们修改的是 `lines[active_]`，**没有修改 `model_`**。因此鼠标每经过一个点不会向 Undo 栈加入一条操作。`rebuild()` 在每次预览变化后重新绘制画面。

## 14. 鼠标松开：`mouseReleaseEvent()`（第 402～437 行）

如果松开中键或右键，就退出平移状态。

如果松开左键且此前在拖动直线，就结束拖动。`lines != before_` 时说明有变化：

- 移动或选择工具：从端点的前后位置求位移，创建 `MoveLineCommand`。
- 旋转工具：使用拖动得到的旋转角，创建 `RotateLineCommand`。

最后调用 `commit()`，才真正把编辑写入模型和历史。即使鼠标移动事件触发很多次，一次按下到松开仍只提交一个命令。没有变化时不提交历史。

## 15. 鼠标滚轮：`wheelEvent()`（第 439～443 行）

`event->angleDelta().y()` 判断滚轮方向。向一边滚乘以 `1.15`，向另一边滚乘以 `1/1.15`，再调用 `rebuild()`。这里缩放的是 `QGraphicsView` 的视图变换，不改直线的模型坐标，也不进入 Undo 栈。

## 16. 几个关键私有函数（第 445～516 行）

### 16.1 `presetAxis()`

把镜像轴类型换成两个 `QPointF`。例如 X 轴为 `(0,0)` 和 `(1,0)`，Y 轴为 `(0,0)` 和 `(0,1)`。模型层只认识“由两点决定的轴”，不需要知道下拉框选了什么文字。

### 16.2 `syncFromModel()`

```text
清空 lines
→ 获取 model_.visibleLines()
→ 将每个模型 Point 转为 Qt QPointF
→ 生成 CadLine
→ rebuild()
```

这一步通常在命令完成或 Undo/Redo 后执行。它把模型当前状态重新映射到界面。

### 16.3 `commit()`：编辑操作的汇合点

```cpp
void commit(std::unique_ptr<minicad::Command> command) {
    history_.execute(std::move(command), model_);
    syncFromModel();
    notify();
}
```

参数是一个具体命令，但类型写成基类 `Command` 指针，所以同一个函数可以接收创建、移动、旋转等任何命令。

第一行让 History 在正式模型上执行并记录命令；第二行刷新显示；第三行通知窗口更新撤销、重做按钮。`std::move(command)` 表示把这个命令对象的所有权交给 History。鼠标和文本命令最终都走这里。

### 16.4 `resetInteraction()` 与 `resetCustomMirror()`

撤销、重做或文本命令执行时，可能正好有未完成的鼠标操作。重置函数清除选中、拖动、画线第一点和自定义镜像轴的临时状态，避免旧交互继续影响恢复后的模型。

### 16.5 `hitLine()`

它自己实现点选：对每条线段计算鼠标点到线段的最短距离，选出阈值内最近的一条。线段参数 `t` 被限制在 `[0,1]`，所以命中区域对应有限线段，而不是延长后的无限直线。阈值除以当前缩放系数，让不同缩放下的屏幕点击宽容度大致相同。

### 16.6 `notify()`、`emitCommand()`、`setLive()`

它们先检查回调是否被设置，再调用它。这让 `CadView` 可以告诉 `MainWindow`“数据变了”“请记录日志”“请更新实时提示”，而不用直接持有命令输入框、日志框或按钮指针。

## 17. `MainWindow` 构造函数：控件怎么连起来（第 530～589 行）

第 533 行创建 `CadView`，用 `setCentralWidget(view_)` 放入主窗口中央。工具栏动作通过 `addTool()` 创建。`QActionGroup` 设置为互斥，让工具选择类似单选按钮：同一时刻只选中一种工具。

镜像轴下拉框的 `currentIndexChanged` 连接到一个 lambda：当用户更换选项时，从当前条目取出 `MirrorAxis`，调用 `view_->setMirrorAxis(axis)`，同时更新日志和提示。

这里需要理解 Qt 的**信号与槽**：

```cpp
connect(发送者, &发送者类型::某个信号, 接收者, [捕获变量] {
    // 信号发生时执行这里
});
```

`connect()` 不是立刻执行 lambda，而是建立一条连接。之后用户操作触发信号时，Qt 才执行 lambda。这个文件使用 lambda 作为接收处理代码，没有单独声明槽函数。

几个主要连接：

| 发送者与信号 | 触发时执行的事 |
| --- | --- |
| 镜像轴下拉框 `currentIndexChanged` | 设置 `CadView` 的镜像轴。 |
| 命令输入框 `returnPressed` | 取出文字、执行文本命令、记日志、清空输入框。 |
| Undo 动作 `triggered` | 调用 `view_->undo()`。 |
| Redo 动作 `triggered` | 调用 `view_->redo()`。 |

第 579～584 行则是反方向的通知：窗口把三个 lambda 赋给 `view_` 的回调。`CadView` 有消息时调用回调，窗口据此更新按钮、日志和状态栏。

## 18. 底部命令面板（第 592～620 行）

`buildCommandDock()` 创建底部 `QDockWidget`，其中的纵向布局按顺序放：

```text
QPlainTextEdit commandHistory_：只读操作日志
QLabel prompt_：当前命令和实时提示
QLineEdit commandInput_：用户输入文本命令
```

`QPlainTextEdit` 最多保留 500 个文本块，避免日志无限增长。末尾的 `setStyleSheet()` 用 Qt 样式表设置深色背景、字体、边框等，它负责外观，不参与命令执行。

`appendCommand()` 在日志前加当前时间，并把滚动条移到最底部。`updateHistory()` 根据 `view_->canUndo()` / `canRedo()` 启用或禁用工具栏按钮。

## 19. 三个从用户动作到模型变化的完整例子

### 19.1 鼠标画一条线

```text
点击“直线”工具 → MainWindow::addTool 连接的 lambda 调用 setTool(Create)
第一次点画布 → CadView::mousePressEvent 保存 first_
移动鼠标 → mouseMoveEvent 更新虚线预览；rebuild() 重画
第二次点画布 → 创建 CreateLineCommand
→ commit() → History::execute() → LineModel::create()
→ syncFromModel() → rebuild() → 正式直线出现在画布上
```

### 19.2 鼠标拖动一条线

```text
按下目标线 → hitLine() 找到下标，保存 before_ 和 dragStart_
移动鼠标 → 只修改 lines 中的端点，显示预览
松开鼠标 → 求最终位移，创建 MoveLineCommand
→ commit() 将移动写入 model_，History 留下一条记录
```

### 19.3 在底部输入 `delete 1`

```text
按 Enter → QLineEdit 发出 returnPressed
→ MainWindow 的 lambda 调用 executeTextCommand("delete 1")
→ 验证 ID 1 可见，创建 DeleteLineCommand
→ commit() → LineModel::erase(1) 设置 deleted=true
→ syncFromModel() 过滤被删除实体 → rebuild() 后画布不显示它
```

Undo 时 History 恢复删除前的模型快照，`syncFromModel()` 再次把 ID 1 转为显示线，所以它重新出现。

## 20. 初学时最容易混淆的地方

1. **`QGraphicsView` 不是数据模型。** 它负责观察场景、接收事件和视图变换；正式数据在 `LineModel`。
2. **`QGraphicsScene` 里的线不是 `LineEntity`。** 清空 scene 不等于删除模型中的直线。
3. **`lines` 也不是最终权威数据。** 它是显示副本，并允许在拖动时临时变化；提交后会从模型重新同步。
4. **`connect()` 只是登记处理方法。** 用户真正触发信号后，lambda 才执行。
5. **`mousePressEvent()`、`mouseMoveEvent()`、`mouseReleaseEvent()` 要连起来读。** 一次拖动跨越多个事件；提交命令通常发生在松开时。
6. **`event->pos()` 与模型坐标不同。** 使用 `mapToScene()` 才能把视图上的鼠标位置变成场景中的几何位置。
7. **这里有两个“command”。** `minicad::Command` 是可撤销操作对象；`CadView::command` 是给窗口写日志的文字回调。

## 21. 建议怎样对照源码阅读

第一次读，只沿着这一条路径找代码：

```text
main()
→ MainWindow 构造函数
→ setCentralWidget(view_)
→ addTool() / connect()
→ CadView::mousePressEvent()
→ CadView::commit()
→ syncFromModel()
→ rebuild()
```

第二次读，再分别追画线、移动、旋转、自定义镜像的鼠标状态。第三次读文本命令和 Undo/Redo。这样可以先弄清 Qt 怎样把事件送进来，再研究每种工具的细节。

## 22. 把整个 `CadView` 当作一个类来理解

前面是沿着源码顺序阅读。本节换一个角度：先问 `CadView` **拥有什么、对外提供什么、Qt 会自动调用什么、内部怎样保持数据一致**。这样再看整段类定义，就不会觉得它只是许多互不相关的函数堆在一起。

### 22.1 类声明和访问范围

```cpp
class CadView final : public QGraphicsView {
public:
    // 窗口和测试代码可以直接调用的部分
protected:
    // Qt 绘制与鼠标事件会调用的重写函数
private:
    // 只给 CadView 自己使用的辅助函数和状态
};
```

`public`、`protected`、`private` 是 C++ 的访问控制：

| 区域 | 本类中主要有什么 | 谁会用 |
| --- | --- | --- |
| `public` | 构造函数、工具设置、撤销重做、文本命令、`rebuild()`、三个通知回调 | `MainWindow` 和测试代码。 |
| `protected` | `drawBackground()`、鼠标按下/移动/松开、滚轮事件 | Qt 在相应事件发生时自动调用。 |
| `private` | `commit()`、`syncFromModel()`、`hitLine()`、模型与历史、交互标志 | `CadView` 内部逻辑。 |

继承 `QGraphicsView` 是为了获得现成的二维视图能力；`final` 表示这个类不计划再被继承。`drawBackground()` 和鼠标事件函数后面的 `override` 表示“我正在改写基类的虚函数”，因此 Qt 触发相应绘制或事件时会进入这里写的实现。

`CadView` 的几个字段写在 `public`，例如 `lines`、`tool` 和回调。它们并不是函数参数，而是**每个 `CadView` 对象自己长期保存的成员变量**。当前实现让 `MainWindow` 或测试直接访问它们，写法简洁；如果项目继续变大，可以考虑把状态设为私有，并通过只读查询或专门方法访问。

### 22.2 类里有四类状态

读完整个类时，先按用途给成员分类：

| 类别 | 主要成员 | 能否作为正式编辑结果 |
| --- | --- | --- |
| 正式业务数据 | `model_` | 是。这里保存所有直线及删除状态。 |
| 历史数据 | `history_` | 记录已提交操作，负责 Undo/Redo。 |
| 显示与预览数据 | `lines`、`before_`、`selected_` | `lines` 可以临时变化；提交后要从模型重新同步。 |
| 鼠标交互进度 | `hasFirst_`、`dragging_`、`panning_`、`mirrorTarget_`、各坐标和角度字段 | 否。它们只说明当前交互进行到哪一步。 |

再加上 `tool` 和 `mirrorAxis_`，它们决定**相同鼠标动作将被解释成哪种操作**。例如同样点击一条线：在 Delete 模式会删除，在 Mirror 模式会镜像，在 Select 模式会选中并可开始拖动。

这就是这个类最核心的抽象：**模型描述“现在画布上有什么”，交互状态描述“用户正在做什么”，History 描述“已经完成过什么”。**

### 22.3 `lines`、`before_`、`model_` 三者的关系

理解这个类最容易卡在这三个名字：

```text
model_  ──syncFromModel()──▶  lines  ──rebuild()──▶  QGraphicsScene
                                 │
                                 └──拖动开始时复制──▶ before_
```

假设模型中有一条线 `(0,0) → (100,0)`。`syncFromModel()` 把它转成 `CadLine` 放进 `lines`，`rebuild()` 再调用 `scene()->addLine()` 画出来。

开始拖动时，`before_ = lines` 记住旧位置。鼠标移动到新位置时，只把 `lines` 改成 `(20,10) → (120,10)` 并重画，所以屏幕看起来已经移动，但 `model_` 仍是旧位置。松开鼠标时创建 `MoveLineCommand`，`commit()` 才让命令修改 `model_`；紧接着 `syncFromModel()` 再用正式模型覆盖 `lines`。

因此在拖动的中间阶段，`lines` 和 `model_` 可以暂时不同。这是有意的：**预览不应成为几十条独立的撤销历史**。

### 22.4 `selected_` 与 `id` 为什么不是同一件事

`selected_`、`active_`、`mirrorTarget_` 保存 `lines` 中的**下标**，用于快速找到当前显示数组里的那条线。例如 `selected_=0` 是“可见列表第 0 项被选中”。

`LineEntity::id` 是模型里的**稳定编号**。删除一条线后，其他可见线的下标可能改变，ID 不会因此改变。所以在真正创建命令时，代码使用 `lines[index].id`；命令保存的是 ID，再由模型按 ID 找实体。

可以这样记：**下标服务于当前画面，ID 服务于正式模型和历史命令。**

### 22.5 三个 `std::function` 到底是什么

```cpp
std::function<void()> changed;
std::function<void(const QString&)> command;
std::function<void(const QString&)> liveStatus;
```

它们是可保存函数的成员变量，初始时可以为空。`MainWindow` 构造时给它们赋值：

```cpp
view_->changed = [this] { updateHistory(); };
view_->command = [this](const QString& text) { appendCommand(text); };
view_->liveStatus = [this](const QString& text) {
    prompt_->setText(...);
    statusBar()->showMessage(text);
};
```

之后 `CadView` 调用 `notify()` 时，里面的 `if (changed) changed();` 就会使窗口执行 `updateHistory()`。`emitCommand("...")` 把消息交给窗口的日志框；`setLive("...")` 把实时提示交给窗口的提示标签和状态栏。

这样 `CadView` 不必知道日志框或按钮具体长什么样。注意小写 `command` 是**文字通知回调**，并非 `minicad::Command` 命令对象。

### 22.6 哪些函数是真正修改正式模型的入口

从类外部看，有三条常见路径：

```text
鼠标完成创建/删除/移动/旋转/镜像 → commit(command)
文本 create/delete/move/rotate/mirror → executeTextCommand() → commit(command)
撤销/重做 → undo()/redo() → history_ 恢复 model_
```

`commit()` 是**提交新编辑命令**的中心方法：

```cpp
history_.execute(std::move(command), model_);
syncFromModel();
notify();
```

第一行修改正式模型并记录历史；第二行刷新画面；第三行请窗口更新按钮。Undo/Redo 不经过 `commit()`，因为它们不是“新命令”，而是让 `History` 在现有命令历史中前进或后退。但它们同样会在恢复后 `syncFromModel()` 和 `notify()`。

`rebuild()` **不修改** `model_`；`drawBackground()` **不修改** `model_`；平移和滚轮缩放也**不修改** `model_`。它们只负责画面。

### 22.7 为什么 `executeTextCommand()` 在 `CadView` 中

这个函数把底部输入字符串转成操作。它的五个 lambda 各自负责一件小事：

| 局部函数 | 作用 |
| --- | --- |
| `expect(count)` | 检查参数个数。 |
| `number(index)` | 把文字转成有限的 `double`。 |
| `idAt(index)` | 把文字转成合法的正整数 ID。 |
| `visibleIndex(id)` | 确保目标 ID 在当前可见线列表中。 |
| `details(id)` | 整理执行结果中的直线说明。 |

然后按动词分支：`help`、`list` 只返回信息；`undo`、`redo` 调用已有历史方法；五种编辑动词构造对应命令并调用 `commit()`。错误通过 `try/catch` 转成“错误：...”字符串交给窗口显示。

这里把解析放在 `CadView`，是为了方便访问当前 `lines`、`model_` 和 `commit()`。因此它同时承担了交互控制和文本解析两个职责；若将来扩展成大型项目，文本解析器可以单独成类，但本项目当前并未拆分。

### 22.8 鼠标事件共同构成一个状态机

不要把三个鼠标函数各自当作一次完整操作。一条线的鼠标移动跨越多个事件：

```text
mousePressEvent：确定目标，保存起始状态，设置 dragging_=true
mouseMoveEvent：根据当前鼠标位置修改 lines，并 rebuild() 显示预览
mouseReleaseEvent：比较前后状态，生成命令并 commit()
```

而画线不是拖动，是“两次点击”状态机：

```text
第一次点击：hasFirst_=true，保存 first_
鼠标移动：更新 previewSecond_，显示橡皮筋虚线
第二次点击：创建命令，hasFirst_=false
```

自定义镜像轴又是另一套步骤：选目标线 → 取轴第一点 → 取轴第二点 → 创建镜像命令。多个布尔值和坐标字段正是在记录这些阶段。

### 22.9 `rebuild()` 为什么每次都 `scene()->clear()`

`rebuild()` 不直接画到模型上，而是根据当前 `lines` 和交互状态，重建 Qt 场景里的显示项：正式可见直线、选中端点、画线虚线、自定义镜像轴虚线。

`scene()->clear()` 只删除旧的 `QGraphicsItem` 显示对象，**不会清空 `model_` 或历史记录**。随后 `addLine()`、`addEllipse()` 创建新的显示对象。这个策略让预览与选择的绘制逻辑简单一致，代价是每次鼠标移动也要重建全部显示项。

`drawBackground()` 则负责网格和坐标轴，与 `rebuild()` 创建的图形项不同：背景由 Qt 在需要重绘时调用 `QPainter` 画出。

### 22.10 一张方法职责速查表

| 方法 | 何时调用 | 主要作用 | 会改 `model_` 吗 |
| --- | --- | --- | --- |
| 构造函数 | 创建 `CadView` 时 | 建场景，设置画布属性。 | 否 |
| `setTool()` / `setMirrorAxis()` | 点击工具栏时 | 切换解释鼠标事件的模式。 | 否 |
| `executeTextCommand()` | 输入框按 Enter 时 | 解析字符串；编辑命令会走 `commit()`。 | 视命令而定 |
| `rebuild()` | 显示或预览变化时 | 根据 `lines` 重建场景显示项。 | 否 |
| `drawBackground()` | Qt 要绘制背景时 | 画底色、网格、坐标轴。 | 否 |
| `mousePressEvent()` | 鼠标按下时 | 选择交互分支；某些工具立即提交。 | 视工具而定 |
| `mouseMoveEvent()` | 鼠标移动时 | 平移视图或更新临时预览。 | 否 |
| `mouseReleaseEvent()` | 鼠标松开时 | 把完成的移动/旋转拖动提交。 | 可能 |
| `wheelEvent()` | 转动滚轮时 | 缩放视图。 | 否 |
| `hitLine()` | 要点选实体时 | 按距离查找可见线段。 | 否 |
| `syncFromModel()` | 提交、Undo、Redo 后 | 从模型重建 `lines` 并重画。 | 否 |
| `commit()` | 确认新编辑操作时 | 执行命令、记录历史、刷新显示、通知窗口。 | 是 |
| `undo()` / `redo()` | 用户撤销/重做时 | 让历史恢复模型，刷新显示。 | 是 |

读这个类时可以不断问自己三个问题：**现在改的是 `model_` 还是 `lines`？当前动作只是预览还是已经调用 `commit()`？这个函数是由窗口主动调用，还是由 Qt 事件循环自动调用？** 这三个问题弄清楚后，大部分代码就能顺着数据流读懂。
