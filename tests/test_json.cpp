#include "check.h"

#include <string>

#include "core/json.h"

using blueward::Json;
using blueward::ParseJson;

void RunJsonTests() {
    {
        const std::string text = "{\n  \"b\": 1,\n  \"a\": {\"x\": [true, null, \"s\"]},\n  \"unknown\": 123\n}";
        Json value;
        std::string error;
        CHECK(ParseJson(text, value, error));
        CHECK(error.empty());
        CHECK(value.isObject());
        CHECK(value.members().size() == 3);
        if (value.members().size() == 3) {
            CHECK(value.members()[0].first == "b");
            CHECK(value.members()[2].first == "unknown");
            const Json* a = value.Find("a");
            CHECK(a && a->Find("x") && a->Find("x")->items().size() == 3);
            if (a && a->Find("x") && a->Find("x")->items().size() == 3) CHECK(a->Find("x")->items()[2].asString() == "s");
        }
        const std::string dumped = blueward::DumpJson(value);
        Json again;
        CHECK(ParseJson(dumped, again, error));
        CHECK(again.members().size() == 3);
        if (again.members().size() == 3) {
            CHECK(again.members()[0].first == "b");
            CHECK(again.members()[2].first == "unknown");
        }
    }
    {
        Json value;
        std::string error;
        CHECK(ParseJson("{\"size\": 5368709120}", value, error));
        const Json* size = value.Find("size");
        CHECK(size && size->isInteger());
        CHECK(size->asInt() == 5368709120LL);
        CHECK(blueward::DumpJson(value).find("5368709120") != std::string::npos);
    }
    {
        std::string deep;
        for (int i = 0; i < 65; ++i) deep += "[";
        for (int i = 0; i < 65; ++i) deep += "]";
        Json value;
        std::string error;
        CHECK(!ParseJson(deep, value, error));
        CHECK(!error.empty());
        CHECK(error.find("nested") != std::string::npos || error.find("deep") != std::string::npos);
    }
    {
        Json value;
        std::string error;
        CHECK(ParseJson("\xEF\xBB\xBF{\"a\":\"b\"}", value, error));
        CHECK(value.Find("a") && value.Find("a")->asString() == "b");
    }
    {
        Json value;
        std::string error;
        CHECK(!ParseJson("{\"a\": 01}", value, error));
        CHECK(!ParseJson("[1,2,]", value, error));
        CHECK(!ParseJson("\"unterminated", value, error));
    }
    {
        Json object = Json::Object();
        object.Set("keep", Json::String("yes"));
        object.Set("keep", Json::String("replaced"));
        CHECK(object.members().size() == 1);
        CHECK(object.Find("keep")->asString() == "replaced");
        object.Erase("keep");
        CHECK(object.members().empty());
    }
}
