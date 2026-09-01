//=============================================================================
// sencha-component-codegen
//
// Reads the annotations on a component declaration and writes the
// ComponentDefinition the engine projects its metadata from, plus a small index
// sidecar the aggregate validation stage reads to find collisions across
// headers.
//
// It links no engine code and knows nothing about behavior: ComponentTraits,
// ComponentStorageTraits and SceneFieldCodec are handwritten and this tool
// neither reads nor emits them.
//
//   sencha-component-codegen <header> --output=<companion.h> --index=<f.index>
//                            --logical=<world/transform/X.h> -- <compile flags>
//=============================================================================

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace
{

llvm::cl::OptionCategory gCategory("sencha-component-codegen");

llvm::cl::opt<std::string> gOutput(
    "output", llvm::cl::desc("Companion header to write"),
    llvm::cl::value_desc("path"), llvm::cl::cat(gCategory));

llvm::cl::opt<std::string> gIndex(
    "index", llvm::cl::desc("Index sidecar to write"),
    llvm::cl::value_desc("path"), llvm::cl::cat(gCategory));

llvm::cl::opt<std::string> gLogical(
    "logical", llvm::cl::desc("Logical include path of the parsed header"),
    llvm::cl::value_desc("path"), llvm::cl::cat(gCategory));

llvm::cl::opt<std::string> gResourceDir(
    "resource-dir", llvm::cl::desc("Clang builtin header directory"),
    llvm::cl::value_desc("path"), llvm::cl::init(SENCHA_CLANG_RESOURCE_DIR),
    llvm::cl::cat(gCategory));

llvm::cl::opt<std::string> gFlags(
    "flags", llvm::cl::desc("File of compile flags, one per line"),
    llvm::cl::value_desc("path"), llvm::cl::cat(gCategory));

llvm::cl::opt<std::string> gSource(
    llvm::cl::Positional, llvm::cl::desc("<header>"), llvm::cl::Required,
    llvm::cl::cat(gCategory));

// Kept in step with kComponentCodegenFormatVersion in ComponentDefinition.h.
// Stamped into every companion so an SDK whose generator predates its headers
// fails at compile naming both versions.
constexpr unsigned kFormatVersion = 1;

// ─── The facts a declaration can state ───────────────────────────────────────

struct FieldFacts
{
    std::string Member;      // C++ member name
    std::string Name;        // serialized name
    std::string AssetKind;   // AssetType enumerator, if any
    std::string AssetArity;  // "Single" | "List"
    std::string DataSubtype;
    std::string Label;
    std::string Tooltip;
    std::string Quantize;    // "min,max,bits"
    bool OwnerOnly = false;
    bool OwnerLocal = false;
    bool LocalOnly = false;
    bool Color = false;
    bool Degrees = false;
    bool Optional = false;
    bool HasDefault = false; // member initializer present
};

struct ComponentFacts
{
    std::string Type;
    std::string Identity;
    std::string SchemaName;
    std::string SceneChunk;
    std::string VisualMesh;
    bool Replicated = false;
    bool Predicted = false;
    bool NonRemovable = false;
    std::vector<FieldFacts> Fields;
    unsigned Line = 0;
};

// ─── Annotation decoding ─────────────────────────────────────────────────────
//
// There is no grammar here on purpose: an annotation is a prefix and, for the
// ones that carry a value, everything after the first '='.

bool Split(llvm::StringRef text, llvm::StringRef prefix, std::string& value)
{
    if (!text.starts_with(prefix))
        return false;
    value = text.drop_front(prefix.size()).str();
    return true;
}

class Visitor : public clang::RecursiveASTVisitor<Visitor>
{
public:
    explicit Visitor(clang::ASTContext& context) : Context(context) {}

    bool VisitCXXRecordDecl(clang::CXXRecordDecl* record)
    {
        // The injected-class-name is a second, implicit record carrying the same
        // attributes; emitting it too would define the component twice.
        if (record->isImplicit() || !record->isThisDeclarationADefinition()
            || record->getName().empty())
        {
            return true;
        }

        // Only what this header declares. Everything it includes is somebody
        // else's companion to emit. Asked of the SourceManager rather than by
        // comparing path spellings, which differ between the command line and
        // the file entry.
        const clang::SourceManager& sources = Context.getSourceManager();
        if (!sources.isInMainFile(sources.getExpansionLoc(record->getLocation())))
            return true;

        ComponentFacts facts;
        facts.Type = record->getNameAsString();
        facts.Line = sources.getExpansionLineNumber(record->getLocation());

        bool annotated = false;
        for (const auto* attr : record->specific_attrs<clang::AnnotateAttr>())
        {
            const llvm::StringRef text = attr->getAnnotation();
            if (!text.starts_with("sencha."))
                continue;
            annotated = true;
            std::string value;
            if (Split(text, "sencha.identity=", value))          facts.Identity = value;
            else if (Split(text, "sencha.schema=", value))       facts.SchemaName = value;
            else if (Split(text, "sencha.scene_chunk=", value))  facts.SceneChunk = value;
            else if (Split(text, "sencha.visual_mesh=", value))  facts.VisualMesh = value;
            else if (text == "sencha.replicated")                facts.Replicated = true;
            else if (text == "sencha.predicted")                 facts.Predicted = true;
            else if (text == "sencha.non_removable")             facts.NonRemovable = true;
            else Error(record->getLocation(), "unknown component annotation '" + text.str() + "'");
        }

        if (!annotated)
            return true;

        if (facts.Identity.empty())
        {
            Error(record->getLocation(),
                  "component '" + facts.Type + "' has annotations but no SENCHA_COMPONENT identity");
            return true;
        }
        if (!facts.SceneChunk.empty() && facts.SceneChunk.size() != 4)
        {
            Error(record->getLocation(),
                  "scene chunk '" + facts.SceneChunk + "' must be exactly four characters");
        }
        if (facts.Predicted && !facts.Replicated)
        {
            Error(record->getLocation(),
                  "component '" + facts.Type + "' is predicted but not replicated: "
                  "prediction resumes from a value that only arrives if it travels");
        }

        for (const clang::FieldDecl* field : record->fields())
            CollectField(field, facts);

        if (!facts.Fields.empty() && facts.SchemaName.empty())
        {
            Error(record->getLocation(),
                  "component '" + facts.Type + "' declares fields but no SENCHA_SCHEMA name");
        }

        Components.push_back(std::move(facts));
        return true;
    }

    std::vector<ComponentFacts> Components;
    bool Failed = false;

private:
    void CollectField(const clang::FieldDecl* field, ComponentFacts& facts)
    {
        FieldFacts out;
        bool tagged = false;
        for (const auto* attr : field->specific_attrs<clang::AnnotateAttr>())
        {
            const llvm::StringRef text = attr->getAnnotation();
            if (!text.starts_with("sencha."))
                continue;
            std::string value;
            if (Split(text, "sencha.field=", value))            { out.Name = value; tagged = true; }
            else if (Split(text, "sencha.asset=", value))       { out.AssetKind = value; out.AssetArity = "Single"; }
            else if (Split(text, "sencha.asset_list=", value))  { out.AssetKind = value; out.AssetArity = "List"; }
            else if (Split(text, "sencha.data_asset=", value))  out.DataSubtype = value;
            else if (Split(text, "sencha.label=", value))       out.Label = value;
            else if (Split(text, "sencha.tooltip=", value))     out.Tooltip = value;
            else if (Split(text, "sencha.quantize=", value))    out.Quantize = value;
            else if (text == "sencha.owner_only")               out.OwnerOnly = true;
            else if (text == "sencha.owner_local")              out.OwnerLocal = true;
            else if (text == "sencha.local_only")               out.LocalOnly = true;
            else if (text == "sencha.color")                    out.Color = true;
            else if (text == "sencha.degrees")                  out.Degrees = true;
            else if (text == "sencha.optional")                 out.Optional = true;
            else Error(field->getLocation(), "unknown field annotation '" + text.str() + "'");
        }

        if (!tagged)
        {
            // An annotation on an untagged member is a mistake worth reporting:
            // the member is not in the schema, so the annotation does nothing.
            if (!out.AssetKind.empty() || out.OwnerOnly || out.LocalOnly || !out.Label.empty())
            {
                Error(field->getLocation(),
                      "member '" + field->getNameAsString() + "' carries field annotations "
                      "but no SENCHA_FIELD, so it is not part of the schema");
            }
            return;
        }

        out.Member = field->getNameAsString();
        out.HasDefault = field->hasInClassInitializer();
        facts.Fields.push_back(std::move(out));
    }

    void Error(clang::SourceLocation where, const std::string& message)
    {
        Failed = true;
        clang::DiagnosticsEngine& diagnostics = Context.getDiagnostics();
        const unsigned id = diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
        diagnostics.Report(where, id) << message;
    }

    clang::ASTContext& Context;
};

// ─── Emission ────────────────────────────────────────────────────────────────

std::string Quoted(const std::string& text) { return "\"" + text + "\""; }

// The macro stringifies its arguments without spacing; generated code should
// read the way a person would have written it.
std::string Spaced(llvm::StringRef csv)
{
    std::string out;
    llvm::SmallVector<llvm::StringRef, 4> parts;
    csv.split(parts, ',');
    for (llvm::StringRef part : parts)
    {
        if (!out.empty())
            out += ", ";
        out += part.trim().str();
    }
    return out;
}

void EmitField(std::ostream& out, const ComponentFacts& component, const FieldFacts& field)
{
    out << "            MakeField(" << Quoted(field.Name)
        << ", &" << component.Type << "::" << field.Member << ")";
    if (field.HasDefault)
        out << "\n                .Default(defaults." << field.Member << ")";
    if (!field.AssetKind.empty())
    {
        if (!field.DataSubtype.empty())
            out << "\n                .AsDataAsset(" << Quoted(field.DataSubtype) << ")";
        else
            out << "\n                .AsAsset(AssetType::" << field.AssetKind
                << ", AssetArity::" << field.AssetArity << ")";
    }
    else if (!field.DataSubtype.empty())
    {
        out << "\n                .AsDataAsset(" << Quoted(field.DataSubtype) << ")";
    }
    if (field.Optional)   out << "\n                .Optional()";
    if (field.Color)      out << "\n                .AsColor()";
    if (field.Degrees)    out << "\n                .Degrees()";
    if (!field.Quantize.empty()) out << "\n                .Quantize(" << Spaced(field.Quantize) << ")";
    if (field.OwnerOnly)  out << "\n                .OwnerOnly()";
    if (field.OwnerLocal) out << "\n                .OwnerLocal()";
    if (field.LocalOnly)  out << "\n                .LocalOnly()";
    if (!field.Label.empty())   out << "\n                .Label(" << Quoted(field.Label) << ")";
    if (!field.Tooltip.empty()) out << "\n                .Tooltip(" << Quoted(field.Tooltip) << ")";
    out << ",\n";
}

// A schema with no fields is still a schema: a tag the scene can place.
void EmitFields(std::ostream& out, const ComponentFacts& component)
{
    if (component.Fields.empty())
    {
        out << "\n    static auto Fields() { return std::tuple{}; }\n";
        return;
    }

    out << "\n    static auto Fields()\n    {\n";
    const bool needsDefaults = std::any_of(
        component.Fields.begin(), component.Fields.end(),
        [](const FieldFacts& f) { return f.HasDefault; });
    if (needsDefaults)
        out << "        const " << component.Type << " defaults;\n";
    out << "        return std::tuple{\n";
    for (const FieldFacts& field : component.Fields)
        EmitField(out, component, field);
    out << "        };\n    }\n";
}

bool WriteCompanion(const std::string& path,
                    const std::string& logical,
                    const std::vector<ComponentFacts>& components)
{
    std::ofstream out(path);
    if (!out)
    {
        llvm::errs() << "sencha-component-codegen: cannot write " << path << "\n";
        return false;
    }

    out << "// Generated by sencha-component-codegen from " << logical << ".\n"
        << "// Do not edit: change the annotations on the component instead.\n"
        << "#pragma once\n\n"
        << "#include <core/metadata/ComponentDefinition.h>\n"
        << "#include <core/metadata/Field.h>\n"
        << "#include <core/serialization/FourCC.h>\n\n"
        << "static_assert(kComponentCodegenFormatVersion == " << kFormatVersion << ",\n"
        << "              \"generated component metadata predates the headers reading it: \"\n"
        << "              \"rebuild with a matching sencha-component-codegen\");\n";

    for (const ComponentFacts& component : components)
    {
        out << "\ntemplate <>\nstruct ComponentDefinition<" << component.Type << ">\n{\n";
        out << "    static constexpr std::string_view Identity = " << Quoted(component.Identity) << ";\n";
        if (!component.SchemaName.empty())
            out << "    static constexpr std::string_view SchemaName = " << Quoted(component.SchemaName) << ";\n";
        if (!component.SceneChunk.empty())
        {
            out << "    static constexpr std::uint32_t SceneChunk = MakeFourCC('"
                << component.SceneChunk[0] << "', '" << component.SceneChunk[1] << "', '"
                << component.SceneChunk[2] << "', '" << component.SceneChunk[3] << "');\n";
        }
        if (component.Replicated)   out << "    static constexpr bool Replicated = true;\n";
        if (component.Predicted)    out << "    static constexpr bool Predicted = true;\n";
        if (component.NonRemovable) out << "    static constexpr bool Removable = false;\n";
        if (!component.VisualMesh.empty())
            out << "    static constexpr std::string_view VisualMeshAsset = " << Quoted(component.VisualMesh) << ";\n";

        if (!component.SchemaName.empty())
            EmitFields(out, component);
        out << "};\n";
    }
    return true;
}

bool WriteIndex(const std::string& path,
                const std::string& logical,
                const std::vector<ComponentFacts>& components)
{
    std::ofstream out(path);
    if (!out)
    {
        llvm::errs() << "sencha-component-codegen: cannot write " << path << "\n";
        return false;
    }
    // Deterministic, line-oriented, never included by C++: the aggregate
    // validation stage reads these to find collisions across headers.
    for (const ComponentFacts& component : components)
    {
        out << component.Type << '\t' << component.Identity << '\t'
            << component.SchemaName << '\t' << component.SceneChunk << '\t'
            << logical << '\t' << component.Line << '\n';
    }
    return true;
}

class Consumer : public clang::ASTConsumer
{
public:
    void HandleTranslationUnit(clang::ASTContext& context) override
    {
        Visitor visitor(context);
        visitor.TraverseDecl(context.getTranslationUnitDecl());

        // Writing anything after a failed parse would replace a good companion
        // with an empty one, which reads as a component that no longer exists.
        if (visitor.Failed || context.getDiagnostics().hasErrorOccurred())
        {
            Ok = false;
            return;
        }
        // Declaration order, so output is deterministic.
        Ok = WriteCompanion(gOutput, gLogical, visitor.Components)
             && WriteIndex(gIndex, gLogical, visitor.Components);
    }

    static bool Ok;
};

bool Consumer::Ok = true;

class Action : public clang::ASTFrontendAction
{
public:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& compiler, llvm::StringRef file) override
    {
        (void)compiler;
        (void)file;
        return std::make_unique<Consumer>();
    }
};

} // namespace

// One flag per line, so a path containing spaces needs no quoting and the file
// CMake generates is the file the parse sees.
std::vector<std::string> ReadFlags(const std::string& path)
{
    // A component header is C++ whatever its extension says.
    std::vector<std::string> flags{ "-fsyntax-only", "-x", "c++",
                                    "-resource-dir=" + gResourceDir.getValue() };
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);)
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();

        // An empty definition or include directory arrives as a bare -D or -I,
        // which would swallow the next argument -- the source path among them.
        if (line.empty() || line == "-D" || line == "-I" || line == "-isystem")
            continue;

        flags.push_back(line);
    }
    return flags;
}

int main(int argc, const char** argv)
{
    llvm::cl::HideUnrelatedOptions(gCategory);
    if (!llvm::cl::ParseCommandLineOptions(argc, argv))
        return 2;
    if (gOutput.empty() || gIndex.empty() || gFlags.empty())
    {
        llvm::errs() << "sencha-component-codegen: --output, --index and --flags are required\n";
        return 2;
    }

    const std::vector<std::string> flags = ReadFlags(gFlags);
    if (flags.size() <= 1)
    {
        llvm::errs() << "sencha-component-codegen: no compile flags in " << gFlags << "\n";
        return 2;
    }

    clang::tooling::FixedCompilationDatabase compilations(".", flags);
    clang::tooling::ClangTool tool(compilations, { gSource.getValue() });
    if (tool.run(clang::tooling::newFrontendActionFactory<Action>().get()) != 0)
        return 1;
    return Consumer::Ok ? 0 : 1;
}
