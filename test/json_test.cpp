#include "harness.hpp"

#include "arrowlane/json.hpp"

#include <cstdint>
#include <string>

AL_TEST(json_roundtrip_object) {
    auto parsed = arrowlane::json_parse(R"({"a":1,"b":[true,null,"x"],"c":{"d":2.5}})");
    AL_REQUIRE(parsed.has_value());
    const auto* a = parsed->find("a");
    AL_REQUIRE(a != nullptr);
    AL_CHECK_EQ(a->as_int(), std::int64_t{1});
    const auto* b = parsed->find("b");
    AL_REQUIRE(b != nullptr);
    AL_CHECK_EQ(b->items().size(), std::size_t{3});
    AL_CHECK(b->items()[1].is_null());
    AL_CHECK_EQ(b->items()[2].as_string(), std::string("x"));
    const auto* c = parsed->find("c");
    AL_REQUIRE(c != nullptr);
    AL_CHECK_EQ(c->find("d")->as_number(), 2.5);
}

AL_TEST(json_writer_preserves_order_and_escapes) {
    auto object = arrowlane::JsonValue::make_object();
    object.set("b", arrowlane::JsonValue(std::int64_t{2}));
    object.set("a", arrowlane::JsonValue(std::string("q\"\n")));
    AL_CHECK_EQ(arrowlane::json_write(object),
                std::string("{\"b\":2,\"a\":\"q\\\"\\n\"}"));
    AL_CHECK_EQ(arrowlane::json_write_sorted(object),
                std::string("{\"a\":\"q\\\"\\n\",\"b\":2}"));
}

AL_TEST(json_parser_rejects_bad_input) {
    AL_CHECK_ERR(arrowlane::json_parse(""), arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::json_parse("{\"a\":1} x"), arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::json_parse("[1,2"), arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::json_parse("\"\\uD800\""), arrowlane::Code::BadHeader);
    AL_CHECK_ERR(arrowlane::json_parse(std::string(100, '[')), arrowlane::Code::BadHeader);
}

AL_TEST(json_numbers_keep_kind) {
    auto parsed = arrowlane::json_parse("[0, -7, 3.5, 1e3]");
    AL_REQUIRE(parsed.has_value());
    AL_CHECK_EQ(parsed->items()[0].as_int(), std::int64_t{0});
    AL_CHECK_EQ(parsed->items()[3].as_number(), 1000.0);
    AL_CHECK_EQ(arrowlane::json_write(arrowlane::JsonValue(0.0)), std::string("0.0"));
    AL_CHECK_EQ(arrowlane::json_write(arrowlane::JsonValue(1.0 / 3.0)),
                std::string("0.3333333333333333"));
}
