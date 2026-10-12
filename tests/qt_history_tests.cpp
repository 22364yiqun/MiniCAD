#define MINICAD_NO_MAIN
#include "../src/qt_main.cpp"
#include <QKeyEvent>
#include <iostream>

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        MainWindow window;
        window.show();
        app.processEvents();
        auto* view = static_cast<CadView*>(window.centralWidget());
        auto send = [&](QEvent::Type type, QPointF p) {
            const QPointF local = view->mapFromScene(p);
            QMouseEvent event(type, local, view->viewport()->mapToGlobal(local.toPoint()),
                              type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                              type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                              Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &event);
        };
        auto click = [&](QPointF p) {
            send(QEvent::MouseButtonPress, p);
            send(QEvent::MouseButtonRelease, p);
        };
        auto trigger = [&](const QString& text) {
            for (auto* action : window.findChildren<QAction*>()) {
                if (action->text() == text) { action->trigger(); return; }
            }
            throw std::runtime_error("missing history action");
        };
        auto create = [&](QPointF a, QPointF b) {
            view->setTool(Tool::Create); click(a); click(b);
        };
        create({-100, -80}, {-40, -80});
        const CadLine initial = view->lines[0];
        create({40, 80}, {100, 80});
        const int secondId = view->lines[1].id;
        check(view->history.systemOperation() == 2, "GUI create history");

        view->setTool(Tool::Move);
        send(QEvent::MouseButtonPress, {-70, -80});
        send(QEvent::MouseMove, {-70, -40});
        check(view->history.systemOperation() == 2, "drag preview recorded history");
        send(QEvent::MouseButtonRelease, {-70, -40});
        check(view->history.systemOperation() == 3 && view->lines[0].a == QPointF(-100, -40),
              "GUI move commit");
        trigger("撤销");
        check(view->lines[0] == initial, "GUI undo move");
        trigger("重做");
        const CadLine moved = view->lines[0];

        view->setTool(Tool::Rotate);
        send(QEvent::MouseButtonPress, moved.b);
        send(QEvent::MouseMove, {-70, -10});
        send(QEvent::MouseButtonRelease, {-70, -10});
        check(view->history.systemOperation() == 4 && !(view->lines[0] == moved), "GUI rotate");
        trigger("撤销"); check(view->lines[0] == moved, "GUI undo rotate");
        trigger("重做");
        const CadLine rotated = view->lines[0];

        view->setTool(Tool::Mirror);
        view->setMirrorAxis(MirrorAxis::YAxis);
        click((rotated.a + rotated.b) / 2);
        const CadLine mirrored = view->lines[0];
        check(mirrored.a == QPointF(-rotated.a.x(), rotated.a.y()), "GUI preset mirror");
        trigger("撤销"); check(view->lines[0] == rotated, "GUI undo mirror");
        trigger("重做");

        view->setMirrorAxis(MirrorAxis::Custom);
        click((mirrored.a + mirrored.b) / 2);
        click({0, 0}); click({0, 0});
        check(view->history.systemOperation() == 5, "degenerate mirror axis recorded");
        click({100, 0});
        check(view->history.systemOperation() == 6, "GUI custom mirror");
        trigger("撤销"); check(view->lines[0] == mirrored, "GUI undo custom mirror");
        trigger("重做");

        view->setTool(Tool::Delete);
        click((view->lines[0].a + view->lines[0].b) / 2);
        check(view->lines.size() == 1 && view->lines[0].id == secondId, "GUI delete stable IDs");
        trigger("撤销"); check(view->lines.size() == 2, "GUI undo delete");
        trigger("重做"); check(view->lines.size() == 1, "GUI redo delete");
        trigger("撤销");
        create({-120, 120}, {-60, 120});
        check(view->history.redoCount() == 0 && view->lines.size() == 3, "GUI branch pruning");

        // Undo during a drag must cancel the preview and invalidate its old index.
        view->setTool(Tool::Move);
        send(QEvent::MouseButtonPress, (view->lines[0].a + view->lines[0].b) / 2);
        send(QEvent::MouseMove, {-160, -100});
        trigger("撤销");
        send(QEvent::MouseMove, {140, 140});
        send(QEvent::MouseButtonRelease, {140, 140});
        check(view->history.systemOperation() == 6 && view->lines.size() == 2,
              "GUI stale drag after undo");
        while (view->history.undoCount()) trigger("撤销");
        check(view->lines.empty(), "GUI undo all");
        while (view->history.redoCount()) trigger("重做");
        check(view->lines.size() == 3, "GUI redo all duplicates");
        const auto committed = view->lines;
        const auto count = view->history.systemOperation();
        view->setTool(Tool::Move);
        send(QEvent::MouseButtonPress, (view->lines[0].a + view->lines[0].b) / 2);
        send(QEvent::MouseMove, {-150, -100});
        view->setTool(Tool::Select);
        send(QEvent::MouseButtonRelease, {-150, -100});
        check(view->lines == committed && view->history.systemOperation() == count,
              "tool change kept an uncommitted drag");
        click((view->lines[0].a + view->lines[0].b) / 2);
        check(view->history.systemOperation() == count, "selection created history");
        auto* input = window.findChild<QLineEdit*>();
        check(input != nullptr, "missing text command input");
        input->setText("create 0 0 10 0");
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(input, &enter);
        const int commandId = view->lines.back().id;
        check(view->history.requireVisible(commandId).geometry.b == QPointF(10, 0),
              "window input did not create on canvas");
        auto commandText = [&](const QString& text) { return view->executeTextCommand(text); };
        commandText(QString("move %1 5 5").arg(commandId));
        check(view->history.requireVisible(commandId).geometry.a == QPointF(5, 5), "text move");
        commandText(QString("rotate %1 0 0 90").arg(commandId));
        check(QLineF(view->history.requireVisible(commandId).geometry.a, QPointF(-5, 5)).length() < 1e-5,
              "text rotate");
        commandText(QString("mirror %1 0 0 0 1").arg(commandId));
        check(QLineF(view->history.requireVisible(commandId).geometry.a, QPointF(5, 5)).length() < 1e-5,
              "text mirror");
        check(commandText(QString("mirror %1 0 0 0 0").arg(commandId)).startsWith("错误"),
              "invalid mirror axis accepted");
        commandText(QString("delete %1").arg(commandId));
        check(view->history.require(commandId).deleted, "text delete");
        commandText("undo");
        check(!view->history.require(commandId).deleted, "text undo");
        commandText("redo");
        check(view->history.require(commandId).deleted, "text redo");
        check(commandText("list").contains("直线 #"), "text list");
        std::cout << "Qt mouse interactions and toolbar Undo/Redo passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
