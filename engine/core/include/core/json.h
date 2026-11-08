/// @file json.h
/// @brief Lightweight, exception-free C++20 JSON document model, parser, and serializer.
///
/// Purpose:
/// Provides a zero-dependency, hardware-neutral JSON parser and serializer for scene persistence,
/// configuration schemas, and asset descriptors.
///
/// Ownership and Lifetime:
/// JsonValue is an owning value type. Copying performs deep cloning; moving transfers internal
/// storage without allocations where possible.
///
/// Thread-safety:
/// Const access to distinct or shared JsonValue instances is thread-safe. Concurrent mutation
/// requires external synchronization.
///
/// Performance:
/// Uses std::variant over std::vector, std::string, and primitives. No exceptions and no RTTI.
/// Preserves key insertion ordering in JSON objects via vector of pairs.
///
/// Example:
/// @code
/// auto parseResult = JsonValue::parse("{\"formatVersion\": 1, \"name\": \"Scene\"}");
/// if (parseResult.hasValue()) {
///     const auto& json = parseResult.value();
///     int version = json.getInt("formatVersion");
///     std::string_view name = json.getString("name");
/// }
/// @endcode

#pragma once

#include "core/assert.h"
#include "core/result.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace eng::core {

class JsonValue;

/// Ordered collection of key-value pairs representing a JSON object.
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;

/// Ordered collection of JSON values representing a JSON array.
using JsonArray = std::vector<JsonValue>;

/// @enum JsonType
/// @brief Classification of JSON data types.
enum class JsonType : uint8_t {
    Null = 0,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

/// @class JsonValue
/// @brief Exception-free JSON value supporting parsing, DOM queries, and formatted serialization.
class JsonValue {
public:
    using Storage = std::variant<std::monostate, bool, double, std::string, JsonArray, JsonObject>;

    JsonValue() noexcept : storage_(std::monostate{}) {}
    JsonValue(std::nullptr_t) noexcept : storage_(std::monostate{}) {}
    JsonValue(bool b) noexcept : storage_(b) {}
    JsonValue(int n) noexcept : storage_(static_cast<double>(n)) {}
    JsonValue(unsigned int n) noexcept : storage_(static_cast<double>(n)) {}
    JsonValue(int64_t n) noexcept : storage_(static_cast<double>(n)) {}
    JsonValue(uint64_t n) noexcept : storage_(static_cast<double>(n)) {}
    JsonValue(float f) noexcept : storage_(static_cast<double>(f)) {}
    JsonValue(double d) noexcept : storage_(d) {}
    JsonValue(const char* s) : storage_(std::string(s != nullptr ? s : "")) {}
    JsonValue(std::string s) : storage_(std::move(s)) {}
    JsonValue(std::string_view s) : storage_(std::string(s)) {}
    JsonValue(JsonArray arr) : storage_(std::move(arr)) {}
    JsonValue(JsonObject obj) : storage_(std::move(obj)) {}

    ~JsonValue() = default;
    JsonValue(const JsonValue&) = default;
    JsonValue& operator=(const JsonValue&) = default;
    JsonValue(JsonValue&&) noexcept = default;
    JsonValue& operator=(JsonValue&&) noexcept = default;

    /// Creates an empty JSON array.
    [[nodiscard]] static JsonValue makeArray() { return JsonValue(JsonArray{}); }

    /// Creates an empty JSON object.
    [[nodiscard]] static JsonValue makeObject() { return JsonValue(JsonObject{}); }

    /// Returns the type enumeration of this JSON value.
    [[nodiscard]] JsonType getType() const noexcept {
        switch (storage_.index()) {
        case 0:
            return JsonType::Null;
        case 1:
            return JsonType::Boolean;
        case 2:
            return JsonType::Number;
        case 3:
            return JsonType::String;
        case 4:
            return JsonType::Array;
        case 5:
            return JsonType::Object;
        default:
            return JsonType::Null;
        }
    }

    [[nodiscard]] bool isNull() const noexcept { return storage_.index() == 0; }
    [[nodiscard]] bool isBool() const noexcept { return storage_.index() == 1; }
    [[nodiscard]] bool isNumber() const noexcept { return storage_.index() == 2; }
    [[nodiscard]] bool isString() const noexcept { return storage_.index() == 3; }
    [[nodiscard]] bool isArray() const noexcept { return storage_.index() == 4; }
    [[nodiscard]] bool isObject() const noexcept { return storage_.index() == 5; }

    /// Returns boolean value, or defaultVal if this is not a boolean.
    [[nodiscard]] bool asBool(bool defaultVal = false) const noexcept {
        if (const auto* val = std::get_if<bool>(&storage_)) {
            return *val;
        }
        return defaultVal;
    }

    /// Returns double value, or defaultVal if this is not a number.
    [[nodiscard]] double asDouble(double defaultVal = 0.0) const noexcept {
        if (const auto* val = std::get_if<double>(&storage_)) {
            return *val;
        }
        return defaultVal;
    }

    /// Returns float value, or defaultVal if this is not a number.
    [[nodiscard]] float asFloat(float defaultVal = 0.0F) const noexcept {
        return static_cast<float>(asDouble(static_cast<double>(defaultVal)));
    }

    /// Returns int value, or defaultVal if this is not a number.
    [[nodiscard]] int asInt(int defaultVal = 0) const noexcept {
        return static_cast<int>(asDouble(static_cast<double>(defaultVal)));
    }

    /// Returns int64_t value, or defaultVal if this is not a number.
    [[nodiscard]] int64_t asInt64(int64_t defaultVal = 0) const noexcept {
        return static_cast<int64_t>(asDouble(static_cast<double>(defaultVal)));
    }

    /// Returns uint64_t value, or defaultVal if this is not a number.
    [[nodiscard]] uint64_t asUInt64(uint64_t defaultVal = 0) const noexcept {
        double d = asDouble(static_cast<double>(defaultVal));
        return d >= 0.0 ? static_cast<uint64_t>(d) : 0;
    }

    /// Returns string view value, or defaultVal if this is not a string.
    [[nodiscard]] std::string_view asString(std::string_view defaultVal = "") const noexcept {
        if (const auto* val = std::get_if<std::string>(&storage_)) {
            return *val;
        }
        return defaultVal;
    }

    /// Direct const access to contained array.
    [[nodiscard]] const JsonArray& asArray() const {
        ENG_ASSERT(isArray(), "JsonValue is not an array");
        return std::get<JsonArray>(storage_);
    }

    /// Direct mutable access to contained array.
    [[nodiscard]] JsonArray& asArray() {
        ENG_ASSERT(isArray(), "JsonValue is not an array");
        return std::get<JsonArray>(storage_);
    }

    /// Direct const access to contained object.
    [[nodiscard]] const JsonObject& asObject() const {
        ENG_ASSERT(isObject(), "JsonValue is not an object");
        return std::get<JsonObject>(storage_);
    }

    /// Direct mutable access to contained object.
    [[nodiscard]] JsonObject& asObject() {
        ENG_ASSERT(isObject(), "JsonValue is not an object");
        return std::get<JsonObject>(storage_);
    }

    /// Returns number of elements in array or object (0 for other types).
    [[nodiscard]] size_t size() const noexcept {
        if (const auto* arr = std::get_if<JsonArray>(&storage_)) {
            return arr->size();
        }
        if (const auto* obj = std::get_if<JsonObject>(&storage_)) {
            return obj->size();
        }
        return 0;
    }

    // --- Array Operations ---

    /// Appends a value to the array. Converts this value to an array if it was null.
    void pushBack(JsonValue value) {
        if (isNull()) {
            storage_ = JsonArray{};
        }
        ENG_ASSERT(isArray(), "pushBack called on non-array JsonValue");
        std::get<JsonArray>(storage_).push_back(std::move(value));
    }

    [[nodiscard]] const JsonValue& operator[](size_t index) const {
        const auto& arr = asArray();
        ENG_ASSERT(index < arr.size(), "Array index out of bounds");
        return arr[index];
    }

    [[nodiscard]] JsonValue& operator[](size_t index) {
        auto& arr = asArray();
        ENG_ASSERT(index < arr.size(), "Array index out of bounds");
        return arr[index];
    }

    // --- Object Operations ---

    /// Sets or replaces a key-value pair in the object. Converts this value to an object if null.
    void set(std::string key, JsonValue value) {
        if (isNull()) {
            storage_ = JsonObject{};
        }
        ENG_ASSERT(isObject(), "set called on non-object JsonValue");
        auto& obj = std::get<JsonObject>(storage_);
        for (auto& pair : obj) {
            if (pair.first == key) {
                pair.second = std::move(value);
                return;
            }
        }
        obj.emplace_back(std::move(key), std::move(value));
    }

    /// Finds a member by key in this object, or returns nullptr if not found or not an object.
    [[nodiscard]] const JsonValue* find(std::string_view key) const noexcept {
        if (const auto* obj = std::get_if<JsonObject>(&storage_)) {
            for (const auto& pair : *obj) {
                if (pair.first == key) {
                    return &pair.second;
                }
            }
        }
        return nullptr;
    }

    /// Finds a member by key in this object (mutable), or returns nullptr.
    [[nodiscard]] JsonValue* find(std::string_view key) noexcept {
        if (auto* obj = std::get_if<JsonObject>(&storage_)) {
            for (auto& pair : *obj) {
                if (pair.first == key) {
                    return &pair.second;
                }
            }
        }
        return nullptr;
    }

    /// Checks if this object contains a specific key.
    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        return find(key) != nullptr;
    }

    /// Accesses or inserts a key in the object. Converts this value to an object if null.
    [[nodiscard]] JsonValue& operator[](std::string_view key) {
        if (isNull()) {
            storage_ = JsonObject{};
        }
        ENG_ASSERT(isObject(), "operator[] called on non-object JsonValue");
        auto& obj = std::get<JsonObject>(storage_);
        for (auto& pair : obj) {
            if (pair.first == key) {
                return pair.second;
            }
        }
        obj.emplace_back(std::string(key), JsonValue{});
        return obj.back().second;
    }

    // Convenience property getters with defaults
    [[nodiscard]] std::string_view getString(std::string_view key,
                                             std::string_view defaultVal = "") const noexcept {
        const auto* v = find(key);
        return v != nullptr ? v->asString(defaultVal) : defaultVal;
    }

    [[nodiscard]] double getDouble(std::string_view key, double defaultVal = 0.0) const noexcept {
        const auto* v = find(key);
        return v != nullptr ? v->asDouble(defaultVal) : defaultVal;
    }

    [[nodiscard]] float getFloat(std::string_view key, float defaultVal = 0.0F) const noexcept {
        const auto* v = find(key);
        return v != nullptr ? v->asFloat(defaultVal) : defaultVal;
    }

    [[nodiscard]] int getInt(std::string_view key, int defaultVal = 0) const noexcept {
        const auto* v = find(key);
        return v != nullptr ? v->asInt(defaultVal) : defaultVal;
    }

    [[nodiscard]] int64_t getInt64(std::string_view key, int64_t defaultVal = 0) const noexcept {
        const auto* v = find(key);
        return v != nullptr ? v->asInt64(defaultVal) : defaultVal;
    }

    [[nodiscard]] uint64_t getUInt64(std::string_view key, uint64_t defaultVal = 0) const noexcept {
        const auto* v = find(key);
        return v != nullptr ? v->asUInt64(defaultVal) : defaultVal;
    }

    [[nodiscard]] bool getBool(std::string_view key, bool defaultVal = false) const noexcept {
        const auto* v = find(key);
        return v != nullptr ? v->asBool(defaultVal) : defaultVal;
    }

    [[nodiscard]] const JsonValue* getObject(std::string_view key) const noexcept {
        const auto* v = find(key);
        return (v != nullptr && v->isObject()) ? v : nullptr;
    }

    [[nodiscard]] const JsonValue* getArray(std::string_view key) const noexcept {
        const auto* v = find(key);
        return (v != nullptr && v->isArray()) ? v : nullptr;
    }

    // --- Serialization & Parsing ---

    /// Serializes this JSON value to a formatted string.
    ///
    /// @param indentSpaces Number of spaces per indentation level (0 for compact).
    [[nodiscard]] std::string serialize(int indentSpaces = 2) const;

    /// Parses a JSON text string into a JsonValue DOM.
    ///
    /// Returns Result with JsonValue on success, or Error on parse failure.
    [[nodiscard]] static Result<JsonValue> parse(std::string_view text);

private:
    Storage storage_;
};

} // namespace eng::core
