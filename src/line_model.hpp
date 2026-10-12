#pragma once

#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace minicad {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEpsilon = 1e-9;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

    // 运算符重载
inline Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
inline Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
inline Point operator*(Point p, double scale) { return {p.x * scale, p.y * scale}; }

inline bool nearlyEqual(double a, double b) {
    return std::abs(a - b) <= kEpsilon;
}

struct LineEntity {
    int id = 0;
    Point start;
    Point end;
    bool deleted = false;
};

    // 重命名 标准键值对格式
using ModelState = std::map<int, LineEntity>;

    // 问题就是这个next_id合理吗
class LineModel {
public:
    int create(Point start, Point end) {
        // 创建直线
        const int id = nextId_++;
        lines_[id] = LineEntity{id, start, end, false};
        return id;
    }

    void erase(int id) {
        // 删除直线
        auto& line = require(id);
        line.deleted = true;
    }

    void setLine(int id, Point start, Point end, bool deleted = false) {
        // 修改指定直线的对应状态
        auto& line = require(id);
        line.start = start;
        line.end = end;
        line.deleted = deleted;
    }

    const LineEntity& require(int id) const {
        // 只读访问，不可修改直线
        auto it = lines_.find(id);
        if (it == lines_.end()) {
            // 未找到
            throw std::invalid_argument("unknown line id");
        }
        return it->second;
    }

    LineEntity& require(int id) {
        // 可修改版本
        return const_cast<LineEntity&>(std::as_const(*this).require(id));
    }

    // 复制直线map
    ModelState snapshot() const { return lines_; }

    // lines_ == state
    void restore(ModelState state) { lines_ = std::move(state); }

    // 返回末尾id
    int nextId() const { return nextId_; }

    void setNextId(int value) { nextId_ = value; }

    std::vector<LineEntity> visibleLines() const {
        //获取可见的直线
        std::vector<LineEntity> result;
        for (const auto& [id, line] : lines_) {
            (void)id;
            if (!line.deleted) {
                result.push_back(line);
            }
        }
        return result;
    }

private:
    ModelState lines_; // 存放直线
    int nextId_ = 1;
};

struct FullState {
    ModelState lines;
    int nextId = 1;
};

inline FullState capture(const LineModel& model) {
    // 拍下当前 LineModel 的完整快照
    return {model.snapshot(), model.nextId()};
}

inline void restore(LineModel& model, FullState state) {
    model.restore(std::move(state.lines));
    model.setNextId(state.nextId);
}

class Command {
    // 抽象接口
public:
    virtual ~Command() = default;
    virtual const char* name() const = 0;
    virtual void execute(LineModel&) = 0;
    virtual void undo(LineModel&) = 0;
    virtual void redo(LineModel&) = 0;
};

class StateCommand : public Command {
    // 实现撤销逻辑
public:
    void execute(LineModel& model) final {
        before_ = capture(model);
        apply(model);
        after_ = capture(model);
    }

    void undo(LineModel& model) final {
        if (!before_) {
            throw std::logic_error("command was not executed");
        }
        restore(model, *before_);
    }

    void redo(LineModel& model) final {
        if (!after_) {
            throw std::logic_error("command was not executed");
        }
        restore(model, *after_);
    }

protected:
    virtual void apply(LineModel&) = 0;

private:
    std::optional<FullState> before_;
    std::optional<FullState> after_;
};

class CreateLineCommand final : public StateCommand {
public:
    CreateLineCommand(Point start, Point end) : start_(start), end_(end) {}
    const char* name() const override { return "create"; }

protected:
    void apply(LineModel& model) override { model.create(start_, end_); }

private:
    Point start_;
    Point end_;
};

class DeleteLineCommand final : public StateCommand {
public:
    explicit DeleteLineCommand(int id) : id_(id) {}
    const char* name() const override { return "delete"; }

protected:
    void apply(LineModel& model) override { model.erase(id_); }

private:
    int id_;
};

class MoveLineCommand final : public StateCommand {
public:
    MoveLineCommand(int id, Point delta) : id_(id), delta_(delta) {}
    const char* name() const override { return "move"; }

protected:
    void apply(LineModel& model) override {
        const auto line = model.require(id_);
        model.setLine(id_, line.start + delta_, line.end + delta_, line.deleted);
    }

private:
    int id_;
    Point delta_;
};

inline Point rotatePoint(Point p, Point center, double radians) {
    const Point q = p - center;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return center + Point{q.x * c - q.y * s, q.x * s + q.y * c};
}

class RotateLineCommand final : public StateCommand {
public:
    RotateLineCommand(int id, Point center, double degrees)
        : id_(id), center_(center), radians_(degrees * kPi / 180.0) {}
    const char* name() const override { return "rotate"; }

protected:
    void apply(LineModel& model) override {
        const auto line = model.require(id_);
        model.setLine(id_, rotatePoint(line.start, center_, radians_),
                      rotatePoint(line.end, center_, radians_), line.deleted);
    }

private:
    int id_;
    Point center_;
    double radians_;
};

inline Point mirrorPoint(Point point, Point axisStart, Point axisEnd) {
    const Point axis = axisEnd - axisStart;
    const double lengthSquared = axis.x * axis.x + axis.y * axis.y;
    if (lengthSquared <= kEpsilon) {
        throw std::invalid_argument("mirror axis must have two distinct points");
    }
    const Point relative = point - axisStart;
    const double projectionScale = (relative.x * axis.x + relative.y * axis.y) / lengthSquared;
    const Point projection = axisStart + axis * projectionScale;
    return projection * 2.0 - point;
}

class MirrorLineCommand final : public StateCommand {
public:
    MirrorLineCommand(int id, Point axisStart, Point axisEnd)
        : id_(id), axisStart_(axisStart), axisEnd_(axisEnd) {}
    const char* name() const override { return "mirror"; }

protected:
    void apply(LineModel& model) override {
        const auto line = model.require(id_);
        model.setLine(id_, mirrorPoint(line.start, axisStart_, axisEnd_),
                      mirrorPoint(line.end, axisStart_, axisEnd_), line.deleted);
    }

private:
    int id_;
    Point axisStart_;
    Point axisEnd_;
};

class History {
public:
    void execute(std::unique_ptr<Command> command, LineModel& model) {
        command->execute(model);
        undo_.push_back(std::move(command));
        redo_.clear();
    }

    bool undo(LineModel& model) {
        if (undo_.empty()) return false;
        auto command = std::move(undo_.back());
        undo_.pop_back();
        command->undo(model);
        redo_.push_back(std::move(command));
        return true;
    }

    bool redo(LineModel& model) {
        if (redo_.empty()) return false;
        auto command = std::move(redo_.back());
        redo_.pop_back();
        command->redo(model);
        undo_.push_back(std::move(command));
        return true;
    }

    std::size_t undoCount() const { return undo_.size(); }
    std::size_t redoCount() const { return redo_.size(); }

private:
    std::vector<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
};

}  // namespace minicad
