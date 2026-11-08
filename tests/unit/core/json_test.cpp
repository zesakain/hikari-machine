/// @file json_test.cpp
/// @brief Unit tests for eng::core::JsonValue parser and serializer.

#include "core/json.h"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "Assertion failed: " #condition " at line " << __LINE__ << std::endl;     \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

int testPrimitives() {
    using namespace eng::core;

    JsonValue nullVal;
    TEST_CHECK(nullVal.isNull());
    TEST_CHECK(nullVal.getType() == JsonType::Null);

    JsonValue boolVal(true);
    TEST_CHECK(boolVal.isBool());
    TEST_CHECK(boolVal.asBool() == true);

    JsonValue numVal(42.5);
    TEST_CHECK(numVal.isNumber());
    TEST_CHECK(numVal.asDouble() == 42.5);
    TEST_CHECK(numVal.asFloat() == 42.5F);
    TEST_CHECK(numVal.asInt() == 42);
    TEST_CHECK(numVal.asUInt64() == 42);

    JsonValue strVal("Hikari Engine");
    TEST_CHECK(strVal.isString());
    TEST_CHECK(strVal.asString() == "Hikari Engine");

    return 0;
}

int testArrayOperations() {
    using namespace eng::core;

    JsonValue arr = JsonValue::makeArray();
    TEST_CHECK(arr.isArray());
    TEST_CHECK(arr.size() == 0);

    arr.pushBack(10);
    arr.pushBack(20.5);
    arr.pushBack("test");
    TEST_CHECK(arr.size() == 3);
    TEST_CHECK(arr[0].asInt() == 10);
    TEST_CHECK(arr[1].asDouble() == 20.5);
    TEST_CHECK(arr[2].asString() == "test");

    return 0;
}

int testObjectOperations() {
    using namespace eng::core;

    JsonValue obj = JsonValue::makeObject();
    TEST_CHECK(obj.isObject());
    TEST_CHECK(obj.size() == 0);

    obj.set("name", "SceneAlpha");
    obj.set("version", 1);
    obj.set("active", true);

    TEST_CHECK(obj.size() == 3);
    TEST_CHECK(obj.contains("name"));
    TEST_CHECK(obj.contains("version"));
    TEST_CHECK(!obj.contains("nonexistent"));

    TEST_CHECK(obj.getString("name") == "SceneAlpha");
    TEST_CHECK(obj.getInt("version") == 1);
    TEST_CHECK(obj.getBool("active") == true);
    TEST_CHECK(obj.getFloat("nonexistent", 5.0F) == 5.0F);

    // Overwrite existing key
    obj.set("version", 2);
    TEST_CHECK(obj.getInt("version") == 2);
    TEST_CHECK(obj.size() == 3);

    return 0;
}

int testSerializationAndParsingRoundTrip() {
    using namespace eng::core;

    JsonValue root = JsonValue::makeObject();
    root.set("formatVersion", 1);

    JsonValue meta = JsonValue::makeObject();
    meta.set("name", "MainScene");
    meta.set("generator", "Hikari Editor");
    root.set("metadata", std::move(meta));

    JsonValue entities = JsonValue::makeArray();
    JsonValue e1 = JsonValue::makeObject();
    e1.set("id", 101);
    e1.set("name", "Player");

    JsonValue comps = JsonValue::makeObject();
    JsonValue transform = JsonValue::makeObject();
    JsonValue pos = JsonValue::makeArray();
    pos.pushBack(0.0);
    pos.pushBack(1.5);
    pos.pushBack(-3.0);
    transform.set("position", std::move(pos));
    comps.set("Transform", std::move(transform));

    e1.set("components", std::move(comps));
    entities.pushBack(std::move(e1));
    root.set("entities", std::move(entities));

    std::string jsonStr = root.serialize(2);
    TEST_CHECK(!jsonStr.empty());

    // Parse it back
    auto parseRes = JsonValue::parse(jsonStr);
    TEST_CHECK(parseRes.hasValue());

    const auto& parsed = parseRes.value();
    TEST_CHECK(parsed.isObject());
    TEST_CHECK(parsed.getInt("formatVersion") == 1);

    const auto* parsedMeta = parsed.getObject("metadata");
    TEST_CHECK(parsedMeta != nullptr);
    TEST_CHECK(parsedMeta->getString("name") == "MainScene");
    TEST_CHECK(parsedMeta->getString("generator") == "Hikari Editor");

    const auto* parsedEntities = parsed.getArray("entities");
    TEST_CHECK(parsedEntities != nullptr);
    TEST_CHECK(parsedEntities->size() == 1);

    const auto& parsedE1 = (*parsedEntities)[0];
    TEST_CHECK(parsedE1.getUInt64("id") == 101);
    TEST_CHECK(parsedE1.getString("name") == "Player");

    const auto* parsedComps = parsedE1.getObject("components");
    TEST_CHECK(parsedComps != nullptr);
    const auto* parsedTransform = parsedComps->getObject("Transform");
    TEST_CHECK(parsedTransform != nullptr);
    const auto* parsedPos = parsedTransform->getArray("position");
    TEST_CHECK(parsedPos != nullptr);
    TEST_CHECK(parsedPos->size() == 3);
    TEST_CHECK((*parsedPos)[0].asDouble() == 0.0);
    TEST_CHECK((*parsedPos)[1].asDouble() == 1.5);
    TEST_CHECK((*parsedPos)[2].asDouble() == -3.0);

    return 0;
}

int testCommentsAndEscapes() {
    using namespace eng::core;

    std::string_view jsonWithComments = R"(
    // Header comment
    {
        /* multi-line
           comment block */
        "title": "Escape \"quotes\" and \nnewlines \ttabs",
        "nested": {
            "value": -123.456,
            "flag": false
        }
    }
    )";

    auto res = JsonValue::parse(jsonWithComments);
    TEST_CHECK(res.hasValue());
    const auto& val = res.value();
    TEST_CHECK(val.getString("title").find("Escape \"quotes\"") != std::string_view::npos);

    const auto* nested = val.getObject("nested");
    TEST_CHECK(nested != nullptr);
    TEST_CHECK(std::abs(nested->getDouble("value") - (-123.456)) < 0.001);
    TEST_CHECK(nested->getBool("flag") == false);

    return 0;
}

int testSyntaxErrorHandling() {
    using namespace eng::core;

    // Empty input
    auto res1 = JsonValue::parse("");
    TEST_CHECK(!res1.hasValue());

    // Unclosed string
    auto res2 = JsonValue::parse("{\"key\": \"unclosed}");
    TEST_CHECK(!res2.hasValue());

    // Missing colon
    auto res3 = JsonValue::parse("{\"key\" \"val\"}");
    TEST_CHECK(!res3.hasValue());

    // Unclosed object
    auto res4 = JsonValue::parse("{\"key\": 12");
    TEST_CHECK(!res4.hasValue());

    // Unclosed array
    auto res5 = JsonValue::parse("[1, 2, 3");
    TEST_CHECK(!res5.hasValue());

    // Trailing garbage
    auto res6 = JsonValue::parse("{\"a\": 1} unexpected");
    TEST_CHECK(!res6.hasValue());

    return 0;
}

} // namespace

int main() {
    if (testPrimitives() != 0)
        return 1;
    if (testArrayOperations() != 0)
        return 1;
    if (testObjectOperations() != 0)
        return 1;
    if (testSerializationAndParsingRoundTrip() != 0)
        return 1;
    if (testCommentsAndEscapes() != 0)
        return 1;
    if (testSyntaxErrorHandling() != 0)
        return 1;

    std::cout << "All JsonValue unit tests passed successfully." << std::endl;
    return 0;
}
