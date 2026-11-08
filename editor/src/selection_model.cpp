/// @file selection_model.cpp
/// @brief Implementation of the centralized editor SelectionModel.

#include "editor/selection_model.h"

#include "world/world.h"

#include <algorithm>

namespace eng::editor {

void SelectionModel::select(world::EntityHandle entity, bool multiSelect) {
    if (!entity.isValid()) {
        if (!multiSelect) {
            clear();
        }
        return;
    }

    if (!multiSelect) {
        if (selectedEntities_.size() == 1 && selectedEntities_.front() == entity &&
            primarySelected_ == entity) {
            return; // No change
        }
        selectedEntities_.clear();
        selectedEntities_.push_back(entity);
        primarySelected_ = entity;
        hasChanged_ = true;
    } else {
        auto it = std::find(selectedEntities_.begin(), selectedEntities_.end(), entity);
        if (it == selectedEntities_.end()) {
            selectedEntities_.push_back(entity);
            primarySelected_ = entity;
            hasChanged_ = true;
        } else {
            primarySelected_ = entity;
        }
    }
}

void SelectionModel::toggle(world::EntityHandle entity) {
    if (!entity.isValid()) {
        return;
    }

    auto it = std::find(selectedEntities_.begin(), selectedEntities_.end(), entity);
    if (it != selectedEntities_.end()) {
        selectedEntities_.erase(it);
        if (primarySelected_ == entity) {
            primarySelected_ =
                selectedEntities_.empty() ? world::EntityHandle{} : selectedEntities_.back();
        }
    } else {
        selectedEntities_.push_back(entity);
        primarySelected_ = entity;
    }
    hasChanged_ = true;
}

void SelectionModel::selectRange(std::span<const world::EntityHandle> orderedEntities,
                                 world::EntityHandle targetEntity) {
    if (orderedEntities.empty() || !targetEntity.isValid()) {
        return;
    }

    // Find index of target
    auto targetIt = std::find(orderedEntities.begin(), orderedEntities.end(), targetEntity);
    if (targetIt == orderedEntities.end()) {
        select(targetEntity, false);
        return;
    }
    size_t targetIdx = static_cast<size_t>(std::distance(orderedEntities.begin(), targetIt));

    // Find index of primary selection if present
    size_t startIdx = targetIdx;
    if (primarySelected_.isValid()) {
        auto primaryIt =
            std::find(orderedEntities.begin(), orderedEntities.end(), primarySelected_);
        if (primaryIt != orderedEntities.end()) {
            startIdx = static_cast<size_t>(std::distance(orderedEntities.begin(), primaryIt));
        }
    }

    size_t minIdx = std::min(startIdx, targetIdx);
    size_t maxIdx = std::max(startIdx, targetIdx);

    selectedEntities_.clear();
    selectedEntities_.reserve(maxIdx - minIdx + 1);

    for (size_t i = minIdx; i <= maxIdx; ++i) {
        selectedEntities_.push_back(orderedEntities[i]);
    }
    primarySelected_ = targetEntity;
    hasChanged_ = true;
}

void SelectionModel::deselect(world::EntityHandle entity) {
    if (!entity.isValid()) {
        return;
    }

    auto it = std::find(selectedEntities_.begin(), selectedEntities_.end(), entity);
    if (it != selectedEntities_.end()) {
        selectedEntities_.erase(it);
        if (primarySelected_ == entity) {
            primarySelected_ =
                selectedEntities_.empty() ? world::EntityHandle{} : selectedEntities_.back();
        }
        hasChanged_ = true;
    }
}

void SelectionModel::clear() noexcept {
    if (!selectedEntities_.empty() || primarySelected_.isValid()) {
        selectedEntities_.clear();
        primarySelected_ = world::EntityHandle{};
        hasChanged_ = true;
    }
}

void SelectionModel::selectAll(std::span<const world::EntityHandle> entities) {
    selectedEntities_.clear();
    selectedEntities_.reserve(entities.size());
    for (const auto& e : entities) {
        if (e.isValid()) {
            selectedEntities_.push_back(e);
        }
    }
    if (!selectedEntities_.empty()) {
        primarySelected_ = selectedEntities_.back();
    } else {
        primarySelected_ = world::EntityHandle{};
    }
    hasChanged_ = true;
}

bool SelectionModel::isSelected(world::EntityHandle entity) const noexcept {
    if (!entity.isValid()) {
        return false;
    }
    return std::find(selectedEntities_.begin(), selectedEntities_.end(), entity) !=
           selectedEntities_.end();
}

void SelectionModel::validate(const world::World& world) {
    bool removed = false;
    auto it = selectedEntities_.begin();
    while (it != selectedEntities_.end()) {
        if (!world.isAlive(*it)) {
            it = selectedEntities_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

    if (!world.isAlive(primarySelected_)) {
        primarySelected_ =
            selectedEntities_.empty() ? world::EntityHandle{} : selectedEntities_.back();
        removed = true;
    }

    if (removed) {
        hasChanged_ = true;
    }
}

} // namespace eng::editor
