#include "BrushModifierSerialization.h"

#include "BrushMeshSerialization.h"

#include <utility>

namespace
{
    JsonValue Vec3ToJson(Vec3d v)
    {
        return JsonValue(JsonValue::Array{
            JsonValue(static_cast<double>(v.X)),
            JsonValue(static_cast<double>(v.Y)),
            JsonValue(static_cast<double>(v.Z)) });
    }

    bool Vec3FromJson(const JsonValue* v, Vec3d& out)
    {
        if (v == nullptr || !v->IsArray() || v->Size() < 3)
            return false;
        const JsonValue::Array& a = v->AsArray();
        if (!a[0].IsNumber() || !a[1].IsNumber() || !a[2].IsNumber())
            return false;
        out = Vec3d{ static_cast<float>(a[0].AsNumber()),
                     static_cast<float>(a[1].AsNumber()),
                     static_cast<float>(a[2].AsNumber()) };
        return true;
    }

    const char* AxisKey(LocalAxis axis)
    {
        return axis == LocalAxis::Y ? "y" : axis == LocalAxis::Z ? "z" : "x";
    }

    bool AxisFromJson(const JsonValue* v, LocalAxis& out)
    {
        if (v == nullptr || !v->IsString())
            return false;
        const std::string& text = v->AsString();
        if (text == "x") out = LocalAxis::X;
        else if (text == "y") out = LocalAxis::Y;
        else if (text == "z") out = LocalAxis::Z;
        else return false;
        return true;
    }

    float NumberOr(const JsonValue* v, float fallback)
    {
        return v != nullptr && v->IsNumber() ? static_cast<float>(v->AsNumber()) : fallback;
    }

    bool BoolOr(const JsonValue* v, bool fallback)
    {
        return v != nullptr && v->IsBool() ? v->AsBool() : fallback;
    }

    struct ToJson
    {
        JsonValue::Object& Obj;

        void operator()(const MirrorModifier& mirror) const
        {
            Obj.emplace_back("axis", JsonValue(AxisKey(mirror.Axis)));
            const char* source = mirror.Source == MirrorPlaneSource::BoundsCenter ? "bounds"
                : mirror.Source == MirrorPlaneSource::Custom ? "custom" : "origin";
            Obj.emplace_back("source", JsonValue(source));
            Obj.emplace_back("offset", JsonValue(static_cast<double>(mirror.Offset)));
            if (mirror.Source == MirrorPlaneSource::Custom)
            {
                JsonValue::Object plane;
                plane.emplace_back("normal", Vec3ToJson(mirror.CustomPlane.Normal));
                plane.emplace_back("d", JsonValue(static_cast<double>(mirror.CustomPlane.D)));
                Obj.emplace_back("plane", JsonValue(std::move(plane)));
            }
        }

        void operator()(const ArrayModifier& array) const
        {
            Obj.emplace_back("placement",
                             JsonValue(array.Placement == ArrayPlacement::ConstantOffset ? "offset" : "bounds"));
            Obj.emplace_back("axis", JsonValue(AxisKey(array.Axis)));
            Obj.emplace_back("reverse", JsonValue(array.Reverse));
            Obj.emplace_back("count", JsonValue(array.Count));
            Obj.emplace_back("spacing", JsonValue(static_cast<double>(array.Spacing)));
            if (array.Placement == ArrayPlacement::ConstantOffset)
                Obj.emplace_back("offset", Vec3ToJson(array.Offset));
        }
    };

    // Fills a default-constructed kind's params from its entry. False when the
    // entry cannot mean what it says. Entries written before the relationship
    // model keep their meaning: a bare plane is a Custom plane, a bare offset is
    // a constant offset.
    struct FromJson
    {
        const JsonValue& Entry;

        bool operator()(MirrorModifier& mirror) const
        {
            (void)AxisFromJson(Entry.Find("axis"), mirror.Axis);
            mirror.Offset = NumberOr(Entry.Find("offset"), 0.0f);
            const JsonValue* plane = Entry.Find("plane");
            const JsonValue* source = Entry.Find("source");
            std::string sourceText = source != nullptr && source->IsString() ? source->AsString() : "";
            if (sourceText.empty())
                sourceText = plane != nullptr ? "custom" : "origin";
            if (sourceText == "custom")
            {
                mirror.Source = MirrorPlaneSource::Custom;
                if (plane == nullptr || !plane->IsObject()
                    || !Vec3FromJson(plane->Find("normal"), mirror.CustomPlane.Normal))
                    return false;
                mirror.CustomPlane.D = NumberOr(plane->Find("d"), 0.0f);
            }
            else if (sourceText == "bounds")
                mirror.Source = MirrorPlaneSource::BoundsCenter;
            else if (sourceText == "origin")
                mirror.Source = MirrorPlaneSource::Origin;
            else
                return false;
            return true;
        }

        bool operator()(ArrayModifier& array) const
        {
            const JsonValue* count = Entry.Find("count");
            if (count == nullptr || !count->IsNumber())
                return false;
            array.Count = static_cast<int>(count->AsNumber());
            (void)AxisFromJson(Entry.Find("axis"), array.Axis);
            array.Reverse = BoolOr(Entry.Find("reverse"), false);
            array.Spacing = NumberOr(Entry.Find("spacing"), 0.0f);
            const JsonValue* placement = Entry.Find("placement");
            const JsonValue* offset = Entry.Find("offset");
            std::string placementText =
                placement != nullptr && placement->IsString() ? placement->AsString() : "";
            if (placementText.empty())
                placementText = offset != nullptr ? "offset" : "bounds";
            if (placementText == "offset")
            {
                array.Placement = ArrayPlacement::ConstantOffset;
                if (!Vec3FromJson(offset, array.Offset))
                    return false;
            }
            else if (placementText == "bounds")
                array.Placement = ArrayPlacement::RelativeToBounds;
            else
                return false;
            return true;
        }
    };

    void Report(std::string* error, std::size_t index, const char* what)
    {
        if (error == nullptr)
            return;
        error->append("modifier ").append(std::to_string(index)).append(": ").append(what).append("\n");
    }
}

JsonValue BrushModifierStackToJson(const BrushModifierStack& stack)
{
    JsonValue::Array entries;
    entries.reserve(stack.size());
    for (const BrushModifier& modifier : stack)
    {
        JsonValue::Object obj;
        obj.emplace_back("kind", JsonValue(BrushModifierKindOf(modifier).JsonKey));
        std::visit(ToJson{ obj }, modifier.Params);
        obj.emplace_back("enabled", JsonValue(modifier.Enabled));
        entries.push_back(JsonValue(std::move(obj)));
    }
    return JsonValue(std::move(entries));
}

BrushModifierStack BrushModifierStackFromJson(const JsonValue& value, std::string* error)
{
    BrushModifierStack stack;
    if (!value.IsArray())
        return stack;
    const JsonValue::Array& entries = value.AsArray();
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const JsonValue& entry = entries[i];
        const JsonValue* kind = entry.Find("kind");
        if (kind == nullptr || !kind->IsString())
        {
            Report(error, i, "missing kind");
            continue;
        }

        const BrushModifierKindInfo* kindInfo = nullptr;
        for (const BrushModifierKindInfo& info : BrushModifierKinds())
            if (kind->AsString() == info.JsonKey)
                kindInfo = &info;
        if (kindInfo == nullptr)
        {
            Report(error, i, "unknown kind");
            continue;
        }

        BrushModifier modifier = kindInfo->MakeDefault();
        if (const JsonValue* enabled = entry.Find("enabled"); enabled && enabled->IsBool())
            modifier.Enabled = enabled->AsBool();
        if (!std::visit(FromJson{ entry }, modifier.Params))
        {
            Report(error, i, "malformed parameters");
            continue;
        }
        stack.push_back(std::move(modifier));
    }
    return stack;
}

JsonValue BrushRecordToJson(const BrushRecord& record)
{
    JsonValue json = BrushMeshToJson(record.Mesh);
    if (!record.Modifiers.empty())
        json.AsObject().emplace_back("modifiers", BrushModifierStackToJson(record.Modifiers));
    return json;
}

BrushRecord BrushRecordFromJson(const JsonValue& value, std::string* error)
{
    BrushRecord record;
    record.Mesh = BrushMeshFromJson(value);
    if (const JsonValue* modifiers = value.Find("modifiers"))
        record.Modifiers = BrushModifierStackFromJson(*modifiers, error);
    return record;
}
