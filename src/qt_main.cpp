#include <QApplication>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QToolBar>
#include <QActionGroup>
#include <QStatusBar>
#include <QScrollBar>
#include <QVector>
#include <cmath>
#include <algorithm>
#include <functional>

struct CadLine { QPointF a, b; };

static bool operator==(const CadLine& lhs, const CadLine& rhs) {
    return lhs.a == rhs.a && lhs.b == rhs.b;
}

enum class Tool { Select, Create, Move, Rotate, Mirror, Delete };

class CadView final : public QGraphicsView {
public:
    explicit CadView(QWidget* parent = nullptr) : QGraphicsView(parent) {
        setScene(new QGraphicsScene(this));
        setRenderHint(QPainter::Antialiasing);
        setDragMode(QGraphicsView::NoDrag);
        setSceneRect(-1000, -1000, 2000, 2000);
        setBackgroundBrush(QColor("#20242b"));
    }

    QVector<CadLine> lines;
    Tool tool = Tool::Select;
    std::function<void()> changed;

    void rebuild() {
        scene()->clear();
        for (int i = 0; i < lines.size(); ++i) {
            auto* item = scene()->addLine(QLineF(lines[i].a, lines[i].b), QPen(QColor("#f3c74f"), 2));
            item->setData(0, i);
        }
        viewport()->update();
    }

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override {
        painter->fillRect(rect, QColor("#20242b"));
        const int step = 20;
        QPen grid(QColor("#343b46"), 0);
        painter->setPen(grid);
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
            if (!hasFirst_) { first_ = p; hasFirst_ = true; }
            else { lines.push_back({first_, p}); hasFirst_ = false; rebuild(); notify(); }
            return;
        }
        int index = hitLine(p);
        if (index < 0) return;
        if (tool == Tool::Delete) { before_ = lines; lines.remove(index); rebuild(); notify(); return; }
        active_ = index; before_ = lines; dragStart_ = p;
        if (tool == Tool::Rotate) {
            const auto& line = lines[index]; center_ = (line.a + line.b) / 2.0;
            startAngle_ = std::atan2(p.y() - center_.y(), p.x() - center_.x());
        }
        dragging_ = true;
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (panning_) {
            const QPoint delta = event->pos() - panLast_; panLast_ = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y()); return;
        }
        if (!dragging_ || active_ < 0) return;
        const QPointF p = mapToScene(event->pos());
        if (tool == Tool::Move || tool == Tool::Select) {
            const QPointF d = p - dragStart_;
            lines[active_].a = before_[active_].a + d; lines[active_].b = before_[active_].b + d;
        } else if (tool == Tool::Rotate) {
            const double angle = std::atan2(p.y() - center_.y(), p.x() - center_.x()) - startAngle_;
            auto rotate = [&](QPointF q) { const QPointF v = q - center_; const double c = std::cos(angle), s = std::sin(angle); return center_ + QPointF(v.x()*c-v.y()*s, v.x()*s+v.y()*c); };
            lines[active_].a = rotate(before_[active_].a); lines[active_].b = rotate(before_[active_].b);
        } else if (tool == Tool::Mirror) {
            const double axis = 0.0;
            lines[active_].a = QPointF(before_[active_].a.x(), 2 * axis - before_[active_].a.y());
            lines[active_].b = QPointF(before_[active_].b.x(), 2 * axis - before_[active_].b.y());
        }
        rebuild();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) { panning_ = false; unsetCursor(); return; }
        if (event->button() == Qt::LeftButton && dragging_) { dragging_ = false; if (lines != before_) notify(); }
    }

    void wheelEvent(QWheelEvent* event) override {
        const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        scale(factor, factor);
    }

private:
    int hitLine(QPointF p) const {
        int result = -1; double best = 10.0 / transform().m11();
        for (int i = 0; i < lines.size(); ++i) {
            const QLineF l(lines[i].a, lines[i].b); const QPointF v = l.p2() - l.p1();
            const double lengthSquared = QPointF::dotProduct(v, v);
            const double t = lengthSquared > 1e-12 ? std::clamp(QPointF::dotProduct(p - l.p1(), v) / lengthSquared, 0.0, 1.0) : 0.0;
            const double d = QLineF(p, l.p1() + t * v).length();
            if (d < best) { best = d; result = i; }
        }
        return result;
    }
    void notify() { if (changed) changed(); }
    bool hasFirst_ = false, panning_ = false, dragging_ = false;
    int active_ = -1; QPointF first_, dragStart_, center_; double startAngle_ = 0; QPoint panLast_;
    QVector<CadLine> before_;
};

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        view_ = new CadView(this); setCentralWidget(view_); resize(1100, 720); setWindowTitle("MiniCAD - Qt 2D");
        auto* bar = addToolBar("Tools"); bar->setMovable(false);
        QActionGroup* group = new QActionGroup(this); group->setExclusive(true);
        addTool("Select", Tool::Select, bar, group); addTool("Line", Tool::Create, bar, group); addTool("Move", Tool::Move, bar, group); addTool("Rotate", Tool::Rotate, bar, group); addTool("Mirror", Tool::Mirror, bar, group); addTool("Delete", Tool::Delete, bar, group);
        bar->addSeparator(); undo_ = bar->addAction("Undo"); redo_ = bar->addAction("Redo"); undo_->setShortcut(QKeySequence::Undo); redo_->setShortcut(QKeySequence::Redo);
        connect(undo_, &QAction::triggered, this, [this] { if (!undoStates_.isEmpty()) { redoStates_.push_back(view_->lines); view_->lines = undoStates_.takeLast(); view_->rebuild(); updateHistory(); } });
        connect(redo_, &QAction::triggered, this, [this] { if (!redoStates_.isEmpty()) { undoStates_.push_back(view_->lines); view_->lines = redoStates_.takeLast(); view_->rebuild(); updateHistory(); } });
        view_->changed = [this] { undoStates_.push_back(lastState_); redoStates_.clear(); lastState_ = view_->lines; updateHistory(); };
        lastState_ = view_->lines; updateHistory(); statusBar()->showMessage("Ready");
    }
private:
    void addTool(const QString& text, Tool tool, QToolBar* bar, QActionGroup* group) { auto* a = bar->addAction(text); a->setCheckable(true); group->addAction(a); if (tool == Tool::Select) a->setChecked(true); connect(a, &QAction::triggered, this, [this, tool] { view_->tool = tool; }); }
    void updateHistory() { undo_->setEnabled(!undoStates_.isEmpty()); redo_->setEnabled(!redoStates_.isEmpty()); }
    CadView* view_ = nullptr; QAction* undo_ = nullptr; QAction* redo_ = nullptr; QVector<CadLine> lastState_; QVector<QVector<CadLine>> undoStates_, redoStates_;
};

int main(int argc, char** argv) { QApplication app(argc, argv); MainWindow window; window.show(); return app.exec(); }
