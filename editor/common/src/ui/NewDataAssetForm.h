#pragma once

#include <array>
#include <cstddef>
#include <string>

class DataDocumentSet;

// Choose a subtype, name a path under the content root, create and open it.
class NewDataAssetForm
{
public:
    void Draw(DataDocumentSet& documents, const char* pathHint);

private:
    std::size_t Subtype = 0;
    std::array<char, 512> Path{};
    std::string Error;
};
