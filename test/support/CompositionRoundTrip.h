#pragma once

#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace CompositionRoundTrip
{
    inline DataFieldSchema Field(DataFieldKind kind, std::string key, bool required = false)
    {
        DataFieldSchema field = MakeDataField(kind, std::move(key), "");
        field.Required = required;
        return field;
    }

    inline DataFieldSchema Record(std::string key, std::vector<DataFieldSchema> members)
    {
        DataFieldSchema record = Field(DataFieldKind::Record, std::move(key));
        record.Children = std::move(members);
        return record;
    }

    inline DataFieldSchema List(std::string key, DataFieldSchema element, std::string mergeKey = {},
                                DataMergePolicy policy = DataMergePolicy::Override)
    {
        DataFieldSchema list = Field(DataFieldKind::Array, std::move(key));
        list.Children.push_back(std::move(element));
        list.MergeKey = std::move(mergeKey);
        list.MergePolicy = policy;
        return list;
    }

    inline DataFieldSchema Schema()
    {
        return Record("", {
            Field(DataFieldKind::Float, "n"),
            Field(DataFieldKind::String, "s"),
            Field(DataFieldKind::Float, "req", true),
            Record("rec", { Field(DataFieldKind::Float, "a"), Field(DataFieldKind::String, "b"),
                            Record("inner", { Field(DataFieldKind::Float, "x") }) }),
            List("mods", Record("", { Field(DataFieldKind::StableKey, "key", true), Field(DataFieldKind::String, "attr"),
                                      Field(DataFieldKind::Float, "magnitude"),
                                      Record("sub", { Field(DataFieldKind::Float, "p") }) })),
            List("names", Record("", { Field(DataFieldKind::String, "name", true), Field(DataFieldKind::Float, "v") }),
                 "name"),
            List("tags", Field(DataFieldKind::String, "")),
            List("rows", Record("", { Field(DataFieldKind::Float, "x"), Field(DataFieldKind::Float, "y") })),
            List("facts", Record("", { Field(DataFieldKind::String, "name", true), Field(DataFieldKind::String, "kind") }),
                 "name", DataMergePolicy::AppendOnly),
        });
    }

    struct Case
    {
        std::string Base;
        std::string Target;
    };

    class Generator
    {
    public:
        explicit Generator(std::uint32_t seed)
            : Random(seed)
        {
        }

        [[nodiscard]] Case Next()
        {
            JsonValue base = MakeBase();
            JsonValue target = base;
            const std::size_t mutations = 1 + Below(4);
            for (std::size_t i = 0; i < mutations; ++i)
                Mutate(target);
            return { JsonStringify(base), JsonStringify(target) };
        }

    private:
        [[nodiscard]] std::size_t Below(std::size_t bound) { return static_cast<std::size_t>(Random() % bound); }
        [[nodiscard]] bool Coin() { return Below(2) == 0; }
        [[nodiscard]] double Number() { return static_cast<double>(Below(10)); }
        [[nodiscard]] std::string Word()
        {
            static const char* const kWords[] = { "red", "green", "blue", "amber", "violet" };
            return kWords[Below(std::size(kWords))];
        }

        static void Set(JsonValue& object, std::string_view key, JsonValue value)
        {
            if (JsonValue* found = object.Find(key))
                *found = std::move(value);
            else
                object.AsObject().emplace_back(std::string(key), std::move(value));
        }

        static void Erase(JsonValue& object, std::string_view key)
        {
            std::erase_if(object.AsObject(), [&](const auto& member) { return member.first == key; });
        }

        [[nodiscard]] JsonValue Element(std::string_view list, std::string key)
        {
            JsonValue element(JsonValue::Object{});
            if (list == "mods")
            {
                if (Coin())
                    Set(element, "attr", Word());
                Set(element, "key", std::move(key));
                Set(element, "magnitude", Number());
                if (Coin())
                    Set(element, "sub", JsonValue(JsonValue::Object{ { "p", Number() } }));
            }
            else if (list == "names")
            {
                Set(element, "name", std::move(key));
                if (Coin())
                    Set(element, "v", Number());
            }
            else
            {
                Set(element, "name", std::move(key));
                Set(element, "kind", Word());
            }
            return element;
        }

        [[nodiscard]] std::string FreshKey(std::string_view list, const JsonValue& elements)
        {
            static const char* const kMods[] = { "m0", "m1", "m2", "m3", "m4", "m5", "m6", "m7" };
            static const char* const kNames[] = { "Alpha", "Beta Gamma", "delta.x", "Epsilon", "zeta", "Eta Theta" };
            static const char* const kFacts[] = { "Grounded", "Speed", "Airborne", "Crouched", "Sliding" };
            const std::string_view field = list == "mods" ? "key" : "name";
            for (std::size_t attempt = 0; attempt < 32; ++attempt)
            {
                std::string candidate = list == "mods" ? kMods[Below(std::size(kMods))]
                    : list == "names"                  ? kNames[Below(std::size(kNames))]
                                                       : kFacts[Below(std::size(kFacts))];
                const bool taken = std::ranges::any_of(elements.AsArray(), [&](const JsonValue& element) {
                    const JsonValue* key = element.Find(field);
                    return key != nullptr && key->AsString() == candidate;
                });
                if (!taken)
                    return candidate;
            }
            return std::string(list) + std::to_string(elements.Size());
        }

        [[nodiscard]] JsonValue KeyedList(std::string_view list)
        {
            JsonValue elements(JsonValue::Array{});
            const std::size_t count = Below(5);
            for (std::size_t i = 0; i < count; ++i)
                elements.AsArray().push_back(Element(list, FreshKey(list, elements)));
            return elements;
        }

        [[nodiscard]] JsonValue MakeBase()
        {
            JsonValue base(JsonValue::Object{});
            Set(base, "req", Number());
            if (Coin())
                Set(base, "n", Number());
            if (Coin())
                Set(base, "s", Word());
            if (Coin())
            {
                JsonValue rec(JsonValue::Object{});
                if (Coin())
                    Set(rec, "a", Number());
                if (Coin())
                    Set(rec, "b", Word());
                if (Coin())
                    Set(rec, "inner", JsonValue(JsonValue::Object{ { "x", Number() } }));
                Set(base, "rec", std::move(rec));
            }
            for (const char* list : { "mods", "names", "facts" })
                if (Below(4) != 0)
                    Set(base, list, KeyedList(list));
            if (Coin())
                Set(base, "tags", JsonValue(JsonValue::Array{ Word(), Word() }));
            if (Coin())
                Set(base, "rows", JsonValue(JsonValue::Array{ JsonValue(JsonValue::Object{ { "x", Number() }, { "y", Number() } }) }));
            if (Coin())
                Set(base, "free", JsonValue(JsonValue::Object{ { "p", Number() },
                                                               { "q", JsonValue(JsonValue::Object{ { "r", Word() } }) } }));
            return base;
        }

        void MutateKeyedList(JsonValue& target, std::string_view list)
        {
            JsonValue* found = target.Find(list);
            if (found == nullptr || !found->IsArray())
            {
                Set(target, list, KeyedList(list));
                return;
            }
            JsonValue::Array& elements = found->AsArray();
            const std::string_view field = list == "mods" ? "key" : "name";
            const bool appendOnly = list == "facts";
            const std::size_t choice = appendOnly ? 0 : Below(elements.empty() ? 2 : 8);
            switch (choice)
            {
            case 0:
                elements.push_back(Element(list, FreshKey(list, *found)));
                return;
            case 1:
                elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(Below(elements.size() + 1)),
                                Element(list, FreshKey(list, *found)));
                return;
            case 2:
                std::ranges::shuffle(elements, Random);
                if (elements.size() > 1)
                    std::swap(elements.front(), elements.back());
                return;
            case 3:
                elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(Below(elements.size())));
                return;
            case 4:
            {
                JsonValue& element = elements[Below(elements.size())];
                if (list == "mods")
                    Set(element, Coin() ? "magnitude" : "attr", Coin() ? JsonValue(Number()) : JsonValue(Word()));
                else
                    Set(element, "v", Number());
                return;
            }
            case 5:
            {
                JsonValue& element = elements[Below(elements.size())];
                Erase(element, list == "mods" ? (Coin() ? "attr" : "sub") : "v");
                return;
            }
            case 6:
            {
                JsonValue copy = elements[Below(elements.size())];
                Set(copy, list == "mods" ? "magnitude" : "v", Number());
                elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(Below(elements.size() + 1)), std::move(copy));
                return;
            }
            default:
            {
                JsonValue element = Element(list, list == "mods" ? "not a key" : "");
                Set(element, field, list == "mods" ? JsonValue("not a key") : JsonValue("odd name!"));
                elements.push_back(std::move(element));
                return;
            }
            }
        }

        void Mutate(JsonValue& target)
        {
            switch (Below(14))
            {
            case 0:
                Set(target, "n", Coin() ? JsonValue(Number()) : JsonValue(Word()));
                return;
            case 1:
                Set(target, "req", Number());
                return;
            case 2:
                Erase(target, Coin() ? "n" : "s");
                return;
            case 3:
            {
                JsonValue* rec = target.Find("rec");
                if (rec == nullptr || !rec->IsObject())
                {
                    Set(target, "rec", JsonValue(JsonValue::Object{ { "b", Word() } }));
                    return;
                }
                if (Coin())
                    Set(*rec, "inner", JsonValue(JsonValue::Object{ { "x", Number() } }));
                else
                    Erase(*rec, Coin() ? "a" : "inner");
                return;
            }
            case 4:
                if (Coin())
                    Erase(target, "rec");
                else
                    Set(target, "rec", Number());
                return;
            case 5:
                MutateKeyedList(target, "mods");
                return;
            case 6:
                MutateKeyedList(target, "names");
                return;
            case 7:
                MutateKeyedList(target, "facts");
                return;
            case 8:
                Set(target, "tags", JsonValue(JsonValue::Array{ Word() }));
                return;
            case 9:
                Set(target, "rows", JsonValue(JsonValue::Array{ JsonValue(JsonValue::Object{ { "y", Number() }, { "x", Number() } }),
                                                                JsonValue(JsonValue::Object{ { "x", Number() } }) }));
                return;
            case 10:
                Set(target, "free", JsonValue(JsonValue::Object{ { "q", JsonValue(JsonValue::Object{ { "r", Word() }, { "t", Number() } }) } }));
                return;
            case 11:
                Erase(target, Coin() ? "mods" : Coin() ? "names" : "tags");
                return;
            case 12:
                Set(target, "extra", JsonValue(JsonValue::Object{ { "deep", JsonValue(JsonValue::Array{ Number() }) } }));
                return;
            default:
                Set(target, "n", JsonValue(JsonValue::Object{ { "was", Number() } }));
                return;
            }
        }

        std::mt19937 Random;
    };
}
