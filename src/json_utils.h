#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <concepts>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace json_utils {

/// Escapes the first-null-terminated prefix of bounded text for inclusion in a JSON string.
std::string escape_json_string(std::string_view value);

// Fast-path JSON string escaper for serialize v2. Scans value once; if it contains no character
// that needs escaping ('"', '\\', or a control char < 0x20) the bytes are appended to out verbatim,
// otherwise it falls back to the same escaping escape_json_string produces. Appends (no return
// copy).
/// Appends the JSON-escaped first-null-terminated prefix of bounded text to an owning buffer.
void append_escaped_json_string(std::string &out, std::string_view value);

class JsonReader {
public:
    using ObjectPropertyParser = std::function<bool(std::string_view, JsonReader*, std::string*)>;
    using ArrayValueParser = std::function<bool(JsonReader*, std::string*)>;

    /// Copies a bounded JSON document and parses only its prefix before the first null byte.
    explicit JsonReader(std::string_view input);

    bool parse_root_object(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool parse_object(const ObjectPropertyParser& property_parser, std::string* error_message);

    /// Parses a root object with a compatibility callback that requires an owning property key.
    template <typename LegacyPropertyParser>
        requires(!std::convertible_to<LegacyPropertyParser, ObjectPropertyParser>)
    bool parse_root_object(LegacyPropertyParser property_parser, std::string* error_message)
    {
        return parse_root_object(adapt_legacy_property_parser(std::move(property_parser)), error_message);
    }

    /// Parses an object with a compatibility callback that requires an owning property key.
    template <typename LegacyPropertyParser>
        requires(!std::convertible_to<LegacyPropertyParser, ObjectPropertyParser>)
    bool parse_object(LegacyPropertyParser property_parser, std::string* error_message)
    {
        return parse_object(adapt_legacy_property_parser(std::move(property_parser)), error_message);
    }
    bool parse_array(const ArrayValueParser& value_parser, std::string* error_message);
    bool parse_string(std::string* value, std::string* error_message);
    bool parse_bool(bool* value, std::string* error_message);
    bool parse_integer(int* value, std::string* error_message);
    bool parse_long(long* value, std::string* error_message);
    bool parse_string_array(std::vector<std::string>* values, std::string* error_message);
    bool skip_value(std::string* error_message);
#ifdef TESTING
    /// Exercises bounded literal matching directly for characterization tests.
    bool match_literal_for_testing(std::string_view literal);
#endif

private:
    template <typename LegacyPropertyParser>
    static ObjectPropertyParser adapt_legacy_property_parser(LegacyPropertyParser property_parser)
    {
        return [property_parser = std::move(property_parser)](
                   std::string_view key, JsonReader* reader, std::string* nested_error_message) mutable {
            // Legacy account parsers still require ownership; contain that allocation at this
            // compatibility boundary while native JSON callbacks borrow the normalized key.
            const std::string owned_key(key);
            return property_parser(owned_key, reader, nested_error_message);
        };
    }

    bool parse_object_body(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool consume(char expected);
    bool match_literal(std::string_view literal);
    void skip_whitespace();
    bool is_at_end() const;

    // Owns the bounded JSON prefix being parsed; never includes bytes after the first null.
    std::string m_input;
    // Identifies the next unconsumed byte in m_input and advances as parsing consumes text.
    size_t m_position = 0;
};

// Optimized drop-in equivalent of JsonReader used by the v2 character deserializers: identical
// public surface and observable behavior, but lower-allocation internals (from_chars integer parse,
// move-out / no-escape-fast-path strings, branchless whitespace/digit tests, strlen-free literal
// match). Kept as a separate non-virtual class so JsonReader stays an untouched measurement baseline.
class JsonReaderV2 {
public:
    using ObjectPropertyParser = std::function<bool(std::string_view, JsonReaderV2*, std::string*)>;
    using ArrayValueParser = std::function<bool(JsonReaderV2*, std::string*)>;

    /// Copies a bounded JSON document and parses only its prefix before the first null byte.
    explicit JsonReaderV2(std::string_view input);

    bool parse_root_object(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool parse_object(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool parse_array(const ArrayValueParser& value_parser, std::string* error_message);
    bool parse_string(std::string* value, std::string* error_message);
    bool parse_bool(bool* value, std::string* error_message);
    bool parse_integer(int* value, std::string* error_message);
    bool parse_long(long* value, std::string* error_message);
    bool parse_string_array(std::vector<std::string>* values, std::string* error_message);
    bool skip_value(std::string* error_message);
#ifdef TESTING
    /// Exercises bounded literal matching directly for optimized-reader characterization tests.
    bool match_literal_for_testing(std::string_view literal);
#endif

private:
    bool parse_object_body(const ObjectPropertyParser& property_parser, std::string* error_message);
    bool consume(char expected);
    bool match_literal(std::string_view literal);
    void skip_whitespace();
    bool is_at_end() const;

    // Owns the bounded JSON prefix being parsed; never includes bytes after the first null.
    std::string m_input;
    // Cursor into m_input of the next unconsumed character; advanced by every consume/parse step.
    size_t m_position = 0;
};

} // namespace json_utils

#endif
