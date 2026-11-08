/// @file json.cpp
/// @brief Lightweight, exception-free C++20 JSON document model, parser, and serializer
/// implementation.

#include "core/json.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace eng::core {

namespace {

void writeIndent(std::string& out, int indentSpaces, int depth) {
    if (indentSpaces > 0) {
        out.append(static_cast<size_t>(depth * indentSpaces), ' ');
    }
}

void serializeString(std::string& out, std::string_view str) {
    out.push_back('"');
    for (char c : str) {
        switch (c) {
        case '"':
            out.append("\\\"");
            break;
        case '\\':
            out.append("\\\\");
            break;
        case '\b':
            out.append("\\b");
            break;
        case '\f':
            out.append("\\f");
            break;
        case '\n':
            out.append("\\n");
            break;
        case '\r':
            out.append("\\r");
            break;
        case '\t':
            out.append("\\t");
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char hexBuf[8];
                std::snprintf(hexBuf, sizeof(hexBuf), "\\u%04x", static_cast<unsigned char>(c));
                out.append(hexBuf);
            } else {
                out.push_back(c);
            }
            break;
        }
    }
    out.push_back('"');
}

void serializeValue(std::string& out, const JsonValue& val, int indentSpaces, int depth) {
    switch (val.getType()) {
    case JsonType::Null:
        out.append("null");
        break;
    case JsonType::Boolean:
        out.append(val.asBool() ? "true" : "false");
        break;
    case JsonType::Number: {
        double d = val.asDouble();
        if (std::isnan(d) || std::isinf(d)) {
            out.append("0");
        } else if (std::floor(d) == d && std::abs(d) < 1e15) {
            char numBuf[32];
            std::snprintf(numBuf, sizeof(numBuf), "%lld", static_cast<long long>(d));
            out.append(numBuf);
        } else {
            char numBuf[64];
            std::snprintf(numBuf, sizeof(numBuf), "%.6g", d);
            out.append(numBuf);
        }
        break;
    }
    case JsonType::String:
        serializeString(out, val.asString());
        break;
    case JsonType::Array: {
        const auto& arr = val.asArray();
        if (arr.empty()) {
            out.append("[]");
            return;
        }
        out.push_back('[');
        if (indentSpaces > 0) {
            out.push_back('\n');
        }
        for (size_t i = 0; i < arr.size(); ++i) {
            if (indentSpaces > 0) {
                writeIndent(out, indentSpaces, depth + 1);
            }
            serializeValue(out, arr[i], indentSpaces, depth + 1);
            if (i + 1 < arr.size()) {
                out.push_back(',');
            }
            if (indentSpaces > 0) {
                out.push_back('\n');
            }
        }
        if (indentSpaces > 0) {
            writeIndent(out, indentSpaces, depth);
        }
        out.push_back(']');
        break;
    }
    case JsonType::Object: {
        const auto& obj = val.asObject();
        if (obj.empty()) {
            out.append("{}");
            return;
        }
        out.push_back('{');
        if (indentSpaces > 0) {
            out.push_back('\n');
        }
        for (size_t i = 0; i < obj.size(); ++i) {
            if (indentSpaces > 0) {
                writeIndent(out, indentSpaces, depth + 1);
            }
            serializeString(out, obj[i].first);
            out.push_back(':');
            if (indentSpaces > 0) {
                out.push_back(' ');
            }
            serializeValue(out, obj[i].second, indentSpaces, depth + 1);
            if (i + 1 < obj.size()) {
                out.push_back(',');
            }
            if (indentSpaces > 0) {
                out.push_back('\n');
            }
        }
        if (indentSpaces > 0) {
            writeIndent(out, indentSpaces, depth);
        }
        out.push_back('}');
        break;
    }
    }
}

// -----------------------------------------------------------------------------
// Recursive-Descent Parser
// -----------------------------------------------------------------------------

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input), pos_(0), line_(1), col_(1) {}

    Result<JsonValue> parse() {
        skipWhitespaceAndComments();
        if (isEof()) {
            return Result<JsonValue>::failure(
                Error(ErrorCode::InvalidArgument, "JSON input is empty"));
        }

        auto res = parseValue();
        if (!res.hasValue()) {
            return res;
        }

        skipWhitespaceAndComments();
        if (!isEof()) {
            return Result<JsonValue>::failure(Error(
                ErrorCode::InvalidArgument, "Unexpected trailing characters after JSON root"));
        }

        return res;
    }

private:
    std::string_view input_;
    size_t pos_{0};
    int line_{1};
    int col_{1};

    [[nodiscard]] bool isEof() const noexcept { return pos_ >= input_.size(); }

    [[nodiscard]] char peek() const noexcept { return pos_ < input_.size() ? input_[pos_] : '\0'; }

    char advance() noexcept {
        if (pos_ < input_.size()) {
            char c = input_[pos_++];
            if (c == '\n') {
                line_++;
                col_ = 1;
            } else {
                col_++;
            }
            return c;
        }
        return '\0';
    }

    void skipWhitespaceAndComments() {
        while (!isEof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                advance();
            } else if (c == '/' && pos_ + 1 < input_.size()) {
                char next = input_[pos_ + 1];
                if (next == '/') {
                    // Single-line comment: skip until newline
                    advance();
                    advance();
                    while (!isEof() && peek() != '\n') {
                        advance();
                    }
                } else if (next == '*') {
                    // Block comment: skip until */
                    advance();
                    advance();
                    while (!isEof()) {
                        if (peek() == '*' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '/') {
                            advance();
                            advance();
                            break;
                        }
                        advance();
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }
    }

    Result<JsonValue> parseValue() {
        skipWhitespaceAndComments();
        if (isEof()) {
            return Result<JsonValue>::failure(
                Error(ErrorCode::InvalidArgument, "Unexpected end of input while parsing value"));
        }

        char c = peek();
        if (c == '{') {
            return parseObject();
        }
        if (c == '[') {
            return parseArray();
        }
        if (c == '"') {
            return parseStringValue();
        }
        if (c == 't' || c == 'f') {
            return parseBool();
        }
        if (c == 'n') {
            return parseNull();
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parseNumber();
        }

        return Result<JsonValue>::failure(
            Error(ErrorCode::InvalidArgument, "Unexpected character encountered in JSON value"));
    }

    Result<JsonValue> parseObject() {
        advance(); // consume '{'
        skipWhitespaceAndComments();

        JsonObject obj;
        if (peek() == '}') {
            advance();
            return Result<JsonValue>::success(JsonValue(std::move(obj)));
        }

        while (!isEof()) {
            skipWhitespaceAndComments();
            if (peek() != '"') {
                return Result<JsonValue>::failure(
                    Error(ErrorCode::InvalidArgument, "Expected string key in object member"));
            }

            auto keyRes = parseStringLiteral();
            if (!keyRes.hasValue()) {
                return Result<JsonValue>::failure(keyRes.error());
            }
            std::string key = std::move(keyRes.value());

            skipWhitespaceAndComments();
            if (peek() != ':') {
                return Result<JsonValue>::failure(
                    Error(ErrorCode::InvalidArgument, "Expected ':' after object member key"));
            }
            advance(); // consume ':'

            auto valRes = parseValue();
            if (!valRes.hasValue()) {
                return valRes;
            }

            obj.emplace_back(std::move(key), std::move(valRes.value()));

            skipWhitespaceAndComments();
            if (peek() == ',') {
                advance(); // consume ','
                skipWhitespaceAndComments();
                // Allow optional trailing comma before '}'
                if (peek() == '}') {
                    advance();
                    return Result<JsonValue>::success(JsonValue(std::move(obj)));
                }
            } else if (peek() == '}') {
                advance(); // consume '}'
                return Result<JsonValue>::success(JsonValue(std::move(obj)));
            } else {
                return Result<JsonValue>::failure(Error(
                    ErrorCode::InvalidArgument, "Expected ',' or '}' after object member value"));
            }
        }

        return Result<JsonValue>::failure(
            Error(ErrorCode::InvalidArgument, "Unclosed object: missing closing '}'"));
    }

    Result<JsonValue> parseArray() {
        advance(); // consume '['
        skipWhitespaceAndComments();

        JsonArray arr;
        if (peek() == ']') {
            advance();
            return Result<JsonValue>::success(JsonValue(std::move(arr)));
        }

        while (!isEof()) {
            auto valRes = parseValue();
            if (!valRes.hasValue()) {
                return valRes;
            }
            arr.push_back(std::move(valRes.value()));

            skipWhitespaceAndComments();
            if (peek() == ',') {
                advance(); // consume ','
                skipWhitespaceAndComments();
                if (peek() == ']') {
                    advance();
                    return Result<JsonValue>::success(JsonValue(std::move(arr)));
                }
            } else if (peek() == ']') {
                advance(); // consume ']'
                return Result<JsonValue>::success(JsonValue(std::move(arr)));
            } else {
                return Result<JsonValue>::failure(
                    Error(ErrorCode::InvalidArgument, "Expected ',' or ']' after array element"));
            }
        }

        return Result<JsonValue>::failure(
            Error(ErrorCode::InvalidArgument, "Unclosed array: missing closing ']'"));
    }

    Result<std::string> parseStringLiteral() {
        if (peek() != '"') {
            return Result<std::string>::failure(
                Error(ErrorCode::InvalidArgument, "Expected '\"' at beginning of string"));
        }
        advance(); // consume opening quote

        std::string result;
        while (!isEof()) {
            char c = advance();
            if (c == '"') {
                return Result<std::string>::success(std::move(result));
            }
            if (c == '\\') {
                if (isEof()) {
                    return Result<std::string>::failure(
                        Error(ErrorCode::InvalidArgument, "Unterminated escape sequence"));
                }
                char esc = advance();
                switch (esc) {
                case '"':
                    result.push_back('"');
                    break;
                case '\\':
                    result.push_back('\\');
                    break;
                case '/':
                    result.push_back('/');
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u': {
                    // Basic 4-hex digit unicode escape support
                    unsigned int codepoint = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (isEof()) {
                            return Result<std::string>::failure(
                                Error(ErrorCode::InvalidArgument, "Incomplete unicode escape"));
                        }
                        char hexChar = advance();
                        codepoint <<= 4;
                        if (hexChar >= '0' && hexChar <= '9') {
                            codepoint |= static_cast<unsigned int>(hexChar - '0');
                        } else if (hexChar >= 'a' && hexChar <= 'f') {
                            codepoint |= static_cast<unsigned int>(hexChar - 'a' + 10);
                        } else if (hexChar >= 'A' && hexChar <= 'F') {
                            codepoint |= static_cast<unsigned int>(hexChar - 'A' + 10);
                        } else {
                            return Result<std::string>::failure(Error(
                                ErrorCode::InvalidArgument, "Invalid hex digit in unicode escape"));
                        }
                    }
                    if (codepoint < 0x80) {
                        result.push_back(static_cast<char>(codepoint));
                    } else if (codepoint < 0x800) {
                        result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                    } else {
                        result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                    }
                    break;
                }
                default:
                    result.push_back(esc);
                    break;
                }
            } else {
                result.push_back(c);
            }
        }

        return Result<std::string>::failure(
            Error(ErrorCode::InvalidArgument, "Unclosed string: missing closing '\"'"));
    }

    Result<JsonValue> parseStringValue() {
        auto strRes = parseStringLiteral();
        if (strRes.hasValue()) {
            return Result<JsonValue>::success(JsonValue(std::move(strRes.value())));
        }
        return Result<JsonValue>::failure(strRes.error());
    }

    Result<JsonValue> parseNumber() {
        size_t start = pos_;
        if (peek() == '-') {
            advance();
        }

        if (!isEof() && peek() == '0') {
            advance();
        } else {
            while (!isEof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        if (!isEof() && peek() == '.') {
            advance();
            while (!isEof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        if (!isEof() && (peek() == 'e' || peek() == 'E')) {
            advance();
            if (!isEof() && (peek() == '+' || peek() == '-')) {
                advance();
            }
            while (!isEof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        std::string_view numSpan = input_.substr(start, pos_ - start);
        double val = 0.0;
        auto convRes = std::from_chars(numSpan.data(), numSpan.data() + numSpan.size(), val);
        if (convRes.ec != std::errc()) {
            // Fallback parsing
            char buf[64];
            size_t copyLen = std::min(numSpan.size(), sizeof(buf) - 1);
            for (size_t i = 0; i < copyLen; ++i) {
                buf[i] = numSpan[i];
            }
            buf[copyLen] = '\0';
            val = std::strtod(buf, nullptr);
        }

        return Result<JsonValue>::success(JsonValue(val));
    }

    Result<JsonValue> parseBool() {
        if (input_.substr(pos_).starts_with("true")) {
            pos_ += 4;
            col_ += 4;
            return Result<JsonValue>::success(JsonValue(true));
        }
        if (input_.substr(pos_).starts_with("false")) {
            pos_ += 5;
            col_ += 5;
            return Result<JsonValue>::success(JsonValue(false));
        }
        return Result<JsonValue>::failure(
            Error(ErrorCode::InvalidArgument, "Invalid boolean literal"));
    }

    Result<JsonValue> parseNull() {
        if (input_.substr(pos_).starts_with("null")) {
            pos_ += 4;
            col_ += 4;
            return Result<JsonValue>::success(JsonValue(nullptr));
        }
        return Result<JsonValue>::failure(
            Error(ErrorCode::InvalidArgument, "Invalid null literal"));
    }
};

} // namespace

std::string JsonValue::serialize(int indentSpaces) const {
    std::string out;
    out.reserve(256);
    serializeValue(out, *this, indentSpaces, 0);
    return out;
}

Result<JsonValue> JsonValue::parse(std::string_view text) {
    JsonParser parser(text);
    return parser.parse();
}

} // namespace eng::core
