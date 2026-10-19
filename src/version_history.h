#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

// Each node belongs to both the chronological system chain and its entity chain.
// Ownership is separate from the four non-owning cross links.

// VersionHistory is a generic class that manages the history of changes to geometric entities.
template <class Geometry>
class VersionHistory {
public:
    VersionHistory() = default;
    VersionHistory(const VersionHistory&) = delete;
    VersionHistory& operator=(const VersionHistory&) = delete;
    VersionHistory(VersionHistory&&) = delete;
    VersionHistory& operator=(VersionHistory&&) = delete;
    struct Version {
        int id; // 这是谁的版本，例如直线 A 的 ID。
        Geometry geometry; // 这个版本的直线端点。
        bool deleted;
        std::size_t operation; // 这个版本的系统操作编号。
        Version* systemPrev = nullptr;
        Version* systemNext = nullptr;
        Version* entityPrev = nullptr;
        Version* entityNext = nullptr;
    };

    int create(const Geometry& geometry) {
        const int id = nextId_;
        append(id, geometry, false);
        ++nextId_; // IDs are never reused, even after abandoning a Redo branch.
        return id;
    }

    void update(int id, const Geometry& geometry) {
        requireVisible(id);
        append(id, geometry, false);
    }

    void erase(int id) {
        const Geometry geometry = requireVisible(id).geometry;
        append(id, geometry, true);
    }
    // 指定 ID 当前生效的版本
    const Version& require(int id) const {
        auto it = entityTails_.find(id);
        const Version* node = it == entityTails_.end() ? nullptr : it->second;
        // 如果最新版本的操作编号比当前历史位置更靠后，说明用户已经 Undo 到它之前，就沿着 entityPrev 往前找。
        while (node && node->operation > currentOperation_) node = node->entityPrev;
        if (!node) throw std::invalid_argument("unknown line id at current operation");
        return *node;
    }

    // 指定 ID 当前是否存在且可见
    const Version& requireVisible(int id) const {
        const auto& node = require(id);
        if (node.deleted) throw std::invalid_argument("line is deleted");
        return node;
    }

    // visibleVersions()
    std::vector<Version> visibleVersions() const {
        //从该图形的最新版本 tail 开始。
        //如果这个版本位于当前历史位置之后（可能因为执行过 Undo），就沿 entityPrev 往前找。
        //找到当前生效的版本后，检查 deleted。只有未删除的版本才放入 result。
        //最后返回所有可见版本。
        std::vector<Version> result;
        for (const auto& [id, tail] : entityTails_) {
            const Version* node = tail;
            while (node && node->operation > currentOperation_) node = node->entityPrev;
            // Choose the latest version first, then check its deletion marker.
            if (node && !node->deleted) result.push_back(*node);
        }
        return result;
    }

    bool undo() {
        if (!cursor_) return false;
        cursor_ = cursor_->systemPrev;
        --currentOperation_;
        return true;
    }

    bool redo() {
        Version* next = cursor_ ? cursor_->systemNext : head_;
        if (!next) return false;
        cursor_ = next;
        ++currentOperation_;
        return true;
    }

    // 返回当前停在第几步
    std::size_t systemOperation() const { return currentOperation_; }
    // 返回还能 Undo 多少次
    std::size_t undoCount() const { return currentOperation_; }
    // 返回还能 Redo 多少次
    std::size_t redoCount() const { return nodes_.size() - currentOperation_; }

private:
    // 把原来还能 Redo 的“未来操作”全部丢掉。
    void discardFuture() {
        while (nodes_.size() > currentOperation_) {
            Version* node = nodes_.back().get();
            if (node->entityPrev) {
                node->entityPrev->entityNext = nullptr;
                entityTails_.at(node->id) = node->entityPrev;
            } else {
                entityTails_.erase(node->id);
            }
            nodes_.pop_back();
        }
        if (cursor_) cursor_->systemNext = nullptr;
        else head_ = nullptr;
    }

    void append(int id, const Geometry& geometry, bool deleted) {
        // Copy geometry before freeing abandoned versions (it may refer to one).
        auto owned = std::make_unique<Version>(Version{id, geometry, deleted,
                                                     currentOperation_ + 1});
        if (nodes_.size() == nodes_.capacity())
            nodes_.reserve(nodes_.capacity() ? nodes_.capacity() * 2 : 8);
        // Allocate the entity entry before mutating the existing branch.
        entityTails_.try_emplace(id, nullptr);
        discardFuture();
        Version* node = owned.get();
        node->systemPrev = cursor_;
        auto& tail = entityTails_.at(id);
        node->entityPrev = tail;
        if (tail) tail->entityNext = node;
        tail = node;
        if (cursor_) cursor_->systemNext = node;
        else head_ = node;
        nodes_.push_back(std::move(owned));
        cursor_ = node;
        ++currentOperation_;
    }

    std::vector<std::unique_ptr<Version>> nodes_; // 拥有所有版本节点；
    std::map<int, Version*> entityTails_; // 最新图形ID最新的节点的映射；
    Version* head_ = nullptr; // 系统链的第一个节点
    Version* cursor_ = nullptr; //当前历史位置对应的节点
    std::size_t currentOperation_ = 0;  //当前停在全局历史的第几步
    int nextId_ = 1;  // 下一条新直线要分配的 ID
};
