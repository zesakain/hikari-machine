/// @file generational_handle.h
/// @brief Lightweight 64-bit handle primitive with generational stale-reference detection.
///
/// Purpose:
/// Provides a stable, non-owning reference to an indexed resource or entity slot.
/// Combines a 32-bit slot index with a 32-bit generation counter to detect stale references
/// when slots are reused across resource lifecycles.
///
/// Generation Semantics and Stale-Handle Detection:
/// - A resource subsystem or registry maintains a monotonic generation counter per slot.
/// - When an entity or resource is allocated, a handle is minted with the slot's current
/// generation.
/// - When the entity or resource is released or destroyed, the slot's generation counter is
/// incremented.
/// - Any subsequent lookup comparing a previously minted handle's generation against the slot's
///   current generation will mismatch, allowing safe detection of stale handles in O(1) time
///   without accessing dead or reassigned data.
///
/// Ownership and Lifetime:
/// GenerationalHandle is a pure value type (8 bytes). It owns NO memory, contains no pointers,
/// and performs no registry lookup or lifetime management on its own.
///
/// Thread-safety:
/// Value type with pure constexpr operations. Safe to copy and compare concurrently across any
/// thread.
///
/// Performance:
/// 64-bit POD struct passing in a single CPU register. Zero heap allocation.

#pragma once

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace eng {

/// @struct GenerationalHandle
/// @brief 64-bit composite handle consisting of a 32-bit index and a 32-bit generation.
struct GenerationalHandle {
    /// @brief Sentinel value indicating an unassigned or invalid slot index.
    static constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

    /// @brief Sentinel value indicating an unassigned or invalid generation.
    static constexpr uint32_t kInvalidGeneration = 0;

    /// @brief Zero-based slot or array index.
    uint32_t index{kInvalidIndex};

    /// @brief Monotonically increasing lifecycle generation counter (non-zero for valid handles).
    uint32_t generation{kInvalidGeneration};

    /// @brief Constructs an invalid handle sentinel.
    constexpr GenerationalHandle() noexcept = default;

    /// @brief Constructs a handle with an explicit index and generation.
    ///
    /// @param idx Zero-based slot index.
    /// @param gen Slot lifecycle generation counter.
    constexpr GenerationalHandle(uint32_t idx, uint32_t gen) noexcept
        : index(idx), generation(gen) {}

    /// @brief Checks whether the handle represents a potentially valid slot.
    ///
    /// Invariants: A handle is structurally valid if its index is not kInvalidIndex and its
    /// generation is not kInvalidGeneration (0).
    /// Note: Does not verify whether the referenced resource is currently alive in a registry.
    ///
    /// @return True if index != kInvalidIndex and generation != kInvalidGeneration.
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return index != kInvalidIndex && generation != kInvalidGeneration;
    }

    /// @brief Explicit conversion to bool checking validity.
    ///
    /// Prevents ambiguous arithmetic conversions while enabling idiomatic `if (handle)` checks.
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

    /// @brief Default equality and comparison operators.
    [[nodiscard]] constexpr bool operator==(const GenerationalHandle&) const noexcept = default;

    /// @brief Returns a canonical invalid handle sentinel.
    [[nodiscard]] static constexpr GenerationalHandle invalid() noexcept {
        return GenerationalHandle{kInvalidIndex, kInvalidGeneration};
    }

    /// @brief Packs index and generation into a contiguous 64-bit integer.
    ///
    /// Useful for hashing, lock-free atomics, or bitwise storage.
    /// Layout: High 32 bits = generation, Low 32 bits = index.
    [[nodiscard]] constexpr uint64_t toBits() const noexcept {
        return (static_cast<uint64_t>(generation) << 32) | static_cast<uint64_t>(index);
    }

    /// @brief Reconstructs a GenerationalHandle from packed 64-bit integer bits.
    ///
    /// @param bits Contiguous 64-bit integer formatted by toBits().
    /// @return Reconstructed handle instance.
    [[nodiscard]] static constexpr GenerationalHandle fromBits(uint64_t bits) noexcept {
        return GenerationalHandle{static_cast<uint32_t>(bits & 0xFFFF'FFFFULL),
                                  static_cast<uint32_t>(bits >> 32)};
    }
};

static_assert(sizeof(GenerationalHandle) == 8,
              "GenerationalHandle must be exactly 8 bytes (64 bits)");
static_assert(std::is_trivially_copyable_v<GenerationalHandle>,
              "GenerationalHandle must be trivially copyable");
static_assert(std::is_standard_layout_v<GenerationalHandle>,
              "GenerationalHandle must have standard layout");

} // namespace eng
