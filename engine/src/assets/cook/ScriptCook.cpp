#include <assets/cook/ScriptCook.h>

#include <core/hash/ContentHash.h>
#include <script/ScriptCompiler.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

namespace
{
    [[nodiscard]] bool ReadTextFile(const std::filesystem::path& path, std::string& out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return false;
        }
        std::ostringstream text;
        text << file.rdbuf();
        out = text.str();
        return true;
    }

    // Cheap top-level scan for `import "..."` lines. T restricts imports to
    // top-level declarations and has no other string statements, so a
    // line-leading `import` is unambiguous. Used by the freshness check,
    // where a full compile would be wasted work on every boot.
    void ScanImports(std::string_view source, std::vector<std::string>& out)
    {
        size_t lineStart = 0;
        while (lineStart < source.size())
        {
            size_t lineEnd = source.find('\n', lineStart);
            if (lineEnd == std::string_view::npos)
            {
                lineEnd = source.size();
            }
            std::string_view line = source.substr(lineStart, lineEnd - lineStart);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            {
                line.remove_prefix(1);
            }
            if (line.starts_with("import"))
            {
                const size_t open = line.find('"');
                const size_t close = open == std::string_view::npos
                                         ? std::string_view::npos
                                         : line.find('"', open + 1);
                if (close != std::string_view::npos)
                {
                    out.emplace_back(line.substr(open + 1, close - open - 1));
                }
            }
            lineStart = lineEnd + 1;
        }
    }

    // Import paths resolve relative to the importing file (spec D.8).
    [[nodiscard]] std::string ResolveRelative(std::string_view importer, std::string_view import)
    {
        const size_t slash = importer.find_last_of('/');
        return slash == std::string_view::npos
                   ? std::string(import)
                   : std::string(importer.substr(0, slash + 1)) + std::string(import);
    }
}

ScriptImporter::ScriptImporter(std::string assetsRoot)
    : AssetsRoot(std::move(assetsRoot))
{
}

std::vector<std::string_view> ScriptImporter::SourceExtensions() const
{
    return { ".t" };
}

ImportResult ScriptImporter::Import(const ImportInput& input, ICookOutputWriter& output)
{
    const std::string source(reinterpret_cast<const char*>(input.Bytes.data()),
                             input.Bytes.size());

    const std::filesystem::path root(AssetsRoot);
    const ScriptSourceResolver resolver = [&root](std::string_view path, std::string& out) {
        return ReadTextFile(root / std::filesystem::path(std::string(path)), out);
    };

    const ScriptCompileResult compiled =
        CompileScript(input.SourceRelPath, source, resolver);
    if (!compiled.Ok)
    {
        return ImportResult{ .Error = std::format("t compile: {}:{}:{}: {}",
                                                  compiled.Error.File, compiled.Error.Line,
                                                  compiled.Error.Col, compiled.Error.Message) };
    }

    const std::vector<std::byte> bytes = WriteScriptModule(compiled.Module);
    const std::string fileRelPath = ".cooked/" + std::string(input.SourceRelPath) + ".tbc";
    if (!output.WriteBytes(fileRelPath, bytes))
    {
        return ImportResult{ .Error = std::format("t compile: could not write '{}'",
                                                  fileRelPath) };
    }

    ImportResult result;
    result.Artifacts.push_back({ "asset://" + std::string(input.SourceRelPath),
                                 fileRelPath, AssetType::Script });
    return result;
}

uint64_t ScriptImporter::ComputeDependencyHash(const ImportInput& input) const
{
    // Depth-first over the import closure, hashing dependency contents in
    // visit order. The main file's own bytes are covered by SourceHash.
    const std::filesystem::path root(AssetsRoot);
    std::set<std::string> visited;
    std::string concatenated;

    struct Walker
    {
        const std::filesystem::path& Root;
        std::set<std::string>& Visited;
        std::string& Out;

        void Visit(std::string_view fromPath, std::string_view source)
        {
            std::vector<std::string> imports;
            ScanImports(source, imports);
            for (const std::string& import : imports)
            {
                const std::string resolved = ResolveRelative(fromPath, import);
                if (!Visited.insert(resolved).second)
                {
                    continue;
                }
                std::string text;
                if (!ReadTextFile(Root / std::filesystem::path(resolved), text))
                {
                    // Unreadable imports still perturb the hash so the source
                    // recooks (and reports the real error) when they appear.
                    Out += '\0';
                    Out += "missing:";
                    Out += resolved;
                    continue;
                }
                Out += text;
                Visit(resolved, text);
            }
        }
    };

    const std::string source(reinterpret_cast<const char*>(input.Bytes.data()),
                             input.Bytes.size());
    Walker{root, visited, concatenated}.Visit(input.SourceRelPath, source);
    return concatenated.empty() ? 0 : HashBytes64(std::string_view(concatenated));
}
