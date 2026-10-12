#include "version_history.h"
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDockWidget>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStringList>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <functional>

struct CadLine { QPointF a, b; int id = 0; };

static bool operator==(const CadLine& lhs, const CadLine& rhs) {
    return lhs.a == rhs.a && lhs.b == rhs.b && lhs.id == rhs.id;
}

enum class Tool { Select, Create, Move, Rotate, Mirror, Delete };
enum class MirrorAxis { XAxis, YAxis, DiagonalPositive, DiagonalNegative, Custom };

static QString pointText(const QPointF& point) {
    return QString("(%1, %2)").arg(point.x(), 0, 'f', 2).arg(point.y(), 0, 'f', 2);
}

static QString lineDetails(const CadLine& line) {
    const double dx = line.b.x() - line.a.x();
    const double dy = line.b.y() - line.a.y();
    const double length = std::hypot(dx, dy);
    const double angle = std::atan2(dy, dx) * 180.0 / std::acos(-1.0);
    return QString("直线 #%1  起点 %2  终点 %3  |  ΔX=%4  ΔY=%5  长度=%6  角度=%7°")
        .arg(line.id).arg(pointText(line.a), pointText(line.b))
        .arg(dx, 0, 'f', 2).arg(dy, 0, 'f', 2)
        .arg(length, 0, 'f', 2).arg(angle, 0, 'f', 2);
}

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

    QVector<CadLine> lines; // Current display and transient drag preview only.
    VersionHistory<CadLine> history;
    Tool tool = Tool::Select;
    std::function<void()> changed;
    std::function<void(const QString&)> command;
    std::function<void(const QString&)> liveStatus;

    void setTool(Tool nextTool) {
        cancelInteraction();
        tool = nextTool;
        hasFirst_ = false;
        hasPreview_ = false;
        resetCustomMirror();
        rebuild();
    }

    void setMirrorAxis(MirrorAxis axis) {
        cancelInteraction();
        mirrorAxis_ = axis;
        resetCustomMirror();
        rebuild();
    }

    void restoreHistoryView() {
        cancelInteraction();
        lines.clear();
        for (const auto& v : history.visibleVersions()) {
            auto line = v.geometry;
            line.id = v.id;
            lines.push_back(line);
        }
        selected_ = -1;
        rebuild();
    }

    QString executeTextCommand(const QString& text) {
        const QStringList parts = text.simplified().split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) return {};
        const QString verb = parts[0].toLower();
        auto expect = [&](int count) {
            if (parts.size() != count) throw std::invalid_argument("wrong number of arguments; type help");
        };
        auto number = [&](int index) {
            bool ok = false;
            const double value = parts[index].toDouble(&ok);
            if (!ok || !std::isfinite(value)) throw std::invalid_argument("invalid number");
            return value;
        };
        auto idAt = [&](int index) {
            bool ok = false;
            const int id = parts[index].toInt(&ok);
            if (!ok || id <= 0) throw std::invalid_argument("invalid line id");
            return id;
        };
        auto details = [&](int id) {
            CadLine line = history.requireVisible(id).geometry;
            line.id = id;
            return lineDetails(line);
        };
        auto refresh = [&] {
            restoreHistoryView();
            if (changed) changed();
        };
        try {
            if (verb == "help") {
                expect(1);
                return "create x1 y1 x2 y2 | delete id | move id dx dy | "
                       "rotate id cx cy degrees | mirror id ax ay bx by | "
                       "undo | redo | list | help";
            }
            if (verb == "list") {
                expect(1);
                QStringList result;
                for (const auto& version : history.visibleVersions()) {
                    CadLine line = version.geometry;
                    line.id = version.id;
                    result << lineDetails(line);
                }
                return result.isEmpty() ? QString("当前没有直线") : result.join('\n');
            }
            if (verb == "undo") {
                expect(1);
                if (!history.undo()) return "没有可撤销的操作";
                refresh();
                return "已撤销";
            }
            if (verb == "redo") {
                expect(1);
                if (!history.redo()) return "没有可重做的操作";
                refresh();
                return "已重做";
            }
            if (verb == "create") {
                expect(5);
                const QPointF a(number(1), number(2));
                const QPointF b(number(3), number(4));
                const int id = history.create(CadLine{a, b});
                refresh();
                return QString("已创建 %1").arg(details(id));
            }
            if (verb == "delete") {
                expect(2);
                const int id = idAt(1);
                const QString oldLine = details(id);
                history.erase(id);
                refresh();
                return QString("已删除 %1").arg(oldLine);
            }
            if (verb == "move") {
                expect(4);
                const int id = idAt(1);
                CadLine line = history.requireVisible(id).geometry;
                const QPointF delta(number(2), number(3));
                line.a += delta;
                line.b += delta;
                history.update(id, line);
                refresh();
                return QString("已移动 %1").arg(details(id));
            }
            if (verb == "rotate") {
                expect(5);
                const int id = idAt(1);
                CadLine line = history.requireVisible(id).geometry;
                const QPointF center(number(2), number(3));
                const double radians = number(4) * std::acos(-1.0) / 180.0;
                auto rotate = [&](QPointF p) {
                    const QPointF v = p - center;
                    const double c = std::cos(radians), s = std::sin(radians);
                    return center + QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
                };
                line.a = rotate(line.a);
                line.b = rotate(line.b);
                history.update(id, line);
                refresh();
                return QString("已旋转 %1").arg(details(id));
            }
            if (verb == "mirror") {
                expect(6);
                const int id = idAt(1);
                CadLine line = history.requireVisible(id).geometry;
                const QPointF a(number(2), number(3));
                const QPointF b(number(4), number(5));
                if (QLineF(a, b).length() <= 1e-6)
                    throw std::invalid_argument("mirror axis needs two distinct points");
                line.a = reflectPoint(line.a, a, b);
                line.b = reflectPoint(line.b, a, b);
                history.update(id, line);
                refresh();
                return QString("已镜像 %1").arg(details(id));
            }
            throw std::invalid_argument("unknown command; type help");
        } catch (const std::exception& error) {
            return QString("错误：%1").arg(QString::fromUtf8(error.what()));
        }
    }

    void rebuild() {
        scene()->clear();
        for (int i = 0; i < lines.size(); ++i) {
            const bool selected = i == selected_;
            scene()->addLine(QLineF(lines[i].a, lines[i].b),
                QPen(selected ? QColor("#53d8fb") : QColor("#f3c74f"), selected ? 3.0 : 2.0));
            if (selected) {
                const double radius = 5.0 / std::max(transform().m11(), 0.01);
                const QPen outline(QColor("#0c1117"), 0);
                const QBrush grip(QColor("#53d8fb"));
                scene()->addEllipse(QRectF(lines[i].a.x() - radius, lines[i].a.y() - radius,
                                          radius * 2, radius * 2), outline, grip);
                scene()->addEllipse(QRectF(lines[i].b.x() - radius, lines[i].b.y() - radius,
                                          radius * 2, radius * 2), outline, grip);
            }
        }
        if (tool == Tool::Create && hasFirst_) {
            const double radius = 5.0 / std::max(transform().m11(), 0.01);
            scene()->addEllipse(QRectF(first_.x() - radius, first_.y() - radius,
                                      radius * 2, radius * 2),
                                QPen(QColor("#0c1117"), 0), QBrush(QColor("#53d8fb")));
            if (hasPreview_) {
                scene()->addLine(QLineF(first_, previewSecond_),
                    QPen(QColor("#f4f7fa"), 1.5, Qt::DashLine));
                scene()->addEllipse(QRectF(previewSecond_.x() - radius, previewSecond_.y() - radius,
                                          radius * 2, radius * 2),
                                    QPen(QColor("#53d8fb"), 0), Qt::NoBrush);
            }
        }
        if (tool == Tool::Mirror && mirrorAxis_ == MirrorAxis::Custom &&
            mirrorHasFirstAxis_ && hasMirrorHover_) {
            scene()->addLine(QLineF(mirrorAxisFirst_, mirrorAxisHover_),
                QPen(QColor("#ff7bd5"), 2.0, Qt::DashDotLine));
        }
        viewport()->update();
    }

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override {
        painter->fillRect(rect, QColor("#20242b"));
        const int step = 20;
        painter->setPen(QPen(QColor("#343b46"), 0));
        const int left = static_cast<int>(std::floor(rect.left() / step)) * step;
        const int top = static_cast<int>(std::floor(rect.top() / step)) * step;
        for (int x = left; x <= rect.right(); x += step) painter->drawLine(x, rect.top(), x, rect.bottom());
        for (int y = top; y <= rect.bottom(); y += step) painter->drawLine(rect.left(), y, rect.right(), y);
        painter->setPen(QPen(QColor("#b94a48"), 0)); painter->drawLine(rect.left(), 0, rect.right(), 0);
        painter->setPen(QPen(QColor("#4eaf74"), 0)); painter->drawLine(0, rect.top(), 0, rect.bottom());
    }

    void mousePressEvent(QMouseEvent* event) override {
        const QPointF p = mapToScene(event->pos());
        if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
            panning_ = true; panLast_ = event->pos(); setCursor(Qt::ClosedHandCursor); return;
        }
        if (event->button() != Qt::LeftButton) return;

        if (tool == Tool::Create) {
            if (!hasFirst_) {
                first_ = p; previewSecond_ = p; hasFirst_ = true; hasPreview_ = true;
                rebuild();
                setLive(QString("LINE  已指定第一点 %1；请指定下一点").arg(pointText(first_)));
            } else {
                CadLine created{first_, p};
                created.id = history.create(created);
                lines.push_back(created); hasFirst_ = false; hasPreview_ = false;
                selected_ = lines.size() - 1;
                rebuild();
                emitCommand(QString("LINE 创建完成  %1").arg(lineDetails(created)));
                setLive("命令: LINE（单击指定第一点）");
                if (changed) changed();
            }
            return;
        }

        if (tool == Tool::Mirror && mirrorAxis_ == MirrorAxis::Custom && mirrorTarget_ >= 0) {
            if (!mirrorHasFirstAxis_) {
                mirrorAxisFirst_ = p;
                mirrorAxisHover_ = p;
                mirrorHasFirstAxis_ = true;
                hasMirrorHover_ = true;
                rebuild();
                setLive(QString("MIRROR 自定义轴第一点 %1；请指定第二点").arg(pointText(p)));
            } else if (QLineF(mirrorAxisFirst_, p).length() > 1e-6) {
                lines[mirrorTarget_].a = reflectPoint(lines[mirrorTarget_].a, mirrorAxisFirst_, p);
                lines[mirrorTarget_].b = reflectPoint(lines[mirrorTarget_].b, mirrorAxisFirst_, p);
                const int completedTarget = mirrorTarget_;
                resetCustomMirror();
                selected_ = completedTarget;
                rebuild();
                emitCommand(QString("MIRROR 按自定义轴 %1 → %2 完成  %3")
                    .arg(pointText(mirrorAxisFirst_), pointText(p),
                         lineDetails(lines[completedTarget])));
                setLive("命令: MIRROR（请选择对象）");
                notify();
            } else {
                setLive("MIRROR 镜像轴两点不能重合，请重新指定第二点");
            }
            return;
        }

        const int index = hitLine(p);
        if (index < 0) {
            if (tool == Tool::Select) {
                selected_ = -1; rebuild(); emitCommand("SELECT 未选择对象");
            }
            return;
        }

        selected_ = index;
        rebuild();
        emitCommand(QString("SELECT %1").arg(lineDetails(lines[index])));

        if (tool == Tool::Mirror && mirrorAxis_ == MirrorAxis::Custom) {
            mirrorTarget_ = index;
            setLive("MIRROR 已选择对象；请指定镜像轴的第一个点");
            return;
        }

        if (tool == Tool::Delete) {
            const QString deleted = lineDetails(lines[index]);
            history.erase(lines[index].id); lines.remove(index); selected_ = -1; rebuild();
            emitCommand(QString("ERASE 已删除  %1").arg(deleted));
            if (changed) changed();
            return;
        }
        if (tool == Tool::Mirror) {
            lines[index].a = reflectPreset(lines[index].a);
            lines[index].b = reflectPreset(lines[index].b);
            rebuild();
            emitCommand(QString("MIRROR 关于%1镜像完成  %2")
                .arg(mirrorAxisName(), lineDetails(lines[index])));
            notify();
            return;
        }

        active_ = index; before_ = lines[index]; dragStart_ = p;
        if (tool == Tool::Rotate) {
            const auto& line = lines[index];
            center_ = (line.a + line.b) / 2.0;
            startAngle_ = std::atan2(p.y() - center_.y(), p.x() - center_.x());
        }
        dragging_ = true;
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (panning_) {
            const QPoint delta = event->pos() - panLast_; panLast_ = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            return;
        }
        const QPointF hoverPoint = mapToScene(event->pos());
        if (tool == Tool::Create && hasFirst_) {
            previewSecond_ = hoverPoint;
            hasPreview_ = true;
            const QPointF delta = previewSecond_ - first_;
            const double angle = std::atan2(delta.y(), delta.x()) * 180.0 / std::acos(-1.0);
            rebuild();
            setLive(QString("LINE 下一点 %1  |  ΔX=%2  ΔY=%3  长度=%4  角度=%5°")
                .arg(pointText(previewSecond_))
                .arg(delta.x(), 0, 'f', 2).arg(delta.y(), 0, 'f', 2)
                .arg(std::hypot(delta.x(), delta.y()), 0, 'f', 2).arg(angle, 0, 'f', 2));
            return;
        }
        if (tool == Tool::Mirror && mirrorAxis_ == MirrorAxis::Custom && mirrorHasFirstAxis_) {
            mirrorAxisHover_ = hoverPoint;
            hasMirrorHover_ = true;
            rebuild();
            setLive(QString("MIRROR 镜像轴第二点 %1  |  轴长=%2")
                .arg(pointText(hoverPoint)).arg(QLineF(mirrorAxisFirst_, hoverPoint).length(), 0, 'f', 2));
            return;
        }
        if (!dragging_ || active_ < 0) return;
        const QPointF p = hoverPoint;
        if (tool == Tool::Move || tool == Tool::Select) {
            const QPointF delta = p - dragStart_;
            lines[active_].a = before_.a + delta;
            lines[active_].b = before_.b + delta;
            setLive(QString("%1  ΔX=%2  ΔY=%3  距离=%4")
                .arg(tool == Tool::Move ? "MOVE 正在移动" : "拖动对象")
                .arg(delta.x(), 0, 'f', 2).arg(delta.y(), 0, 'f', 2)
                .arg(std::hypot(delta.x(), delta.y()), 0, 'f', 2));
        } else if (tool == Tool::Rotate) {
            const double angle = std::atan2(p.y() - center_.y(), p.x() - center_.x()) - startAngle_;
            auto rotate = [&](QPointF q) {
                const QPointF v = q - center_; const double c = std::cos(angle), s = std::sin(angle);
                return center_ + QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
            };
            lines[active_].a = rotate(before_.a);
            lines[active_].b = rotate(before_.b);
            setLive(QString("ROTATE 正在旋转  角度=%1°").arg(angle * 180.0 / std::acos(-1.0), 0, 'f', 2));
        }
        rebuild();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
            panning_ = false; unsetCursor(); return;
        }
        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            if (active_ >= 0 && active_ < lines.size() && !(lines[active_] == before_)) {
                if (tool == Tool::Move || tool == Tool::Select) {
                    const QPointF delta = lines[active_].a - before_.a;
                    emitCommand(QString("%1 完成  ΔX=%2  ΔY=%3  距离=%4  |  %5")
                        .arg(tool == Tool::Move ? "MOVE" : "DRAG")
                        .arg(delta.x(), 0, 'f', 2).arg(delta.y(), 0, 'f', 2)
                        .arg(std::hypot(delta.x(), delta.y()), 0, 'f', 2)
                        .arg(lineDetails(lines[active_])));
                } else if (tool == Tool::Rotate) {
                    const QLineF oldLine(before_.a, before_.b);
                    const QLineF newLine(lines[active_].a, lines[active_].b);
                    double angle = newLine.angle() - oldLine.angle();
                    if (angle > 180.0) angle -= 360.0;
                    if (angle < -180.0) angle += 360.0;
                    emitCommand(QString("ROTATE 完成  角度=%1°  |  %2")
                        .arg(angle, 0, 'f', 2).arg(lineDetails(lines[active_])));
                }
                notify();
            }
            setLive("命令: 就绪");
        }
    }

    void wheelEvent(QWheelEvent* event) override {
        scale(event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15,
              event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15);
        rebuild();
    }

private:
    QPointF reflectPreset(const QPointF& point) const {
        switch (mirrorAxis_) {
            case MirrorAxis::XAxis: return QPointF(point.x(), -point.y());
            case MirrorAxis::YAxis: return QPointF(-point.x(), point.y());
            case MirrorAxis::DiagonalPositive: return QPointF(point.y(), point.x());
            case MirrorAxis::DiagonalNegative: return QPointF(-point.y(), -point.x());
            case MirrorAxis::Custom: return point;
        }
        return point;
    }

    static QPointF reflectPoint(const QPointF& point, const QPointF& axisA, const QPointF& axisB) {
        const QPointF axis = axisB - axisA;
        const double lengthSquared = QPointF::dotProduct(axis, axis);
        const double t = QPointF::dotProduct(point - axisA, axis) / lengthSquared;
        const QPointF projection = axisA + t * axis;
        return projection * 2.0 - point;
    }

    QString mirrorAxisName() const {
        switch (mirrorAxis_) {
            case MirrorAxis::XAxis: return " X 轴";
            case MirrorAxis::YAxis: return " Y 轴";
            case MirrorAxis::DiagonalPositive: return " y = x 轴";
            case MirrorAxis::DiagonalNegative: return " y = -x 轴";
            case MirrorAxis::Custom: return "自定义轴";
        }
        return {};
    }

    void resetCustomMirror() {
        mirrorTarget_ = -1;
        mirrorHasFirstAxis_ = false;
        hasMirrorHover_ = false;
    }

    int hitLine(QPointF p) const {
        int result = -1; double best = 10.0 / transform().m11();
        for (int i = 0; i < lines.size(); ++i) {
            const QLineF line(lines[i].a, lines[i].b); const QPointF vector = line.p2() - line.p1();
            const double lengthSquared = QPointF::dotProduct(vector, vector);
            const double t = lengthSquared > 1e-12
                ? std::clamp(QPointF::dotProduct(p - line.p1(), vector) / lengthSquared, 0.0, 1.0) : 0.0;
            const double distance = QLineF(p, line.p1() + t * vector).length();
            if (distance < best) { best = distance; result = i; }
        }
        return result;
    }

    void notify() {
        const auto& line = lines[selected_];
        history.update(line.id, line);
        if (changed) changed();
    }
    void cancelInteraction() {
        if (dragging_ && active_ >= 0 && active_ < lines.size()) lines[active_] = before_;
        dragging_ = false;
        active_ = -1;
        hasFirst_ = hasPreview_ = false;
        resetCustomMirror();
    }
    void emitCommand(const QString& text) { if (command) command(text); }
    void setLive(const QString& text) { if (liveStatus) liveStatus(text); }

    bool hasFirst_ = false, hasPreview_ = false, panning_ = false, dragging_ = false;
    bool mirrorHasFirstAxis_ = false, hasMirrorHover_ = false;
    int active_ = -1, selected_ = -1, mirrorTarget_ = -1;
    QPointF first_, previewSecond_, dragStart_, center_, mirrorAxisFirst_, mirrorAxisHover_;
    MirrorAxis mirrorAxis_ = MirrorAxis::XAxis;
    double startAngle_ = 0;
    QPoint panLast_;
    CadLine before_;
};

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        view_ = new CadView(this); setCentralWidget(view_); resize(1180, 780);
        setWindowTitle("MiniCAD - Qt 2D");
        auto* bar = addToolBar("Tools"); bar->setMovable(false);
        auto* group = new QActionGroup(this); group->setExclusive(true);
        addTool("选择", "SELECT", Tool::Select, bar, group);
        addTool("直线", "LINE", Tool::Create, bar, group);
        addTool("移动", "MOVE", Tool::Move, bar, group);
        addTool("旋转", "ROTATE", Tool::Rotate, bar, group);
        addTool("镜像", "MIRROR", Tool::Mirror, bar, group);
        addTool("删除", "ERASE", Tool::Delete, bar, group);
        bar->addSeparator();
        bar->addWidget(new QLabel("镜像轴：", bar));
        auto* mirrorAxis = new QComboBox(bar);
        mirrorAxis->addItem("X 轴", static_cast<int>(MirrorAxis::XAxis));
        mirrorAxis->addItem("Y 轴", static_cast<int>(MirrorAxis::YAxis));
        mirrorAxis->addItem("y = x", static_cast<int>(MirrorAxis::DiagonalPositive));
        mirrorAxis->addItem("y = -x", static_cast<int>(MirrorAxis::DiagonalNegative));
        mirrorAxis->addItem("自定义轴（两点）", static_cast<int>(MirrorAxis::Custom));
        mirrorAxis->setToolTip("选择镜像所依据的轴线");
        bar->addWidget(mirrorAxis);
        connect(mirrorAxis, &QComboBox::currentIndexChanged, this, [this, mirrorAxis](int index) {
            const auto axis = static_cast<MirrorAxis>(mirrorAxis->itemData(index).toInt());
            view_->setMirrorAxis(axis);
            appendCommand(QString("MIRROR 镜像轴设置为：%1").arg(mirrorAxis->currentText()));
            prompt_->setText(QString("<span style='color:#53d8fb'>&gt;</span>&nbsp; MIRROR 镜像轴：%1").arg(mirrorAxis->currentText()));
        });
        bar->addSeparator();
        undo_ = bar->addAction("撤销"); redo_ = bar->addAction("重做");
        undo_->setShortcut(QKeySequence::Undo); redo_->setShortcut(QKeySequence::Redo);

        buildCommandDock();
        connect(commandInput_, &QLineEdit::returnPressed, this, [this] {
            const QString input = commandInput_->text().trimmed();
            if (input.isEmpty()) return;
            appendCommand(QString("> %1").arg(input));
            appendCommand(view_->executeTextCommand(input));
            commandInput_->clear();
        });
        connect(undo_, &QAction::triggered, this, [this] {
            if (!view_->history.undo()) return;
            view_->restoreHistoryView();
            appendCommand("UNDO 已撤销上一步操作"); updateHistory();
        });
        connect(redo_, &QAction::triggered, this, [this] {
            if (!view_->history.redo()) return;
            view_->restoreHistoryView();
            appendCommand("REDO 已恢复下一步操作"); updateHistory();
        });
        view_->changed = [this] { updateHistory(); };
        view_->command = [this](const QString& text) { appendCommand(text); };
        view_->liveStatus = [this](const QString& text) {
            prompt_->setText(QString("<span style='color:#53d8fb'>&gt;</span>&nbsp; %1").arg(text.toHtmlEscaped()));
            statusBar()->showMessage(text);
        };
        updateHistory();
        appendCommand("MiniCAD 已启动。请选择工具开始绘图。");
        prompt_->setText("<span style='color:#53d8fb'>&gt;</span>&nbsp; 命令: 就绪");
        statusBar()->showMessage("就绪");
    }

private:
    void buildCommandDock() {
        auto* dock = new QDockWidget("命令行 / COMMAND", this);
        dock->setObjectName("commandDock"); dock->setAllowedAreas(Qt::BottomDockWidgetArea);
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        auto* panel = new QWidget(dock); panel->setObjectName("commandPanel");
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(9, 5, 9, 7); layout->setSpacing(3);
        commandHistory_ = new QPlainTextEdit(panel); commandHistory_->setReadOnly(true);
        commandHistory_->setMaximumBlockCount(500); commandHistory_->setMinimumHeight(90);
        prompt_ = new QLabel(panel); prompt_->setMinimumHeight(24);
        commandInput_ = new QLineEdit(panel);
        commandInput_->setPlaceholderText("输入 help 查看命令，例如 create 0 0 100 0；按 Enter 执行");
        layout->addWidget(commandHistory_);
        layout->addWidget(prompt_);
        layout->addWidget(commandInput_);
        dock->setWidget(panel); addDockWidget(Qt::BottomDockWidgetArea, dock);
        resizeDocks({dock}, {145}, Qt::Vertical);
        dock->setStyleSheet(
            "QDockWidget { color:#cbd5df; background:#161a20; font-weight:600; }"
            "QDockWidget::title { background:#11151a; padding:5px 9px; }"
            "QWidget#commandPanel { background:#171c22; }"
            "QPlainTextEdit { background:#171c22; color:#dce5ed; border:1px solid #333b45; "
            "font-family:Consolas, 'Microsoft YaHei UI'; font-size:12px; selection-background-color:#315a70; }"
            "QLabel { color:#dce5ed; background:#11151a; border:1px solid #333b45; "
            "font-family:Consolas, 'Microsoft YaHei UI'; padding:2px 6px; }"
            "QLineEdit { color:#f4f7fa; background:#10151b; border:1px solid #53d8fb; "
            "font-family:Consolas, 'Microsoft YaHei UI'; padding:5px 7px; }"
        );
    }

    void addTool(const QString& label, const QString& commandName, Tool tool,
                 QToolBar* bar, QActionGroup* group) {
        auto* action = bar->addAction(label); action->setCheckable(true); group->addAction(action);
        if (tool == Tool::Select) action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, tool, commandName] {
            view_->setTool(tool); appendCommand(QString("命令: %1").arg(commandName));
            prompt_->setText(QString("<span style='color:#53d8fb'>&gt;</span>&nbsp; 命令: %1 — 请在画布中选择或指定点").arg(commandName));
        });
    }

    void appendCommand(const QString& text) {
        const QString time = QDateTime::currentDateTime().toString("HH:mm:ss");
        commandHistory_->appendPlainText(QString("[%1] %2").arg(time, text));
        commandHistory_->verticalScrollBar()->setValue(commandHistory_->verticalScrollBar()->maximum());
    }

    void updateHistory() {
        undo_->setEnabled(view_->history.undoCount() > 0); redo_->setEnabled(view_->history.redoCount() > 0);
    }

    CadView* view_ = nullptr;
    QPlainTextEdit* commandHistory_ = nullptr;
    QLineEdit* commandInput_ = nullptr;
    QLabel* prompt_ = nullptr;
    QAction* undo_ = nullptr;
    QAction* redo_ = nullptr;
};

#ifndef MINICAD_NO_MAIN
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    MainWindow window; window.show();
    return app.exec();
}
#endif
