#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

// Each node belongs to both the chronological system chain and its entity chain.
// Ownership is separate from the four non-owning cross links.
template <class Geometry>
class VersionHistory {
public:
    VersionHistory() = default;
    VersionHistory(const VersionHistory&) = delete;
    VersionHistory& operator=(const VersionHistory&) = delete;
    VersionHistory(VersionHistory&&) = delete;
    VersionHistory& operator=(VersionHistory&&) = delete;
    struct Version {
        int id;
        Geometry geometry;
        bool deleted;
        std::size_t operation;
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

    const Version& require(int id) const {
        auto it = entityTails_.find(id);
        const Version* node = it == entityTails_.end() ? nullptr : it->second;
        while (node && node->operation > currentOperation_) node = node->entityPrev;
        if (!node) throw std::invalid_argument("unknown line id at current operation");
        return *node;
    }

    const Version& requireVisible(int id) const {
        const auto& node = require(id);
        if (node.deleted) throw std::invalid_argument("line is deleted");
        return node;
    }

    std::vector<Version> visibleVersions() const {
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

    std::size_t systemOperation() const { return currentOperation_; }
    std::size_t undoCount() const { return currentOperation_; }
    std::size_t redoCount() const { return nodes_.size() - currentOperation_; }

private:
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

    std::vector<std::unique_ptr<Version>> nodes_;
    std::map<int, Version*> entityTails_;
    Version* head_ = nullptr;
    Version* cursor_ = nullptr;
    std::size_t currentOperation_ = 0;
    int nextId_ = 1;
};
