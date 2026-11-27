#include "scene_source/SceneSourceUpgrade.h"

#include "scene_source/Json5Parser.h"
#include "scene_source/Json5Writer.h"
#include "scene_source/SceneElementPath.h"
#include "scene_source/SceneSourceDocument.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

namespace
{
    constexpr std::string_view kVersionKey = "format_version";

    [[nodiscard]] std::string KeyText(std::string_view key)
    {
        const std::optional<SceneElementPath> path = SceneElementPath::FromString(key);
        return path.has_value() ? path->ToString() : std::string(key);
    }

    void CollectNulls(const Json5Value& value, const std::string& path, std::vector<std::string>& out)
    {
        if (!value.IsObject())
            return;
        for (const Json5Value::Member& member : value.Members)
        {
            const std::string at = path + "." + member.first;
            if (member.second.IsNull())
                out.push_back(at);
            else
                CollectNulls(member.second, at, out);
        }
    }

    [[nodiscard]] Json5Value& PatchFor(Json5Value& patch, std::string_view key)
    {
        const std::string wanted = KeyText(key);
        for (Json5Value::Member& member : patch.Members)
            if (KeyText(member.first) == wanted)
                return member.second;
        return patch.Members.emplace_back(std::string(key), Json5Value::MakeObject()).second;
    }

    class Folder
    {
    public:
        explicit Folder(SceneSourceUpgrade& upgrade)
            : Upgrade(upgrade)
        {
        }

        void Fold(Json5Value& owner, const std::string& what, bool placement)
        {
            const Json5Value* add = placement ? owner.Find("add") : nullptr;
            const Json5Value* remove = owner.Find("remove");
            if (add == nullptr && remove == nullptr)
                return;
            const Json5Value* stated = owner.Find("patch");
            Json5Value patch = stated != nullptr ? *stated : Json5Value::MakeObject();
            if (!patch.IsObject())
                return Refuse(what + ": 'patch' must be an object");

            std::vector<std::pair<std::string, std::string>> added;
            if (add != nullptr)
                FoldAdd(*add, patch, what, added);
            if (remove != nullptr)
                FoldRemove(*remove, patch, what, added);

            const auto joinsThePatch = [&](const std::string& name) {
                return name == "patch" || name == "remove" || (placement && name == "add");
            };
            std::vector<Json5Value::Member> members;
            std::vector<std::string> comments;
            std::optional<std::size_t> placed;
            for (Json5Value::Member& member : owner.Members)
            {
                if (!joinsThePatch(member.first))
                {
                    members.push_back(std::move(member));
                    continue;
                }
                comments.insert(comments.end(), member.second.LeadingComments.begin(),
                                member.second.LeadingComments.end());
                if (!placed.has_value())
                {
                    placed = members.size();
                    members.emplace_back("patch", Json5Value{});
                }
            }
            patch.LeadingComments = std::move(comments);
            members[*placed].second = std::move(patch);
            owner.Members = std::move(members);
        }

        void Refuse(std::string why) { Upgrade.Refused.push_back(std::move(why)); }

    private:
        void FoldAdd(const Json5Value& add, Json5Value& patch, const std::string& what,
                     std::vector<std::pair<std::string, std::string>>& added)
        {
            if (!add.IsObject())
                return Refuse(what + ": 'add' must be an object");
            for (const Json5Value::Member& entry : add.Members)
            {
                if (!entry.second.IsObject())
                {
                    Refuse(what + ": 'add' for '" + entry.first + "' must be an object of components");
                    continue;
                }
                Json5Value& target = PatchFor(patch, entry.first);
                for (const Json5Value::Member& component : entry.second.Members)
                {
                    if (target.Find(component.first) != nullptr)
                    {
                        Refuse(what + ": component '" + component.first + "' on '" + entry.first
                               + "' is both patched and added");
                        continue;
                    }
                    std::vector<std::string> nulls;
                    CollectNulls(component.second, entry.first + "." + component.first, nulls);
                    for (const std::string& path : nulls)
                        Refuse(what + ": " + path + " is null in 'add', which format 3 reads as removing it");
                    target.Members.push_back(component);
                    added.emplace_back(KeyText(entry.first), component.first);
                    Upgrade.Changed.push_back(what + ": add " + entry.first + "." + component.first
                                              + " folded into patch");
                }
            }
        }

        void FoldRemove(const Json5Value& remove, Json5Value& patch, const std::string& what,
                        const std::vector<std::pair<std::string, std::string>>& added)
        {
            if (!remove.IsObject())
                return Refuse(what + ": 'remove' must be an object");
            for (const Json5Value::Member& entry : remove.Members)
            {
                if (!entry.second.IsArray())
                {
                    Refuse(what + ": 'remove' for '" + entry.first + "' must be a list of component names");
                    continue;
                }
                Json5Value& target = PatchFor(patch, entry.first);
                for (const Json5Value& name : entry.second.Elements)
                {
                    if (!name.IsString())
                    {
                        Refuse(what + ": 'remove' for '" + entry.first + "' must be a list of component names");
                        continue;
                    }
                    if (const Json5Value* stated = target.Find(name.Text); stated != nullptr)
                    {
                        if (stated->IsNull())
                            continue;
                        const bool wasAdded = std::ranges::find(added, std::pair{ KeyText(entry.first), name.Text })
                                              != added.end();
                        Refuse(what + ": component '" + name.Text + "' on '" + entry.first + "' is both "
                               + (wasAdded ? "added" : "patched") + " and removed");
                        continue;
                    }
                    target.Members.emplace_back(name.Text, Json5Value{});
                    Upgrade.Changed.push_back(what + ": remove " + entry.first + "." + name.Text
                                              + " folded into patch as null");
                }
            }
        }

        SceneSourceUpgrade& Upgrade;
    };

    [[nodiscard]] std::string InstanceText(const Json5Value& instance)
    {
        const Json5Value* id = instance.Find("id");
        return "instance " + (id != nullptr && id->IsString() ? id->Text : std::string("?"));
    }

    void RefuseVersionOneNulls(const Json5Value& instance, Folder& folder)
    {
        const Json5Value* patch = instance.Find("patch");
        if (patch == nullptr || !patch->IsObject())
            return;
        for (const Json5Value::Member& entry : patch->Members)
        {
            if (!entry.second.IsObject())
                continue;
            for (const Json5Value::Member& component : entry.second.Members)
            {
                std::vector<std::string> nulls;
                CollectNulls(component.second, entry.first + "." + component.first, nulls);
                for (const std::string& path : nulls)
                    folder.Refuse(InstanceText(instance) + ": " + path
                                  + " is null, which format_version 1 wrote as a value and format 3 reads as removing it");
            }
        }
    }

    [[nodiscard]] std::optional<std::string> Restamped(std::string_view text, const Json5Value& upgraded)
    {
        const std::string current = std::to_string(SceneSourceDocument::Version);
        for (std::size_t at = text.find(kVersionKey); at != std::string_view::npos;
             at = text.find(kVersionKey, at + kVersionKey.size()))
        {
            std::size_t cursor = at + kVersionKey.size();
            const auto skipSpace = [&] {
                while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0)
                    ++cursor;
            };
            if (cursor < text.size() && (text[cursor] == '"' || text[cursor] == '\''))
                ++cursor;
            skipSpace();
            if (cursor >= text.size() || text[cursor] != ':')
                continue;
            ++cursor;
            skipSpace();
            const std::size_t number = cursor;
            while (cursor < text.size() && (std::isdigit(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == '.'))
                ++cursor;
            if (cursor == number)
                continue;
            std::string candidate = std::string(text.substr(0, number)) + current + std::string(text.substr(cursor));
            const std::optional<Json5Value> reread = Json5Parse(candidate);
            if (reread.has_value() && reread->SameValueAs(upgraded))
                return candidate;
        }
        return std::nullopt;
    }
} // namespace

std::optional<SceneSourceUpgrade> UpgradeSceneSourceText(std::string_view text, std::string* error)
{
    const auto fail = [&](std::string message) -> std::optional<SceneSourceUpgrade> {
        if (error != nullptr)
            *error = std::move(message);
        return std::nullopt;
    };

    Json5ParseError parseError;
    std::vector<std::string> endComments;
    std::optional<Json5Value> root = Json5Parse(text, &parseError, &endComments);
    if (!root.has_value())
        return fail("line " + std::to_string(parseError.Line) + ", column " + std::to_string(parseError.Column) + ": "
                    + parseError.Message);
    if (!root->IsObject())
        return fail("scene source must be an object");
    const Json5Value* version = root->Find(kVersionKey);
    if (version == nullptr || !version->IsNumber())
        return fail("missing 'format_version'");
    const double from = version->Number;
    SceneSourceUpgrade upgrade;
    upgrade.Text = std::string(text);
    if (from == static_cast<double>(SceneSourceDocument::Version))
        return upgrade;
    if (from != 1.0 && from != 2.0)
        return fail("unsupported format_version " + std::to_string(from));

    Folder folder(upgrade);
    if (from == 1.0)
        for (const std::string_view member : { "extends", "includes", "abstract", "patch", "remove" })
            if (root->Find(member) != nullptr)
                folder.Refuse("'" + std::string(member) + "' was not part of format_version 1");
    folder.Fold(*root, "patch", false);
    if (Json5Value* instances = root->FindMutable("instances"); instances != nullptr && instances->IsArray())
        for (Json5Value& instance : instances->Elements)
        {
            if (!instance.IsObject())
                continue;
            if (from == 1.0)
                RefuseVersionOneNulls(instance, folder);
            folder.Fold(instance, InstanceText(instance), true);
        }

    const bool folded = !upgrade.Changed.empty();
    upgrade.Changed.insert(upgrade.Changed.begin(), "format_version " + std::to_string(static_cast<int>(from)) + " -> "
                                                       + std::to_string(SceneSourceDocument::Version));
    if (!upgrade.Refused.empty())
        return upgrade;

    root->FindMutable(kVersionKey)->Number = static_cast<double>(SceneSourceDocument::Version);
    std::optional<std::string> restamped = folded ? std::nullopt : Restamped(text, *root);
    std::string written = restamped.has_value() ? std::move(*restamped) : Json5Write(*root, endComments);
    std::string readError;
    if (!ParseSceneSource(written, &readError).has_value())
    {
        upgrade.Refused.push_back("the upgraded file does not read: " + readError);
        return upgrade;
    }
    upgrade.Text = std::move(written);
    return upgrade;
}
