#include "line_model.hpp"

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
#include <memory>
#include <utility>

struct CadLine { int id; QPointF a, b; };

static bool operator==(const CadLine& lhs, const CadLine& rhs) {
    return lhs.id == rhs.id && lhs.a == rhs.a && lhs.b == rhs.b;
}

enum class Tool { Select, Create, Move, Rotate, Mirror, Delete };
enum class MirrorAxis { XAxis, YAxis, DiagonalPositive, DiagonalNegative, Custom };

// 把坐标、长度、角度整理成界面显示的文字，不修改直线。
static QString pointText(const QPointF& point) {
    return QString("(%1, %2)").arg(point.x(), 0, 'f', 2).arg(point.y(), 0, 'f', 2);
}

static QString lineDetails(int index, const CadLine& line) {
    (void)index;
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

    QVector<CadLine> lines;
    Tool tool = Tool::Select;
    std::function<void()> changed;
    std::function<void(const QString&)> command;
    std::function<void(const QString&)> liveStatus;

    void setTool(Tool nextTool) {
        tool = nextTool;
        hasFirst_ = false;
        hasPreview_ = false;
        resetRotation();
        resetCustomMirror();
        rebuild();
    }

    void setMirrorAxis(MirrorAxis axis) {
        mirrorAxis_ = axis;
        resetCustomMirror();
        rebuild();
    }

    void clearSelection() { selected_ = -1; rebuild(); }

    bool canUndo() const { return history_.undoCount() != 0; }
    bool canRedo() const { return history_.redoCount() != 0; }
    void undo() {
        if (history_.undo(model_)) { resetInteraction(); syncFromModel(); notify(); }
    }
    void redo() {
        if (history_.redo(model_)) { resetInteraction(); syncFromModel(); notify(); }
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
        auto visibleIndex = [&](int id) {
            for (int i = 0; i < lines.size(); ++i) {
                if (lines[i].id == id) return i;
            }
            throw std::invalid_argument("line id does not exist or is deleted");
        };
        auto details = [&](int id) {
            const int index = visibleIndex(id);
            return lineDetails(index, lines[index]);
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
                if (lines.isEmpty()) return "当前没有直线";
                QStringList result;
                for (int i = 0; i < lines.size(); ++i) result << lineDetails(i, lines[i]);
                return result.join('\n');
            }
            if (verb == "undo") {
                expect(1);
                if (!canUndo()) return "没有可撤销的操作";
                undo();
                return "已撤销";
            }
            if (verb == "redo") {
                expect(1);
                if (!canRedo()) return "没有可重做的操作";
                redo();
                return "已重做";
            }
            if (verb == "create") {
                expect(5);
                const minicad::Point a{number(1), number(2)};
                const minicad::Point b{number(3), number(4)};
                const int id = model_.nextId();
                resetInteraction();
                commit(std::make_unique<minicad::CreateLineCommand>(a, b));
                return QString("已创建 %1").arg(details(id));
            }
            if (verb == "delete") {
                expect(2);
                const int id = idAt(1);
                const QString oldLine = details(id);
                resetInteraction();
                commit(std::make_unique<minicad::DeleteLineCommand>(id));
                return QString("已删除 %1").arg(oldLine);
            }
            if (verb == "move") {
                expect(4);
                const int id = idAt(1);
                visibleIndex(id);
                const minicad::Point delta{number(2), number(3)};
                resetInteraction();
                commit(std::make_unique<minicad::MoveLineCommand>(id, delta));
                return QString("已移动 %1").arg(details(id));
            }
            if (verb == "rotate") {
                expect(5);
                const int id = idAt(1);
                visibleIndex(id);
                const minicad::Point center{number(2), number(3)};
                const double degrees = number(4);
                resetInteraction();
                commit(std::make_unique<minicad::RotateLineCommand>(id, center, degrees));
                return QString("已旋转 %1").arg(details(id));
            }
            if (verb == "mirror") {
                expect(6);
                const int id = idAt(1);
                visibleIndex(id);
                const minicad::Point a{number(2), number(3)};
                const minicad::Point b{number(4), number(5)};
                resetInteraction();
                commit(std::make_unique<minicad::MirrorLineCommand>(id, a, b));
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
        if (tool == Tool::Rotate && rotateHasPivot_) {
            const double radius = 7.0 / std::max(transform().m11(), 0.01);
            scene()->addEllipse(QRectF(center_.x() - radius, center_.y() - radius,
                                      radius * 2, radius * 2),
                                QPen(QColor("#ff7bd5"), 0), Qt::NoBrush);
            scene()->addLine(QLineF(center_.x() - radius * 1.5, center_.y(),
                                    center_.x() + radius * 1.5, center_.y()),
                             QPen(QColor("#ff7bd5"), 0));
            scene()->addLine(QLineF(center_.x(), center_.y() - radius * 1.5,
                                    center_.x(), center_.y() + radius * 1.5),
                             QPen(QColor("#ff7bd5"), 0));
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
                const CadLine created{model_.nextId(), first_, p};
                hasFirst_ = false; hasPreview_ = false;
                selected_ = lines.size();
                commit(std::make_unique<minicad::CreateLineCommand>(
                    toModelPoint(created.a), toModelPoint(created.b)));
                emitCommand(QString("LINE 创建完成  %1").arg(lineDetails(selected_, created)));
                setLive("命令: LINE（单击指定第一点）");
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
            } else if (QLineF(mirrorAxisFirst_, p).length() > std::sqrt(minicad::kEpsilon)) {
                const int completedTarget = mirrorTarget_;
                const QPointF axisFirst = mirrorAxisFirst_;
                commit(std::make_unique<minicad::MirrorLineCommand>(
                    lines[completedTarget].id, toModelPoint(axisFirst), toModelPoint(p)));
                resetCustomMirror();
                selected_ = completedTarget;
                rebuild();
                emitCommand(QString("MIRROR 按自定义轴 %1 → %2 完成  %3")
                    .arg(pointText(axisFirst), pointText(p),
                         lineDetails(completedTarget, lines[completedTarget])));
                setLive("命令: MIRROR（请选择对象）");
            } else {
                setLive("MIRROR 镜像轴两点不能重合，请重新指定第二点");
            }
            return;
        }

        if (tool == Tool::Rotate) {
            if (rotateTarget_ < 0) {
                const int index = hitLine(p);
                if (index < 0) return;
                selected_ = index;
                rotateTarget_ = index;
                rebuild();
                emitCommand(QString("SELECT %1").arg(lineDetails(index, lines[index])));
                setLive("ROTATE 已选择直线；请单击指定旋转中心");
                return;
            }
            if (!rotateHasPivot_) {
                center_ = p;
                rotateHasPivot_ = true;
                rebuild();
                setLive(QString("ROTATE 旋转中心 %1；请从中心以外按住并拖动鼠标确定角度")
                    .arg(pointText(center_)));
                return;
            }
            if (QLineF(center_, p).length() < 1.0 / std::max(transform().m11(), 0.01)) {
                setLive("ROTATE 请从旋转中心以外的位置开始拖动");
                return;
            }
            active_ = rotateTarget_;
            before_ = lines;
            dragStart_ = p;
            startAngle_ = std::atan2(p.y() - center_.y(), p.x() - center_.x());
            dragAngleRadians_ = 0.0;
            dragging_ = true;
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
        emitCommand(QString("SELECT %1").arg(lineDetails(index, lines[index])));

        if (tool == Tool::Mirror && mirrorAxis_ == MirrorAxis::Custom) {
            mirrorTarget_ = index;
            setLive("MIRROR 已选择对象；请指定镜像轴的第一个点");
            return;
        }

        if (tool == Tool::Delete) {
            const QString deleted = lineDetails(index, lines[index]);
            const int id = lines[index].id;
            selected_ = -1;
            commit(std::make_unique<minicad::DeleteLineCommand>(id));
            emitCommand(QString("ERASE 已删除  %1").arg(deleted));
            return;
        }
        if (tool == Tool::Mirror) {
            const auto [axisA, axisB] = presetAxis();
            commit(std::make_unique<minicad::MirrorLineCommand>(
                lines[index].id, toModelPoint(axisA), toModelPoint(axisB)));
            emitCommand(QString("MIRROR 关于%1镜像完成  %2")
                .arg(mirrorAxisName(), lineDetails(index, lines[index])));
            return;
        }

        active_ = index; before_ = lines; dragStart_ = p;
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
            lines[active_].a = before_[active_].a + delta;
            lines[active_].b = before_[active_].b + delta;
            setLive(QString("%1  ΔX=%2  ΔY=%3  距离=%4")
                .arg(tool == Tool::Move ? "MOVE 正在移动" : "拖动对象")
                .arg(delta.x(), 0, 'f', 2).arg(delta.y(), 0, 'f', 2)
                .arg(std::hypot(delta.x(), delta.y()), 0, 'f', 2));
        } else if (tool == Tool::Rotate) {
            const double angle = std::atan2(p.y() - center_.y(), p.x() - center_.x()) - startAngle_;
            dragAngleRadians_ = angle;
            auto rotate = [&](QPointF q) {
                const QPointF v = q - center_; const double c = std::cos(angle), s = std::sin(angle);
                return center_ + QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
            };
            lines[active_].a = rotate(before_[active_].a);
            lines[active_].b = rotate(before_[active_].b);
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
            if (lines != before_) {
                if (tool == Tool::Move || tool == Tool::Select) {
                    const QPointF delta = lines[active_].a - before_[active_].a;
                    emitCommand(QString("%1 完成  ΔX=%2  ΔY=%3  距离=%4  |  %5")
                        .arg(tool == Tool::Move ? "MOVE" : "DRAG")
                        .arg(delta.x(), 0, 'f', 2).arg(delta.y(), 0, 'f', 2)
                        .arg(std::hypot(delta.x(), delta.y()), 0, 'f', 2)
                        .arg(lineDetails(active_, lines[active_])));
                } else if (tool == Tool::Rotate) {
                    const QLineF oldLine(before_[active_].a, before_[active_].b);
                    const QLineF newLine(lines[active_].a, lines[active_].b);
                    double angle = newLine.angle() - oldLine.angle();
                    if (angle > 180.0) angle -= 360.0;
                    if (angle < -180.0) angle += 360.0;
                    emitCommand(QString("ROTATE 完成  角度=%1°  |  %2")
                        .arg(angle, 0, 'f', 2).arg(lineDetails(active_, lines[active_])));
                }
                if (tool == Tool::Rotate) {
                    commit(std::make_unique<minicad::RotateLineCommand>(
                        before_[active_].id, toModelPoint(center_),
                        dragAngleRadians_ * 180.0 / std::acos(-1.0)));
                    resetRotation();
                    rebuild();
                } else {
                    const QPointF delta = lines[active_].a - before_[active_].a;
                    commit(std::make_unique<minicad::MoveLineCommand>(
                        before_[active_].id, toModelPoint(delta)));
                }
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
    static minicad::Point toModelPoint(QPointF p) { return {p.x(), p.y()}; }

    std::pair<QPointF, QPointF> presetAxis() const {
        const QPointF origin(0, 0);
        switch (mirrorAxis_) {
            case MirrorAxis::XAxis: return {origin, QPointF(1, 0)};
            case MirrorAxis::YAxis: return {origin, QPointF(0, 1)};
            case MirrorAxis::DiagonalPositive: return {origin, QPointF(1, 1)};
            case MirrorAxis::DiagonalNegative: return {origin, QPointF(1, -1)};
            case MirrorAxis::Custom: return {origin, QPointF(1, 0)};
        }
        return {origin, QPointF(1, 0)};
    }

    void syncFromModel() {
        lines.clear();
        for (const auto& line : model_.visibleLines()) {
            lines.push_back({line.id, QPointF(line.start.x, line.start.y),
                             QPointF(line.end.x, line.end.y)});
        }
        rebuild();
    }

    void commit(std::unique_ptr<minicad::Command> command) {
        history_.execute(std::move(command), model_);
        syncFromModel();
        notify();
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

    void resetInteraction() {
        selected_ = -1;
        active_ = -1;
        dragging_ = false;
        hasFirst_ = false;
        hasPreview_ = false;
        resetRotation();
        resetCustomMirror();
    }

    void resetRotation() {
        rotateTarget_ = -1;
        rotateHasPivot_ = false;
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

    void notify() { if (changed) changed(); }
    void emitCommand(const QString& text) { if (command) command(text); }
    void setLive(const QString& text) { if (liveStatus) liveStatus(text); }

    bool hasFirst_ = false, hasPreview_ = false, panning_ = false, dragging_ = false;
    bool mirrorHasFirstAxis_ = false, hasMirrorHover_ = false, rotateHasPivot_ = false;
    int active_ = -1, selected_ = -1, mirrorTarget_ = -1, rotateTarget_ = -1;
    QPointF first_, previewSecond_, dragStart_, center_, mirrorAxisFirst_, mirrorAxisHover_;
    MirrorAxis mirrorAxis_ = MirrorAxis::XAxis;
    double startAngle_ = 0, dragAngleRadians_ = 0;
    QPoint panLast_;
    QVector<CadLine> before_;
    minicad::LineModel model_;
    minicad::History history_;
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
            view_->undo();
            appendCommand("UNDO 已撤销上一步操作");
        });
        connect(redo_, &QAction::triggered, this, [this] {
            view_->redo();
            appendCommand("REDO 已恢复下一步操作");
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
        undo_->setEnabled(view_->canUndo()); redo_->setEnabled(view_->canRedo());
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
