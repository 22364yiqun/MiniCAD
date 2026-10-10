#include "version_history.h"
#include <iostream>
#include <random>
#include <string>

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// Independent full-state oracle is used only in tests, never in production history.
int main() {
    try {
        VersionHistory<int> history;
        std::vector<std::map<int, int>> states(1);
        std::size_t position = 0;
        std::mt19937 random(20261003);
        auto verify = [&] {
            std::map<int, int> actual;
            for (const auto& v : history.visibleVersions()) {
                check(actual.emplace(v.id, v.geometry).second, "duplicate entity");
                check(v.operation <= position, "future version visible");
                const auto* node = &history.require(v.id);
                for (auto* p = node; p; p = p->entityPrev) {
                    check(p->id == v.id, "entity chain mixes IDs");
                    if (p->entityPrev) {
                        check(p->entityPrev->entityNext == p, "broken entity back link");
                        check(p->entityPrev->operation < p->operation, "entity order");
                    }
                }
                while (node->systemPrev) node = node->systemPrev;
                std::size_t count = 0;
                for (auto* p = node; p; p = p->systemNext) {
                    check(p->operation == ++count, "system sequence");
                    if (p->systemNext)
                        check(p->systemNext->systemPrev == p, "broken system back link");
                }
                check(count == states.size() - 1, "abandoned system node retained");
            }
            check(actual == states[position], "state differs from snapshot oracle");
            check(history.systemOperation() == position, "wrong system operation");
            check(history.redoCount() == states.size() - 1 - position, "wrong redo count");
        };
        for (int step = 0; step < 4000; ++step) {
            const auto action = random() % 6;
            if (action == 0) {
                check(history.undo() == (position > 0), "undo boundary");
                if (position > 0) --position;
            } else if (action == 1) {
                const bool canRedo = position + 1 < states.size();
                check(history.redo() == canRedo, "redo boundary");
                if (canRedo) ++position;
            } else if (action == 2) {
                const auto oldPosition = history.systemOperation();
                const auto oldRedo = history.redoCount();
                try {
                    history.update(-1, 99);
                    throw std::runtime_error("invalid update accepted");
                } catch (const std::invalid_argument&) {}
                check(history.systemOperation() == oldPosition && history.redoCount() == oldRedo,
                      "invalid edit changed history");
            } else {
                auto state = states[position];
                if (action == 3 || state.empty()) {
                    const int value = static_cast<int>(random() % 1000);
                    state[history.create(value)] = value;
                } else {
                    auto it = state.begin();
                    std::advance(it, random() % state.size());
                    if (action == 4) {
                        history.erase(it->first);
                        check(history.require(it->first).deleted, "missing deletion marker");
                        try {
                            history.update(it->first, 99);
                            throw std::runtime_error("deleted edit accepted");
                        } catch (const std::invalid_argument&) {}
                        state.erase(it);
                    } else {
                        it->second = static_cast<int>(random() % 1000);
                        history.update(it->first, it->second);
                    }
                }
                states.resize(position + 1);
                states.push_back(std::move(state));
                ++position;
            }
            verify();
        }
        while (position) { check(history.undo(), "undo all"); --position; verify(); }
        check(!history.undo(), "undo below zero");
        while (position + 1 < states.size()) { check(history.redo(), "redo all"); ++position; verify(); }
        check(!history.redo(), "redo beyond tail");
        while (history.undo()) {}
        const int id = history.create(42);
        check(history.visibleVersions().size() == 1 && history.redoCount() == 0,
              "branch at zero retained old entities");
        check(history.require(id).operation == 1, "new branch operation");
        check(history.undo() && history.visibleVersions().empty(), "undo create");
        check(history.redo() && history.visibleVersions().size() == 1, "redo create");
        std::cout << "4000 oracle steps and chain/boundary checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
