#include "../json_utils.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

template <typename Reader>
void expect_property_callback_stops_at_embedded_null()
{
    const std::string json = "{\"value\\u0000ignored\":42}";
    std::string observed_key;
    std::string error_message;
    Reader reader(json);
    ASSERT_TRUE(reader.parse_root_object(
        [&](std::string_view key, Reader* nested_reader, std::string* nested_error_message) {
            observed_key.assign(key);
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message))
        << error_message;
    EXPECT_EQ(observed_key, "value");
}

TEST(JsonUtils, PropertyCallbacksBorrowFirstNullTerminatedKeys)
{
    expect_property_callback_stops_at_embedded_null<json_utils::JsonReader>();
    expect_property_callback_stops_at_embedded_null<json_utils::JsonReaderV2>();
}

template <typename Reader>
void expect_literal_matcher_stops_at_embedded_null()
{
    Reader reader("true42");
    constexpr std::string_view embedded_null_literal("true\0ignored", 12);
    ASSERT_TRUE(reader.match_literal_for_testing(embedded_null_literal));

    int trailing_value = 0;
    std::string error_message;
    ASSERT_TRUE(reader.parse_integer(&trailing_value, &error_message)) << error_message;
    EXPECT_EQ(trailing_value, 42);
}

TEST(JsonUtils, LiteralMatchersStopAtEmbeddedNullAndAdvanceByPrefix)
{
    expect_literal_matcher_stops_at_embedded_null<json_utils::JsonReader>();
    expect_literal_matcher_stops_at_embedded_null<json_utils::JsonReaderV2>();
}

TEST(JsonUtils, EscapesQuotesBackslashesAndControlCharacters)
{
    const std::string raw = "\"slash\\\\\n\t\r\b\f";
    EXPECT_EQ(json_utils::escape_json_string(raw), "\\\"slash\\\\\\\\\\n\\t\\r\\b\\f");
}

TEST(JsonUtils, EscapesOtherControlCharactersAsUnicodeEscapes)
{
    const std::string raw(1, '\v');
    EXPECT_EQ(json_utils::escape_json_string(raw), "\\u000b");
}

TEST(JsonUtils, EscapesBoundedTextWithoutRequiringNullTermination) {
    const char bounded_storage[] = {'a', '"', 'b', 'x'};
    const std::string_view bounded_value(bounded_storage, 3);

    EXPECT_EQ(json_utils::escape_json_string(bounded_value), "a\\\"b");

    std::string output = "prefix:";
    json_utils::append_escaped_json_string(output, bounded_value);
    EXPECT_EQ(output, "prefix:a\\\"b");
}

TEST(JsonUtils, EscapesEmbeddedNullsAsUnicodeEscapes) {
    constexpr std::string_view embedded_null_value("alpha\0beta", 10);

    EXPECT_EQ(json_utils::escape_json_string(embedded_null_value), "alpha\\u0000beta");

    std::string output;
    json_utils::append_escaped_json_string(output, embedded_null_value);
    EXPECT_EQ(output, "alpha\\u0000beta");
}

template <typename Reader>
void expect_embedded_null_string_round_trip()
{
    const std::string original("alpha\0beta", 10);
    const std::string document = "\"" + json_utils::escape_json_string(original) + "\"";

    Reader reader(document);
    std::string decoded;
    std::string error_message;
    ASSERT_TRUE(reader.parse_string(&decoded, &error_message)) << error_message;
    EXPECT_EQ(decoded, original);
}

TEST(JsonUtils, EmbeddedNullStringValuesRoundTripThroughSerializeAndParse) {
    expect_embedded_null_string_round_trip<json_utils::JsonReader>();
    expect_embedded_null_string_round_trip<json_utils::JsonReaderV2>();
}

TEST(JsonUtils, ReaderAcceptsBoundedTextAndStopsAtEmbeddedNull) {
    const char bounded_storage[] = {'{', '"', 'v', 'a', 'l', 'u', 'e',
                                    '"', ':', '4', '2', '}', 'x'};
    const std::string_view bounded_json(bounded_storage, 12);
    constexpr std::string_view embedded_null_json("{\"value\":42}\0ignored", 20);

    const auto parse_value = [](std::string_view json) {
        int value = 0;
        std::string error_message;
        json_utils::JsonReader reader(json);
        EXPECT_TRUE(reader.parse_root_object(
            [&](std::string_view key, json_utils::JsonReader *nested_reader,
                std::string *nested_error_message) {
                if (key == "value") {
                    return nested_reader->parse_integer(&value, nested_error_message);
                }
                return nested_reader->skip_value(nested_error_message);
            },
            &error_message))
            << error_message;
        return value;
    };

    EXPECT_EQ(parse_value(bounded_json), 42);
    EXPECT_EQ(parse_value(embedded_null_json), 42);
}

template <typename Reader>
void expect_reader_owns_short_lived_input()
{
    const auto create_reader = []() {
        std::string short_lived_json(256, ' ');
        short_lived_json += "{\"value\":42}";
        short_lived_json.push_back('\0');
        short_lived_json += "ignored";
        return Reader(short_lived_json);
    };

    Reader reader = create_reader();
    int value = 0;
    std::string error_message;
    ASSERT_TRUE(reader.parse_root_object(
        [&](std::string_view key, Reader* nested_reader, std::string* nested_error_message) {
            if (key == "value") {
                return nested_reader->parse_integer(&value, nested_error_message);
            }
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message))
        << error_message;
    EXPECT_EQ(value, 42);
}

TEST(JsonUtils, ReadersOwnShortLivedFirstNullTerminatedInput)
{
    expect_reader_owns_short_lived_input<json_utils::JsonReader>();
    expect_reader_owns_short_lived_input<json_utils::JsonReaderV2>();
}

TEST(JsonUtils, ParsesTypedObjectProperties)
{
    const std::string json = R"({
        "name": "Aragorn",
        "level": 42,
        "trusted": true,
        "aliases": ["strider", "king"]
    })";

    json_utils::JsonReader reader(json);
    std::string name;
    int level = 0;
    bool trusted = false;
    std::vector<std::string> aliases;
    std::string error_message;

    ASSERT_TRUE(reader.parse_root_object([&](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
        if (key == "name")
            return nested_reader->parse_string(&name, nested_error_message);
        if (key == "level")
            return nested_reader->parse_integer(&level, nested_error_message);
        if (key == "trusted")
            return nested_reader->parse_bool(&trusted, nested_error_message);
        if (key == "aliases")
            return nested_reader->parse_string_array(&aliases, nested_error_message);
        return nested_reader->skip_value(nested_error_message);
    },
        &error_message))
        << error_message;

    EXPECT_EQ(name, "Aragorn");
    EXPECT_EQ(level, 42);
    EXPECT_TRUE(trusted);
    EXPECT_EQ(aliases, (std::vector<std::string> { "strider", "king" }));
}

TEST(JsonUtils, SkipsUnknownNestedValuesWithoutBreakingKnownFields)
{
    const std::string json = R"({
        "ignore_object": { "nested": ["value", {"deeper": false}] },
        "ignore_array": [1, true, {"name": "ignored"}],
        "name": "Legolas"
    })";

    json_utils::JsonReader reader(json);
    std::string name;
    std::string error_message;

    ASSERT_TRUE(reader.parse_root_object([&](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
        if (key == "name")
            return nested_reader->parse_string(&name, nested_error_message);
        return nested_reader->skip_value(nested_error_message);
    },
        &error_message))
        << error_message;

    EXPECT_EQ(name, "Legolas");
}

TEST(JsonUtils, RejectsTrailingCharactersAfterRootObject)
{
    const std::string json = "{\"name\":\"Aragorn\"} trailing";
    json_utils::JsonReader reader(json);
    std::string error_message;

    EXPECT_FALSE(reader.parse_root_object(
        [](std::string_view, json_utils::JsonReader* nested_reader,
            std::string* nested_error_message) {
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(JsonUtils, RejectsUnsupportedStringEscapes)
{
    const std::string json = "{\"name\":\"bad\\u263A\"}";
    json_utils::JsonReader reader(json);
    std::string name;
    std::string error_message;

    EXPECT_FALSE(reader.parse_root_object(
        [&](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "name")
                return nested_reader->parse_string(&name, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(JsonUtils, RejectsRawControlCharactersInsideStrings)
{
    const std::string json = std::string("{\"name\":\"bad\nnewline\"}");
    json_utils::JsonReader reader(json);
    std::string name;
    std::string error_message;

    EXPECT_FALSE(reader.parse_root_object(
        [&](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "name")
                return nested_reader->parse_string(&name, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(JsonUtils, ParsesAsciiUnicodeEscapesProducedBySerializer)
{
    const std::string json = "{\"name\":\"line\\u000bbreak\"}";
    json_utils::JsonReader reader(json);
    std::string name;
    std::string error_message;

    ASSERT_TRUE(reader.parse_root_object(
        [&](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "name")
                return nested_reader->parse_string(&name, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message))
        << error_message;

    ASSERT_EQ(name.size(), 10u);
    EXPECT_EQ(name[4], '\v');
}

TEST(JsonUtils, RejectsIntegersOutsideIntRange)
{
    const std::string json = "{\"level\":2147483648}";
    json_utils::JsonReader reader(json);
    int level = 0;
    std::string error_message;

    EXPECT_FALSE(reader.parse_root_object(
        [&](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "level")
                return nested_reader->parse_integer(&level, nested_error_message);
            return nested_reader->skip_value(nested_error_message);
        },
        &error_message));
    EXPECT_NE(error_message.find("out of range"), std::string::npos);
}

} // namespace
