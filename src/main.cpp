#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEpsilon = 1e-9;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
Point operator*(Point p, double scale) { return {p.x * scale, p.y * scale}; }

bool nearlyEqual(double a, double b) {
    return std::abs(a - b) <= kEpsilon;
}

struct LineEntity {
    int id = 0;
    Point start;
    Point end;
    bool deleted = false;
};

using ModelState = std::map<int, LineEntity>;

class LineModel {
public:
    int create(Point start, Point end) {
        const int id = nextId_++;
        lines_[id] = LineEntity{id, start, end, false};
        return id;
    }

    void erase(int id) {
        auto& line = require(id);
        line.deleted = true;
    }

    void setLine(int id, Point start, Point end, bool deleted = false) {
        auto& line = require(id);
        line.start = start;
        line.end = end;
        line.deleted = deleted;
    }

    const LineEntity& require(int id) const {
        auto it = lines_.find(id);
        if (it == lines_.end()) {
            throw std::invalid_argument("unknown line id");
        }
        return it->second;
    }

    LineEntity& require(int id) {
        return const_cast<LineEntity&>(std::as_const(*this).require(id));
    }

    ModelState snapshot() const { return lines_; }

    void restore(ModelState state) { lines_ = std::move(state); }

    int nextId() const { return nextId_; }

    void setNextId(int value) { nextId_ = value; }

    std::vector<LineEntity> visibleLines() const {
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
    ModelState lines_;
    int nextId_ = 1;
};

struct FullState {
    ModelState lines;
    int nextId = 1;
};

FullState capture(const LineModel& model) {
    return {model.snapshot(), model.nextId()};
}

void restore(LineModel& model, FullState state) {
    model.restore(std::move(state.lines));
    model.setNextId(state.nextId);
}

class Command {
public:
    virtual ~Command() = default;
    virtual const char* name() const = 0;
    virtual void execute(LineModel&) = 0;
    virtual void undo(LineModel&) = 0;
    virtual void redo(LineModel&) = 0;
};

class StateCommand : public Command {
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

Point rotatePoint(Point p, Point center, double radians) {
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

Point mirrorPoint(Point point, Point axisStart, Point axisEnd) {
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

void printModel(const LineModel& model, const History& history) {
    std::cout << std::fixed << std::setprecision(3);
    const auto lines = model.visibleLines();
    if (lines.empty()) {
        std::cout << "(no visible lines)\n";
    } else {
        for (const auto& line : lines) {
            std::cout << "line " << line.id << ": (" << line.start.x << ", "
                      << line.start.y << ") -> (" << line.end.x << ", "
                      << line.end.y << ")\n";
        }
    }
    std::cout << "undo=" << history.undoCount() << " redo=" << history.redoCount() << "\n";
}

double readDouble(std::istringstream& input, const char* label) {
    double value = 0.0;
    if (!(input >> value)) throw std::invalid_argument(std::string("invalid ") + label);
    return value;
}

int readInt(std::istringstream& input, const char* label) {
    int value = 0;
    if (!(input >> value)) throw std::invalid_argument(std::string("invalid ") + label);
    return value;
}

void printHelp() {
    std::cout << "commands:\n"
              << "  create x1 y1 x2 y2\n"
              << "  delete id\n"
              << "  move id dx dy\n"
              << "  rotate id cx cy degrees\n"
              << "  mirror id ax ay bx by\n"
              << "  undo | redo | list | help | quit\n";
}

void runDemoTests() {
    LineModel model;
    History history;
    history.execute(std::make_unique<CreateLineCommand>(Point{0, 0}, Point{2, 0}), model);
    const int id = model.visibleLines().front().id;
    history.execute(std::make_unique<MoveLineCommand>(id, Point{1, 2}), model);
    history.execute(std::make_unique<RotateLineCommand>(id, Point{0, 0}, 90), model);
    if (!history.undo(model) || !history.undo(model) || !history.redo(model)) {
        throw std::runtime_error("history demo failed");
    }
    history.execute(std::make_unique<MirrorLineCommand>(id, Point{0, 0}, Point{0, 1}), model);
    if (history.redoCount() != 0) throw std::runtime_error("redo stack was not cleared");
    history.execute(std::make_unique<DeleteLineCommand>(id), model);
    if (!history.undo(model) || model.visibleLines().size() != 1) {
        throw std::runtime_error("delete undo failed");
    }
    std::cout << "self-test passed\n";
}

}  // namespace

int main() {
    try {
        runDemoTests();
        LineModel model;
        History history;
        std::cout << "Line Undo/Redo demo. Type 'help' for commands.\n";
        std::string line;
        while (std::cout << "> " && std::getline(std::cin, line)) {
            std::istringstream input(line);
            std::string command;
            input >> command;
            if (command.empty()) continue;
            try {
                if (command == "create") {
                    const Point a{readDouble(input, "x1"), readDouble(input, "y1")};
                    const Point b{readDouble(input, "x2"), readDouble(input, "y2")};
                    history.execute(std::make_unique<CreateLineCommand>(a, b), model);
                } else if (command == "delete") {
                    history.execute(std::make_unique<DeleteLineCommand>(readInt(input, "id")), model);
                } else if (command == "move") {
                    const int id = readInt(input, "id");
                    history.execute(std::make_unique<MoveLineCommand>(
                        id, Point{readDouble(input, "dx"), readDouble(input, "dy")}), model);
                } else if (command == "rotate") {
                    const int id = readInt(input, "id");
                    const Point center{readDouble(input, "cx"), readDouble(input, "cy")};
                    history.execute(std::make_unique<RotateLineCommand>(
                        id, center, readDouble(input, "degrees")), model);
                } else if (command == "mirror") {
                    const int id = readInt(input, "id");
                    const Point a{readDouble(input, "ax"), readDouble(input, "ay")};
                    const Point b{readDouble(input, "bx"), readDouble(input, "by")};
                    history.execute(std::make_unique<MirrorLineCommand>(id, a, b), model);
                } else if (command == "undo") {
                    if (!history.undo(model)) std::cout << "nothing to undo\n";
                } else if (command == "redo") {
                    if (!history.redo(model)) std::cout << "nothing to redo\n";
                } else if (command == "list") {
                    printModel(model, history);
                    continue;
                } else if (command == "help") {
                    printHelp();
                    continue;
                } else if (command == "quit" || command == "exit") {
                    break;
                } else {
                    std::cout << "unknown command\n";
                    continue;
                }
                printModel(model, history);
            } catch (const std::exception& error) {
                std::cout << "error: " << error.what() << '\n';
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
