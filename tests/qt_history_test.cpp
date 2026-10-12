#define MINICAD_NO_MAIN
#include "../src/qt_main.cpp"

#include <QKeyEvent>
#include <stdexcept>

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

static bool near(QPointF actual, QPointF expected) {
    return QLineF(actual, expected).length() < 1e-5;
}

static void mouse(CadView& view, QEvent::Type type, QPointF scenePoint,
                  Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPointF local = view.mapFromScene(scenePoint);
    QMouseEvent event(type, local, local, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &event);
}

static void click(CadView& view, QPointF p) {
    mouse(view, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}

static void drag(CadView& view, QPointF from, QPointF to) {
    mouse(view, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    mouse(view, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    CadView view;
    view.resize(800, 600);
    view.show();
    app.processEvents();

    view.setTool(Tool::Create);
    click(view, {0, 0});
    click(view, {100, 0});
    check(view.lines.size() == 1 && view.lines[0].id == 1, "create");

    view.setTool(Tool::Move);
    drag(view, {50, 0}, {70, 20});
    check(near(view.lines[0].a, {20, 20}) && near(view.lines[0].b, {120, 20}), "move");
    view.undo();
    check(near(view.lines[0].a, {0, 0}) && view.canRedo(), "undo move");
    view.redo();
    check(near(view.lines[0].a, {20, 20}), "redo move");

    view.setTool(Tool::Rotate);
    drag(view, {120, 20}, {70, 70});
    check(near(view.lines[0].a, {70, -30}) && near(view.lines[0].b, {70, 70}), "rotate");

    view.setTool(Tool::Mirror);
    view.setMirrorAxis(MirrorAxis::YAxis);
    click(view, {70, 20});
    check(near(view.lines[0].a, {-70, -30}) && near(view.lines[0].b, {-70, 70}), "mirror");

    view.setTool(Tool::Delete);
    click(view, {-70, 20});
    check(view.lines.empty(), "delete");
    view.undo();
    check(view.lines.size() == 1 && view.lines[0].id == 1, "undo delete");
    view.redo();
    check(view.lines.empty(), "redo delete");
    view.undo();
    check(view.canRedo(), "redo available after undo");

    view.setTool(Tool::Mirror);
    view.setMirrorAxis(MirrorAxis::Custom);
    click(view, {-70, 20});
    click(view, {0, 0});
    click(view, {1, 0});
    check(near(view.lines[0].a, {-70, 30}) &&
          near(view.lines[0].b, {-70, -70}), "custom mirror");
    check(!view.canRedo(), "new operation clears redo");

    check(view.executeTextCommand("create 0 0 10 0").startsWith("已创建"), "text create");
    check(view.lines.size() == 2 && view.lines[1].id == 2, "text and mouse share canvas");
    view.executeTextCommand("move 2 5 5");
    check(near(view.lines[1].a, {5, 5}), "text move");
    view.executeTextCommand("rotate 2 0 0 90");
    check(near(view.lines[1].a, {-5, 5}), "text rotate");
    view.executeTextCommand("mirror 2 0 0 0 1");
    check(near(view.lines[1].a, {5, 5}), "text mirror");
    view.executeTextCommand("delete 2");
    check(view.lines.size() == 1, "text delete");
    view.executeTextCommand("undo");
    check(view.lines.size() == 2, "text undo");
    view.executeTextCommand("redo");
    check(view.lines.size() == 1, "text redo");
    check(view.executeTextCommand("list").contains("直线 #1"), "text list");
    check(view.executeTextCommand("move 999 1 1").startsWith("错误"), "text error");

    MainWindow window;
    window.show();
    app.processEvents();
    auto* input = window.findChild<QLineEdit*>();
    check(input != nullptr, "command input exists");
    input->setText("create 0 0 10 0");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(input, &enter);
    auto* windowView = static_cast<CadView*>(window.centralWidget());
    check(windowView->lines.size() == 1, "command input changes window canvas");
    return 0;
}
