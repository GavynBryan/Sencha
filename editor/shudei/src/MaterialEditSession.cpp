#include "MaterialEditSession.h"

#include <assets/material/MaterialLoader.h>
#include <assets/material/MaterialWriter.h>

namespace
{
    bool SameVec4(const Vec4& a, const Vec4& b)
    {
        return a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
    }
}

bool SameMaterialDescription(const MaterialDescription& a, const MaterialDescription& b)
{
    return SameVec4(a.BaseColorFactor, b.BaseColorFactor)
        && a.BaseColorTexture.Path == b.BaseColorTexture.Path
        && a.NormalTexture.Path == b.NormalTexture.Path
        && a.NormalScale == b.NormalScale
        && a.OrmTexture.Path == b.OrmTexture.Path
        && a.RoughnessFactor == b.RoughnessFactor
        && a.MetallicFactor == b.MetallicFactor
        && SameVec4(a.EmissiveFactor, b.EmissiveFactor)
        && a.EmissiveTexture.Path == b.EmissiveTexture.Path
        && a.AlphaMode == b.AlphaMode
        && a.AlphaCutoff == b.AlphaCutoff;
}

bool MaterialEditSession::Open(std::string virtualPath, std::string filePath, std::string* error)
{
    MaterialDescription loaded;
    MaterialParseError parseError;
    if (!LoadMaterialFromFile(filePath, loaded, &parseError))
    {
        if (error != nullptr)
            *error = parseError.Message;
        return false;
    }

    OpenVirtualPath = std::move(virtualPath);
    OpenFilePath = std::move(filePath);
    SavedState = loaded;
    WorkingState = loaded;
    Dirty = false;
    ++StateVersion;
    return true;
}

void MaterialEditSession::Close()
{
    OpenVirtualPath.clear();
    OpenFilePath.clear();
    SavedState = MaterialDescription{};
    WorkingState = MaterialDescription{};
    Dirty = false;
    ++StateVersion;
}

void MaterialEditSession::SetWorking(const MaterialDescription& description)
{
    if (SameMaterialDescription(WorkingState, description))
        return;
    WorkingState = description;
    Dirty = !SameMaterialDescription(WorkingState, SavedState);
    ++StateVersion;
}

bool MaterialEditSession::Save(std::string* error)
{
    if (!HasOpen())
    {
        if (error != nullptr)
            *error = "no material is open";
        return false;
    }
    if (!SaveMaterialFile(OpenFilePath, WorkingState, error))
        return false;
    SavedState = WorkingState;
    Dirty = false;
    return true;
}

bool MaterialEditSession::SaveTo(const std::string& filePath, std::string* error) const
{
    return SaveMaterialFile(filePath, WorkingState, error);
}

bool MaterialEditSession::CreateNew(const std::string& filePath, std::string* error)
{
    return SaveMaterialFile(filePath, MaterialDescription{}, error);
}

void MaterialEditSession::RenameTo(std::string virtualPath, std::string filePath)
{
    if (!HasOpen())
        return;
    OpenVirtualPath = std::move(virtualPath);
    OpenFilePath = std::move(filePath);
}
