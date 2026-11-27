//=============================================================================
// sencha-component-codegen
//
// Reads the annotations in a header and writes its companion --
// ComponentDefinition<T> for components, AuthoredApiDefinition<T> for authored
// verbs, queries and events -- plus an index sidecar for cross-header
// component collision checks.
//
// It links no engine code and knows nothing about behavior: ComponentTraits,
// ComponentStorageTraits and SceneFieldCodec are handwritten and this tool
// neither reads nor emits them. It never emits a registration: declaring and
// binding stay explicit calls in game code.
//
//   sencha-component-codegen <header> --output=<companion.h> --index=<f.index>
//                            --logical=<world/transform/X.h> --flags=<flags.rsp>
//   sencha-component-codegen --format-version
//=============================================================================

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
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

llvm::cl::opt<std::string> gDepfile(
    "depfile", llvm::cl::desc("Dependency file naming every header the parse read"),
    llvm::cl::value_desc("path"), llvm::cl::cat(gCategory));

llvm::cl::opt<std::string> gFlags(
    "flags", llvm::cl::desc("File of compile flags, one per line"),
    llvm::cl::value_desc("path"), llvm::cl::cat(gCategory));

llvm::cl::opt<bool> gPrintFormatVersion(
    "format-version", llvm::cl::desc("Print the companion format version and exit"),
    llvm::cl::cat(gCategory));

llvm::cl::opt<std::string> gSource(
    llvm::cl::Positional, llvm::cl::desc("<header>"), llvm::cl::cat(gCategory));

// Kept in step with kComponentCodegenFormatVersion in ComponentDefinition.h.
// Stamped into every companion so an SDK whose generator predates its headers
// fails at compile naming both versions, and reported by --format-version so
// the build can refuse such a generator at configure time.
constexpr unsigned kFormatVersion = 4;

// ─── The facts a declaration can state ───────────────────────────────────────

struct FieldFacts
{
    std::string Member;      // C++ member name
    std::string Name;        // serialized name
    std::string AssetRef;    // the Field call that binds the member to an asset, if any
    std::string Label;
    std::string Tooltip;
    std::string Quantize;    // "min,max,bits"
    bool OwnerOnly = false;
    bool OwnerLocal = false;
    bool LocalOnly = false;
    bool Color = false;
    bool Degrees = false;
    bool Optional = false;
    bool Runtime = false;
    bool Unsaved = false;
    bool HasDefault = false; // member initializer present
};

// Independent of whether the member is also a SENCHA_FIELD.
struct FieldQueryFacts
{
    std::string Member;
    std::string Query;       // the part after the component identity
    std::string Type;        // qualified member type
    std::string Label;
    std::string Description;
    unsigned Line = 0;
};

struct ComponentFacts
{
    std::string Type;
    std::string QualifiedType;
    std::string Identity;
    std::string SchemaName;
    std::string SceneChunk;
    std::string VisualMesh;
    std::string Placeable;
    std::string PlaceIcon;
    bool Replicated = false;
    bool Predicted = false;
    bool AuthorityMaterialized = false;
    bool NonRemovable = false;
    bool Saved = false;
    std::vector<FieldFacts> Fields;
    std::vector<FieldQueryFacts> Queries;
    unsigned Line = 0;
};

struct ParamFacts
{
    std::string Local;           // the adapter's variable for it
    std::string Name;            // the C++ parameter name, for diagnostics
    std::string Key;             // persisted argument key
    std::string Type;            // qualified value type, reference and cv removed
    std::string Label;
    std::string Description;
    std::string Units;
    std::string Widget;
    std::string TargetComponent; // as annotated, resolved by the compiler in the companion
    std::string RangeMin;
    std::string RangeMax;
    std::string Min;
    std::string Max;
    std::string AssetKind;
    std::string DataSubtype;
    std::string Default;         // a typed C++ constant; empty for none
    bool Target = false;
    bool Borrowed = false;
    unsigned Line = 0;
};

enum class MethodRole
{
    Verb,
    Query,
    Lease,
    Release,
    Holds,
};

enum class VerbReturn
{
    Admission,
    Answer,
    Refusal,
    Result,
};

struct MethodFacts
{
    MethodRole Role = MethodRole::Verb;
    std::string Identity;
    std::string Method;
    std::string Label;
    std::string Description;
    std::string Category;
    std::string ResultType;      // a query's qualified return type
    std::string ReturnType;
    VerbReturn Returns = VerbReturn::Admission;
    std::string ResultKey;
    std::string ResultComponent;
    std::string Prediction;
    bool Cosmetic = false;
    bool Completes = false;
    bool TakesInvocation = false;
    std::string Release;
    std::string Holds;
    std::vector<ParamFacts> Params;
    clang::SourceLocation Where;
    unsigned Line = 0;

    bool Dispatched() const { return Role == MethodRole::Verb || Role == MethodRole::Lease; }
    bool Entry() const { return Role != MethodRole::Release && Role != MethodRole::Holds; }
};

struct ProviderFacts
{
    std::string Type;            // qualified
    std::vector<MethodFacts> Methods;
    bool ChecksTargets = false;
    unsigned TargetLine = 0;
};

struct EventFieldFacts
{
    std::string Member;
    std::string Name;
    std::string Type;
    std::string Label;
    unsigned Line = 0;
};

struct EventFacts
{
    std::string Type;            // qualified
    std::string Identity;
    std::string Source;          // as annotated
    std::string Label;
    std::string Description;
    std::string Category;
    std::vector<EventFieldFacts> Fields;
    unsigned Line = 0;
};

struct RecordFacts
{
    std::string Type;
    std::vector<EventFieldFacts> Fields;
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

std::string Literal(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:   out += c; break;
        }
    }
    out += "\"";
    return out;
}

// Names generated adapters use, which parameter locals must not shadow.
bool IsReservedLocal(const std::string& name)
{
    static const std::set<std::string> reserved{ "self", "invocation", "arguments", "result",
                                                 "definition", "field", "member", "members" };
    return reserved.count(name) != 0;
}

class Visitor : public clang::RecursiveASTVisitor<Visitor>
{
public:
    explicit Visitor(clang::ASTContext& context)
        : Context(context)
        , Policy(context.getPrintingPolicy())
    {
        Policy.SuppressScope = false;
        Policy.FullyQualifiedName = true;
        Policy.SuppressUnwrittenScope = true;
    }

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
        facts.QualifiedType = record->getQualifiedNameAsString();
        facts.Line = sources.getExpansionLineNumber(record->getLocation());

        EventFacts event;
        event.Type = facts.QualifiedType;
        event.Line = facts.Line;

        bool annotated = false;
        bool isEvent = false;
        bool isRecord = false;
        bool eventOnly = false;  // an annotation only an event may carry
        for (const auto* attr : record->specific_attrs<clang::AnnotateAttr>())
        {
            const llvm::StringRef text = attr->getAnnotation();
            if (!text.starts_with("sencha."))
                continue;
            annotated = true;
            std::string value;
            if (Split(text, "sencha.identity=", value))           facts.Identity = value;
            else if (Split(text, "sencha.schema=", value))        facts.SchemaName = value;
            else if (Split(text, "sencha.scene_chunk=", value))   facts.SceneChunk = value;
            else if (Split(text, "sencha.visual_mesh=", value))   facts.VisualMesh = value;
            else if (Split(text, "sencha.placeable=", value))     facts.Placeable = value;
            else if (Split(text, "sencha.place_icon=", value))    facts.PlaceIcon = value;
            else if (text == "sencha.replicated")                 facts.Replicated = true;
            else if (text == "sencha.predicted")                  facts.Predicted = true;
            else if (text == "sencha.authority_materialized")     facts.AuthorityMaterialized = true;
            else if (text == "sencha.non_removable")              facts.NonRemovable = true;
            else if (text == "sencha.saved")                      facts.Saved = true;
            else if (text == "sencha.record")                     isRecord = true;
            else if (Split(text, "sencha.event=", value))         { event.Identity = value; isEvent = true; }
            else if (Split(text, "sencha.event_source=", value))  { event.Source = value; eventOnly = true; }
            else if (Split(text, "sencha.label=", value))         { event.Label = value; eventOnly = true; }
            else if (Split(text, "sencha.description=", value))   { event.Description = value; eventOnly = true; }
            else if (Split(text, "sencha.category=", value))      { event.Category = value; eventOnly = true; }
            else Error(record->getLocation(), "unknown component annotation '" + text.str() + "'");
        }

        const bool templated = record->getDescribedClassTemplate() != nullptr
                               || llvm::isa<clang::ClassTemplateSpecializationDecl>(record)
                               || record->isDependentContext();

        if (isRecord)
        {
            if (!facts.Identity.empty() || isEvent || eventOnly)
            {
                Error(record->getLocation(),
                      "'" + facts.Type + "' is a SENCHA_RECORD and carries component or event annotations: "
                      "a record is a result's shape and nothing else");
                return true;
            }
            if (templated)
            {
                Error(record->getLocation(), "record '" + facts.Type + "' is a template");
                return true;
            }
            CollectRecord(record);
            CollectMethods(record, facts.Type, /*isComponent=*/false, /*isEvent=*/true);
            return true;
        }

        if (isEvent)
        {
            if (!facts.Identity.empty())
            {
                Error(record->getLocation(),
                      "'" + facts.Type + "' is declared both a component and an event: a "
                      "component is state and an event says something happened, so they are "
                      "two declarations");
                return true;
            }
            if (templated)
            {
                Error(record->getLocation(), "event '" + facts.Type + "' is a template");
                return true;
            }
            CollectEvent(record, event);
            CollectMethods(record, facts.Type, /*isComponent=*/false, /*isEvent=*/true);
            return true;
        }

        if (eventOnly)
        {
            Error(record->getLocation(),
                  "'" + facts.Type + "' carries event annotations but no SENCHA_EVENT identity");
        }

        if (annotated)
        {
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
            if (!facts.PlaceIcon.empty() && facts.Placeable.empty())
            {
                Error(record->getLocation(),
                      "component '" + facts.Type + "' names a place icon but is not SENCHA_PLACEABLE: "
                      "an icon pictures a placeable component, and nothing would show this one");
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

            CollectMethods(record, facts.Type, /*isComponent=*/true, /*isEvent=*/false);
            Components.push_back(std::move(facts));
            return true;
        }

        // Neither a component nor an event: only methods may be authored here.
        for (const clang::FieldDecl* field : record->fields())
        {
            for (const auto* attr : field->specific_attrs<clang::AnnotateAttr>())
            {
                if (attr->getAnnotation().starts_with("sencha.query="))
                {
                    Error(field->getLocation(),
                          "member '" + field->getNameAsString() + "' of '" + facts.Type
                              + "' is a SENCHA_QUERY, but '" + facts.Type
                              + "' is not a SENCHA_COMPONENT: a member query reads a component "
                                "from an entity");
                }
            }
        }

        if (templated)
        {
            for (const clang::CXXMethodDecl* method : record->methods())
            {
                for (const auto* attr : method->specific_attrs<clang::AnnotateAttr>())
                {
                    if (attr->getAnnotation().starts_with("sencha."))
                    {
                        Error(method->getLocation(),
                              "'" + facts.Type + "' is a template: an authored method belongs "
                              "to one concrete type");
                        break;
                    }
                }
            }
            return true;
        }
        CollectMethods(record, facts.Type, /*isComponent=*/false, /*isEvent=*/false);
        return true;
    }

    std::vector<ComponentFacts> Components;
    std::vector<ProviderFacts> Providers;
    std::vector<EventFacts> Events;
    std::vector<RecordFacts> Records;
    bool Failed = false;

private:
    void CollectField(const clang::FieldDecl* field, ComponentFacts& facts)
    {
        FieldFacts out;
        FieldQueryFacts query;
        bool tagged = false;
        bool queried = false;
        for (const auto* attr : field->specific_attrs<clang::AnnotateAttr>())
        {
            const llvm::StringRef text = attr->getAnnotation();
            if (!text.starts_with("sencha."))
                continue;
            std::string value;
            if (Split(text, "sencha.field=", value))            { out.Name = value; tagged = true; }
            else if (Split(text, "sencha.query=", value))       { query.Query = value; queried = true; }
            else if (Split(text, "sencha.description=", value)) query.Description = value;
            else if (Split(text, "sencha.asset=", value))       BindAsset(field, out, "AsAsset(AssetType::" + value + ", AssetArity::Single)");
            else if (Split(text, "sencha.asset_list=", value))  BindAsset(field, out, "AsAsset(AssetType::" + value + ", AssetArity::List)");
            else if (Split(text, "sencha.data_asset=", value))  BindAsset(field, out, "AsDataAsset(" + value + ")");
            else if (Split(text, "sencha.label=", value))       out.Label = value;
            else if (Split(text, "sencha.tooltip=", value))     out.Tooltip = value;
            else if (Split(text, "sencha.quantize=", value))    out.Quantize = value;
            else if (text == "sencha.owner_only")               out.OwnerOnly = true;
            else if (text == "sencha.owner_local")              out.OwnerLocal = true;
            else if (text == "sencha.local_only")               out.LocalOnly = true;
            else if (text == "sencha.color")                    out.Color = true;
            else if (text == "sencha.degrees")                  out.Degrees = true;
            else if (text == "sencha.optional")                 out.Optional = true;
            else if (text == "sencha.runtime")                  out.Runtime = true;
            else if (text == "sencha.unsaved")                  out.Unsaved = true;
            else Error(field->getLocation(), "unknown field annotation '" + text.str() + "'");
        }

        if (queried)
        {
            query.Member = field->getNameAsString();
            query.Type = TypeName(field->getType().getUnqualifiedType());
            query.Label = out.Label;
            query.Line = Line(field->getLocation());
            if (query.Query.empty())
                Error(field->getLocation(), "SENCHA_QUERY on member '" + query.Member + "' has no name");
            for (const FieldQueryFacts& existing : facts.Queries)
            {
                if (existing.Query == query.Query)
                {
                    Error(field->getLocation(),
                          "query '" + query.Query + "' is declared twice on component '"
                              + facts.Type + "'");
                }
            }
            facts.Queries.push_back(query);
        }
        else if (!query.Description.empty())
        {
            Error(field->getLocation(),
                  "member '" + field->getNameAsString() + "' has a SENCHA_DESCRIPTION but no "
                  "SENCHA_QUERY for it to describe");
        }

        if (!tagged)
        {
            // An annotation on an untagged member is a mistake worth reporting:
            // the member is not in the schema, so the annotation does nothing. A
            // label also serves a query.
            if (!out.AssetRef.empty() || out.OwnerOnly || out.LocalOnly || out.Runtime || out.Unsaved
                || (!out.Label.empty() && !queried))
            {
                Error(field->getLocation(),
                      "member '" + field->getNameAsString() + "' carries field annotations "
                      "but no SENCHA_FIELD, so it is not part of the schema");
            }
            return;
        }

        out.Member = field->getNameAsString();
        out.HasDefault = field->hasInClassInitializer();
        CheckSavedForms(field, facts, out);
        facts.Fields.push_back(std::move(out));
    }

    void CheckSavedForms(const clang::FieldDecl* field, const ComponentFacts& facts, const FieldFacts& out)
    {
        if (!out.Runtime && !out.Unsaved)
            return;
        const std::string member = "member '" + field->getNameAsString() + "'";
        if (out.Runtime && out.Unsaved)
        {
            Error(field->getLocation(),
                  member + " is both SENCHA_RUNTIME and SENCHA_UNSAVED: a field in no record form "
                  "belongs in no schema");
            return;
        }
        const std::string annotation = out.Runtime ? "SENCHA_RUNTIME" : "SENCHA_UNSAVED";
        if (!facts.Saved)
        {
            Error(field->getLocation(),
                  member + " is " + annotation + ", but '" + facts.Type + "' is not SENCHA_SAVED, so no "
                  "save holds it");
        }
        if (!out.HasDefault)
        {
            Error(field->getLocation(),
                  member + " is " + annotation + " and has no member initializer: a record that leaves "
                  "it out reads it as its default");
        }
        if (out.Runtime && !out.AssetRef.empty())
        {
            Error(field->getLocation(),
                  member + " is SENCHA_RUNTIME and an asset reference: an asset is content's to name");
        }
    }

    // A member refers to one asset in one way; a second binding is a mistake,
    // not an override.
    void BindAsset(const clang::FieldDecl* field, FieldFacts& out, std::string call)
    {
        if (!out.AssetRef.empty())
        {
            Error(field->getLocation(),
                  "member '" + field->getNameAsString() + "' has more than one asset binding");
            return;
        }
        out.AssetRef = std::move(call);
    }

    void CollectEvent(const clang::CXXRecordDecl* record, EventFacts& event)
    {
        if (event.Identity.empty())
            Error(record->getLocation(), "SENCHA_EVENT on '" + event.Type + "' has no name");
        event.Fields = CollectMembers(record, "event '" + event.Identity + "'");
        Events.push_back(std::move(event));
    }

    void CollectRecord(const clang::CXXRecordDecl* record)
    {
        RecordFacts facts;
        facts.Type = record->getQualifiedNameAsString();
        facts.Line = Line(record->getLocation());
        facts.Fields = CollectMembers(record, "record '" + record->getNameAsString() + "'");
        if (facts.Fields.empty())
            Error(record->getLocation(), "SENCHA_RECORD '" + record->getNameAsString() + "' has no SENCHA_FIELD");
        Records.push_back(std::move(facts));
    }

    std::vector<EventFieldFacts> CollectMembers(const clang::CXXRecordDecl* record, const std::string& owner)
    {
        std::vector<EventFieldFacts> members;
        for (const clang::FieldDecl* field : record->fields())
        {
            EventFieldFacts out;
            bool tagged = false;
            for (const auto* attr : field->specific_attrs<clang::AnnotateAttr>())
            {
                const llvm::StringRef text = attr->getAnnotation();
                if (!text.starts_with("sencha."))
                    continue;
                std::string value;
                if (Split(text, "sencha.field=", value))       { out.Name = value; tagged = true; }
                else if (Split(text, "sencha.label=", value))  out.Label = value;
                else
                {
                    Error(field->getLocation(),
                          "annotation '" + text.str() + "' does not apply to the members of " + owner
                              + "; a member takes SENCHA_FIELD and SENCHA_LABEL");
                }
            }
            if (!tagged)
            {
                if (!out.Label.empty())
                {
                    Error(field->getLocation(),
                          "member '" + field->getNameAsString() + "' has a SENCHA_LABEL but no "
                          "SENCHA_FIELD, so it is not part of " + owner);
                }
                continue;
            }
            out.Member = field->getNameAsString();
            out.Type = TypeName(field->getType().getUnqualifiedType());
            out.Line = Line(field->getLocation());
            for (const EventFieldFacts& existing : members)
            {
                if (existing.Name == out.Name)
                {
                    Error(field->getLocation(),
                          "key '" + out.Name + "' is declared twice on " + owner);
                }
            }
            members.push_back(std::move(out));
        }
        return members;
    }

    void CollectMethods(const clang::CXXRecordDecl* record,
                        const std::string& typeName,
                        bool isComponent,
                        bool isEvent)
    {
        ProviderFacts provider;
        provider.Type = record->getQualifiedNameAsString();
        std::set<std::string> methodNames;
        std::set<std::string> identities;
        std::vector<MethodFacts> halves;

        for (const clang::CXXMethodDecl* method : record->methods())
        {
            if (method->isImplicit())
                continue;

            MethodFacts facts;
            bool annotated = false;
            int roles = 0;
            bool hasPrediction = false;
            for (const auto* attr : method->specific_attrs<clang::AnnotateAttr>())
            {
                const llvm::StringRef text = attr->getAnnotation();
                if (!text.starts_with("sencha."))
                    continue;
                annotated = true;
                std::string value;
                if (Split(text, "sencha.verb=", value))                  { facts.Identity = value; facts.Role = MethodRole::Verb; ++roles; }
                else if (Split(text, "sencha.query=", value))            { facts.Identity = value; facts.Role = MethodRole::Query; ++roles; }
                else if (Split(text, "sencha.lease=", value))            { facts.Identity = value; facts.Role = MethodRole::Lease; ++roles; }
                else if (Split(text, "sencha.lease_release=", value))    { facts.Identity = value; facts.Role = MethodRole::Release; ++roles; }
                else if (Split(text, "sencha.lease_holds=", value))      { facts.Identity = value; facts.Role = MethodRole::Holds; ++roles; }
                else if (Split(text, "sencha.label=", value))            facts.Label = value;
                else if (Split(text, "sencha.description=", value))      facts.Description = value;
                else if (Split(text, "sencha.category=", value))         facts.Category = value;
                else if (Split(text, "sencha.prediction=", value))       { facts.Prediction = value; hasPrediction = true; }
                else if (Split(text, "sencha.result=", value))           facts.ResultKey = value;
                else if (Split(text, "sencha.result_component=", value)) facts.ResultComponent = value;
                else if (text == "sencha.cosmetic")                      facts.Cosmetic = true;
                else if (text == "sencha.completes")                     facts.Completes = true;
                else Error(method->getLocation(), "unknown method annotation '" + text.str() + "'");
            }
            if (!annotated)
                continue;

            const std::string name = method->getNameAsString();
            if (isComponent || isEvent)
            {
                Error(method->getLocation(),
                      "'" + typeName + "::" + name + "' is an authored method on a "
                      + std::string(isComponent ? "component" : "event")
                      + ": components and events are data, so expose the operation or "
                        "question from the system that owns the behaviour");
                continue;
            }
            if (roles != 1)
            {
                Error(method->getLocation(),
                      "'" + typeName + "::" + name + "' needs exactly one of SENCHA_VERB, SENCHA_QUERY, "
                      "SENCHA_LEASE, SENCHA_LEASE_RELEASE and SENCHA_LEASE_HOLDS");
                continue;
            }
            facts.Method = name;
            facts.Where = method->getLocation();
            facts.Line = Line(method->getLocation());
            const std::string what = std::string(RoleAnnotation(facts.Role)) + "(\"" + facts.Identity + "\")";

            if (method->isStatic() || method->getAccess() != clang::AS_public
                || method->getDescribedFunctionTemplate() != nullptr
                || llvm::isa<clang::CXXConstructorDecl>(method)
                || llvm::isa<clang::CXXDestructorDecl>(method)
                || llvm::isa<clang::CXXConversionDecl>(method)
                || method->isOverloadedOperator())
            {
                Error(method->getLocation(),
                      what + ": '" + name + "' must be a public, non-static, non-template "
                      "member function");
                continue;
            }
            if (!methodNames.insert(name).second)
            {
                Error(method->getLocation(),
                      what + ": '" + name + "' is annotated more than once; an authored "
                      "method is not overloaded");
                continue;
            }
            if (facts.Identity.empty())
            {
                Error(method->getLocation(), what + ": the identity is empty");
                continue;
            }
            if (!CheckClassification(method, facts, hasPrediction, what))
                continue;

            if (facts.Role == MethodRole::Release || facts.Role == MethodRole::Holds)
            {
                if (CheckLeaseHalf(method, facts, what))
                    halves.push_back(std::move(facts));
                continue;
            }

            if (!identities.insert(facts.Identity).second)
            {
                Error(method->getLocation(),
                      what + ": '" + facts.Identity + "' is declared twice on '" + typeName + "'");
                continue;
            }

            if (facts.Dispatched() && !ClassifyVerbReturn(method, facts, what))
                continue;
            if (facts.Role == MethodRole::Query)
            {
                if (!method->isConst())
                {
                    Error(method->getLocation(),
                          what + ": '" + name + "' must be const: a query observes and never "
                          "changes simulation state");
                    continue;
                }
                if (method->getReturnType()->isVoidType())
                {
                    Error(method->getLocation(), what + ": '" + name + "' returns nothing");
                    continue;
                }
                facts.ResultType = TypeName(
                    method->getReturnType().getNonReferenceType().getUnqualifiedType());
            }

            const unsigned count = method->getNumParams();
            for (unsigned index = 0; index < count; ++index)
            {
                const clang::ParmVarDecl* param = method->getParamDecl(index);
                if (IsInvocationParameter(param))
                {
                    if (!facts.Dispatched() || index != 0)
                    {
                        Error(param->getLocation(),
                              what + ": only a verb or a lease takes the invocation, and only as its "
                              "first parameter");
                    }
                    else
                    {
                        facts.TakesInvocation = true;
                    }
                    continue;
                }
                CollectParam(param, index, what, facts);
            }
            if (facts.Dispatched())
            {
                for (const ParamFacts& param : facts.Params)
                {
                    if (param.Target && !provider.ChecksTargets)
                    {
                        provider.ChecksTargets = true;
                        provider.TargetLine = param.Line;
                    }
                }
            }

            provider.Methods.push_back(std::move(facts));
        }

        PairLeaseHalves(provider, halves);
        if (!provider.Methods.empty())
            Providers.push_back(std::move(provider));
    }

    static const char* RoleAnnotation(MethodRole role)
    {
        switch (role)
        {
        case MethodRole::Verb:    return "SENCHA_VERB";
        case MethodRole::Query:   return "SENCHA_QUERY";
        case MethodRole::Lease:   return "SENCHA_LEASE";
        case MethodRole::Release: return "SENCHA_LEASE_RELEASE";
        case MethodRole::Holds:   return "SENCHA_LEASE_HOLDS";
        }
        return "";
    }

    bool CheckClassification(const clang::CXXMethodDecl* method, const MethodFacts& facts, bool hasPrediction,
                             const std::string& what)
    {
        const bool classified = facts.Cosmetic || hasPrediction || facts.Completes || !facts.ResultKey.empty()
                                || !facts.ResultComponent.empty();
        if (classified && !facts.Dispatched())
        {
            Error(method->getLocation(),
                  what + ": SENCHA_COSMETIC, SENCHA_PREDICTION, SENCHA_COMPLETES and SENCHA_RESULT describe "
                  "a verb or a lease");
            return false;
        }
        if (facts.Completes && facts.Role != MethodRole::Lease)
        {
            Error(method->getLocation(),
                  what + ": SENCHA_COMPLETES on a verb; only a lease is held long enough to complete");
            return false;
        }
        if (facts.Role == MethodRole::Lease && !facts.ResultKey.empty())
        {
            Error(method->getLocation(), what + ": a lease answers no result");
            return false;
        }
        if (facts.Cosmetic && hasPrediction)
        {
            Error(method->getLocation(),
                  what + ": a cosmetic verb is predicted as presentation, so it names no SENCHA_PREDICTION");
            return false;
        }
        if (hasPrediction && facts.Prediction != "AuthorityOnly" && facts.Prediction != "Simulation")
        {
            Error(method->getLocation(),
                  what + ": SENCHA_PREDICTION(" + facts.Prediction + ") names AuthorityOnly or Simulation; "
                  "a presentation-predicted verb is SENCHA_COSMETIC");
            return false;
        }
        return true;
    }

    bool ClassifyVerbReturn(const clang::CXXMethodDecl* method, MethodFacts& facts, const std::string& what)
    {
        const clang::QualType returned = method->getReturnType().getNonReferenceType().getUnqualifiedType();
        facts.ReturnType = TypeName(returned);
        if (IsNamed(returned, "VerbAdmission"))
            facts.Returns = VerbReturn::Admission;
        else if (IsNamed(returned, "VerbAnswer"))
            facts.Returns = VerbReturn::Answer;
        else if (IsSpecializationOf(returned, "VerbRefusal"))
            facts.Returns = VerbReturn::Refusal;
        else if (IsSpecializationOf(returned, "VerbResult"))
            facts.Returns = VerbReturn::Result;
        else
        {
            Error(method->getLocation(),
                  what + ": '" + facts.Method + "' returns '" + TypeName(method->getReturnType())
                      + "'; a verb returns VerbAdmission, VerbAnswer, VerbRefusal<E> or VerbResult<T, E>");
            return false;
        }
        if (facts.Returns == VerbReturn::Result && facts.Role == MethodRole::Lease)
        {
            Error(method->getLocation(), what + ": a lease answers no result, so it returns no VerbResult");
            return false;
        }
        if (facts.Returns == VerbReturn::Result && facts.ResultKey.empty())
        {
            Error(method->getLocation(),
                  what + ": '" + facts.Method + "' returns VerbResult, so it names its result with SENCHA_RESULT");
            return false;
        }
        if (facts.Returns != VerbReturn::Result && !facts.ResultKey.empty())
        {
            Error(method->getLocation(),
                  what + ": '" + facts.Method + "' names a result with SENCHA_RESULT but returns no VerbResult");
            return false;
        }
        return true;
    }

    bool CheckLeaseHalf(const clang::CXXMethodDecl* method, const MethodFacts& facts, const std::string& what)
    {
        const clang::QualType returned = method->getReturnType();
        if (facts.Role == MethodRole::Release)
        {
            const bool onlyInvocation = method->getNumParams() == 1 && IsInvocationParameter(method->getParamDecl(0));
            if (!onlyInvocation)
            {
                Error(method->getLocation(),
                      what + ": '" + facts.Method + "' takes only the invocation; a release ends whatever "
                      "the owner holds, so it has no arguments");
                return false;
            }
            if (!IsNamed(returned, "VerbAdmission") && !IsNamed(returned, "VerbAnswer"))
            {
                Error(method->getLocation(),
                      what + ": '" + facts.Method + "' returns '" + TypeName(returned)
                          + "'; a release returns VerbAdmission or VerbAnswer");
                return false;
            }
            return true;
        }
        const bool takesOwner = method->getNumParams() == 1
                                && method->getParamDecl(0)->getType()->isLValueReferenceType()
                                && method->getParamDecl(0)->getType().getNonReferenceType().isConstQualified()
                                && IsNamed(method->getParamDecl(0)->getType().getNonReferenceType(), "LeaseOwner");
        if (!method->isConst() || !returned->isBooleanType() || !takesOwner)
        {
            Error(method->getLocation(),
                  what + ": '" + facts.Method + "' is a const method returning bool and taking only "
                  "const LeaseOwner&: whether the owner's hold goes on");
            return false;
        }
        return true;
    }

    void PairLeaseHalves(ProviderFacts& provider, const std::vector<MethodFacts>& halves)
    {
        for (const MethodFacts& half : halves)
        {
            const auto lease = std::find_if(provider.Methods.begin(), provider.Methods.end(), [&](const MethodFacts& m) {
                return m.Role == MethodRole::Lease && m.Identity == half.Identity;
            });
            const std::string what = std::string(RoleAnnotation(half.Role)) + "(\"" + half.Identity + "\")";
            if (lease == provider.Methods.end())
            {
                Error(half.Where, what + ": '" + half.Method + "' names no SENCHA_LEASE on this type");
                continue;
            }
            std::string& slot = half.Role == MethodRole::Release ? lease->Release : lease->Holds;
            if (!slot.empty())
            {
                Error(half.Where, what + ": the lease already has '" + slot + "'");
                continue;
            }
            if (half.Role == MethodRole::Holds && !lease->Completes)
            {
                Error(half.Where,
                      what + ": '" + half.Method + "' names a lease that does not complete; only a "
                      "SENCHA_COMPLETES lease is asked whether a hold goes on");
                continue;
            }
            slot = half.Method;
        }
        for (const MethodFacts& lease : provider.Methods)
        {
            if (lease.Role != MethodRole::Lease)
                continue;
            if (lease.Release.empty())
            {
                Error(lease.Where,
                      "SENCHA_LEASE(\"" + lease.Identity + "\"): the lease has no SENCHA_LEASE_RELEASE; "
                      "an owner must be able to let go");
            }
            if (lease.Completes && lease.Holds.empty())
            {
                Error(lease.Where,
                      "SENCHA_LEASE(\"" + lease.Identity + "\"): the lease completes but has no "
                      "SENCHA_LEASE_HOLDS to say whether a hold goes on");
            }
        }
    }

    void CollectParam(const clang::ParmVarDecl* param,
                      unsigned index,
                      const std::string& what,
                      MethodFacts& method)
    {
        ParamFacts out;
        out.Name = param->getNameAsString();
        out.Line = Line(param->getLocation());
        const std::string shown = out.Name.empty() ? "#" + std::to_string(index + 1) : out.Name;

        bool argument = false;
        bool hasRange = false;
        for (const auto* attr : param->specific_attrs<clang::AnnotateAttr>())
        {
            const llvm::StringRef text = attr->getAnnotation();
            if (!text.starts_with("sencha."))
                continue;
            std::string value;
            if (Split(text, "sencha.arg=", value))                     { out.Key = value; argument = true; }
            else if (Split(text, "sencha.target=", value))             { out.Key = value; out.Target = true; }
            else if (Split(text, "sencha.target_component=", value))   out.TargetComponent = value;
            else if (Split(text, "sencha.label=", value))              out.Label = value;
            else if (Split(text, "sencha.description=", value))        out.Description = value;
            else if (Split(text, "sencha.units=", value))              out.Units = value;
            else if (Split(text, "sencha.widget=", value))             out.Widget = value;
            else if (Split(text, "sencha.min=", value))                out.Min = value;
            else if (Split(text, "sencha.max=", value))                out.Max = value;
            else if (Split(text, "sencha.asset=", value))              out.AssetKind = value;
            else if (Split(text, "sencha.data_asset=", value))         out.DataSubtype = value;
            else if (Split(text, "sencha.range=", value))
            {
                hasRange = true;
                const size_t comma = value.find(',');
                out.RangeMin = llvm::StringRef(value).substr(0, comma).trim().str();
                out.RangeMax = comma == std::string::npos
                                   ? std::string{}
                                   : llvm::StringRef(value).substr(comma + 1).trim().str();
            }
            else if (text == "sencha.optional")
            {
                Error(param->getLocation(),
                      what + ": parameter '" + shown + "' is SENCHA_OPTIONAL; an argument that "
                      "may be absent is declared std::optional<T>");
            }
            else
            {
                Error(param->getLocation(),
                      what + ": unknown parameter annotation '" + text.str() + "'");
            }
        }

        if (argument == out.Target)
        {
            Error(param->getLocation(),
                  what + ": parameter '" + shown + "' needs exactly one of SENCHA_ARG and "
                  "SENCHA_TARGET; every authored argument is named by the content that "
                  "supplies it");
            return;
        }
        if (out.Key.empty())
        {
            Error(param->getLocation(), what + ": parameter '" + shown + "' has an empty key");
            return;
        }
        for (const ParamFacts& existing : method.Params)
        {
            if (existing.Key == out.Key)
            {
                Error(param->getLocation(),
                      what + ": argument key '" + out.Key + "' is used twice");
                return;
            }
        }

        const clang::QualType declared = param->getType();
        if (declared->isRValueReferenceType()
            || (declared->isLValueReferenceType()
                && !declared.getNonReferenceType().isConstQualified()))
        {
            Error(param->getLocation(),
                  what + ": parameter '" + shown + "' is a non-const reference; an authored "
                  "argument is a value the caller supplied, not an output");
            return;
        }
        out.Borrowed = declared->isLValueReferenceType();
        const clang::QualType value = declared.getNonReferenceType().getUnqualifiedType();
        out.Type = TypeName(value);

        if (out.Target && !IsNamed(value, "EntityId") && !IsOptionalOf(value, "EntityId"))
        {
            Error(param->getLocation(),
                  what + ": parameter '" + shown + "' uses SENCHA_TARGET"
                      + (out.TargetComponent.empty() ? "" : "(" + out.TargetComponent + ")")
                      + " but has type " + TypeName(declared));
            return;
        }
        if (!out.Target && !out.TargetComponent.empty())
        {
            Error(param->getLocation(),
                  what + ": parameter '" + shown + "' names a target component but is not a "
                  "SENCHA_TARGET");
            return;
        }
        if (hasRange && out.RangeMax.empty())
        {
            Error(param->getLocation(),
                  what + ": parameter '" + shown + "' has a SENCHA_RANGE without a maximum");
            return;
        }
        if (hasRange && (!out.Min.empty() || !out.Max.empty()))
        {
            Error(param->getLocation(),
                  what + ": parameter '" + shown + "' has both SENCHA_RANGE and SENCHA_MIN or SENCHA_MAX; "
                  "a range states both ends");
            return;
        }
        if (!out.AssetKind.empty() && !IsNamed(value, "AssetRef") && !IsOptionalOf(value, "AssetRef"))
        {
            Error(param->getLocation(),
                  what + ": parameter '" + shown + "' is SENCHA_ASSET(" + out.AssetKind + ") but has type "
                      + TypeName(declared) + "; SENCHA_ASSET applies to an AssetRef parameter");
            return;
        }
        if (!out.DataSubtype.empty() && !IsNamed(value, "AuthoredDataRef") && !IsOptionalOf(value, "AuthoredDataRef"))
        {
            Error(param->getLocation(),
                  what + ": parameter '" + shown + "' is SENCHA_DATA_ASSET but has type " + TypeName(declared)
                      + "; SENCHA_DATA_ASSET applies to an AuthoredDataRef parameter");
            return;
        }

        if (param->hasDefaultArg() || param->hasUninstantiatedDefaultArg()
            || param->hasUnparsedDefaultArg())
        {
            DescribeDefault(param, value, what, shown, out);
        }

        out.Local = out.Name.empty() ? "argument" + std::to_string(index)
                  : IsReservedLocal(out.Name) ? out.Name + "_"
                  : out.Name;
        method.Params.push_back(std::move(out));
    }

    // Emits the default as a typed constant; non-constant defaults are refused.
    void DescribeDefault(const clang::ParmVarDecl* param,
                         clang::QualType type,
                         const std::string& what,
                         const std::string& shown,
                         ParamFacts& out)
    {
        const clang::Expr* expression = param->hasDefaultArg() ? param->getDefaultArg() : nullptr;
        if (expression == nullptr)
        {
            Error(param->getLocation(),
                  what + ": the default of parameter '" + shown + "' is not a constant");
            return;
        }

        if (IsOptionalOf(type, ""))
        {
            // Present only for C++ parameter ordering; it declares no default.
            const std::string text = SourceText(expression);
            if (text != "std::nullopt" && text != "nullopt" && text != "{}")
            {
                Error(param->getLocation(),
                      what + ": optional parameter '" + shown + "' defaults to '" + text
                          + "'; an optional argument's default is std::nullopt");
            }
            return;
        }

        if (IsNamed(type, "std::basic_string") || IsNamed(type, "std::basic_string_view"))
        {
            if (const clang::StringLiteral* literal = FindStringLiteral(expression))
            {
                const char* spelled = IsNamed(type, "std::basic_string") ? "std::string{ " : "std::string_view{ ";
                out.Default = spelled + Literal(literal->getString().str()) + " }";
                return;
            }
            Error(param->getLocation(),
                  what + ": the default of parameter '" + shown + "' is not a string literal");
            return;
        }

        clang::Expr::EvalResult result;
        if (!expression->EvaluateAsRValue(result, Context) || result.HasSideEffects)
        {
            Error(param->getLocation(),
                  what + ": the default of parameter '" + shown + "' is not a constant");
            return;
        }
        const clang::APValue& constant = result.Val;
        if (type->isBooleanType() && constant.isInt())
        {
            out.Default = constant.getInt().getBoolValue() ? "true" : "false";
        }
        else if ((type->isEnumeralType() || type->isIntegerType()) && constant.isInt())
        {
            out.Default = "static_cast<" + out.Type + ">(" + llvm::toString(constant.getInt(), 10)
                        + ")";
        }
        else if (type->isRealFloatingType() && constant.isFloat())
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.17g", constant.getFloat().convertToDouble());
            out.Default = "static_cast<" + out.Type + ">(" + buffer + ")";
        }
        else
        {
            // Rejected by the companion's CanRepresentSchemaDefault assertion.
            out.Default = "static_cast<" + out.Type + ">(" + SourceText(expression) + ")";
        }
    }

    bool IsInvocationParameter(const clang::ParmVarDecl* param) const
    {
        const clang::QualType type = param->getType();
        return type->isLValueReferenceType() && type.getNonReferenceType().isConstQualified()
            && IsNamed(type.getNonReferenceType(), "VerbInvocation");
    }

    static bool IsNamed(clang::QualType type, llvm::StringRef qualified)
    {
        const clang::QualType canonical = type.getCanonicalType().getUnqualifiedType();
        if (const clang::TagDecl* tag = canonical->getAsTagDecl())
        {
            std::string name = tag->getQualifiedNameAsString();
            // libstdc++ spells std::string's template in an inline namespace.
            if (name.rfind("std::__cxx11::", 0) == 0)
                name = "std::" + name.substr(std::string("std::__cxx11::").size());
            return name == qualified;
        }
        return false;
    }

    // An empty `element` matches any std::optional.
    static bool IsOptionalOf(clang::QualType type, llvm::StringRef element)
    {
        const clang::QualType canonical = type.getCanonicalType().getUnqualifiedType();
        const auto* specialization =
            llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(canonical->getAsCXXRecordDecl());
        if (specialization == nullptr
            || specialization->getSpecializedTemplate()->getQualifiedNameAsString() != "std::optional")
        {
            return false;
        }
        if (element.empty())
            return true;
        const clang::TemplateArgumentList& arguments = specialization->getTemplateArgs();
        return arguments.size() == 1 && arguments[0].getKind() == clang::TemplateArgument::Type
            && IsNamed(arguments[0].getAsType(), element);
    }

    static bool IsSpecializationOf(clang::QualType type, llvm::StringRef qualified)
    {
        const clang::QualType canonical = type.getCanonicalType().getUnqualifiedType();
        const auto* specialization =
            llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(canonical->getAsCXXRecordDecl());
        return specialization != nullptr
            && specialization->getSpecializedTemplate()->getQualifiedNameAsString() == qualified;
    }

    static const clang::StringLiteral* FindStringLiteral(const clang::Stmt* statement)
    {
        if (statement == nullptr)
            return nullptr;
        if (const auto* literal = llvm::dyn_cast<clang::StringLiteral>(statement))
            return literal;
        for (const clang::Stmt* child : statement->children())
        {
            if (const clang::StringLiteral* found = FindStringLiteral(child))
                return found;
        }
        return nullptr;
    }

    std::string SourceText(const clang::Expr* expression) const
    {
        const clang::SourceManager& sources = Context.getSourceManager();
        const clang::CharSourceRange range =
            clang::CharSourceRange::getTokenRange(expression->getSourceRange());
        return clang::Lexer::getSourceText(range, sources, Context.getLangOpts()).trim().str();
    }

    // Fully qualified, typedef sugar kept.
    std::string TypeName(clang::QualType type) const
    {
        return clang::TypeName::getFullyQualifiedName(type, Context, Policy);
    }

    unsigned Line(clang::SourceLocation where) const
    {
        return Context.getSourceManager().getExpansionLineNumber(where);
    }

    void Error(clang::SourceLocation where, const std::string& message)
    {
        Failed = true;
        clang::DiagnosticsEngine& diagnostics = Context.getDiagnostics();
        const unsigned id = diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
        diagnostics.Report(where, id) << message;
    }

    clang::ASTContext& Context;
    clang::PrintingPolicy Policy;
};

// ─── Emission ────────────────────────────────────────────────────────────────

std::string Quoted(const std::string& text) { return Literal(text); }

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
    if (!field.AssetRef.empty()) out << "\n                ." << field.AssetRef;
    if (field.Optional)   out << "\n                .Optional()";
    if (field.Color)      out << "\n                .AsColor()";
    if (field.Degrees)    out << "\n                .Degrees()";
    if (!field.Quantize.empty()) out << "\n                .Quantize(" << Spaced(field.Quantize) << ")";
    if (field.OwnerOnly)  out << "\n                .OwnerOnly()";
    if (field.OwnerLocal) out << "\n                .OwnerLocal()";
    if (field.LocalOnly)  out << "\n                .LocalOnly()";
    if (field.Runtime)    out << "\n                .Runtime()";
    if (field.Unsaved)    out << "\n                .Unsaved()";
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

std::string Where(const std::string& logical, unsigned line)
{
    return logical + ":" + std::to_string(line);
}

// Emitted so an unsupported type fails with the declaration's location
// before any template error.
void EmitTypeAssert(std::ostream& out,
                    const std::string& requirement,
                    const std::string& checked,
                    const std::string& type,
                    const std::string& where,
                    const std::string& subject,
                    const std::string& role)
{
    out << "    static_assert(" << requirement << "<" << checked << ">,\n"
        << "                  " << Literal(where + ": " + subject + " has type '" + type
                                           + "', which is not an authored " + role + " type")
        << ");\n";
}

void EmitResultAsserts(std::ostream& out,
                       const std::string& type,
                       const std::string& where,
                       const std::string& subject)
{
    EmitTypeAssert(out, "IsAuthoredResultType", "AuthoredQueryValueType<" + type + ">", type, where, subject,
                   "result");
}

std::string MethodLabel(const MethodFacts& method)
{
    const char* annotation = method.Role == MethodRole::Query ? "SENCHA_QUERY"
                           : method.Role == MethodRole::Lease ? "SENCHA_LEASE"
                           : "SENCHA_VERB";
    return std::string(annotation) + "(\"" + method.Identity + "\")";
}

void EmitBoundAssert(std::ostream& out, const std::string& check, const std::string& where,
                     const std::string& message)
{
    out << "    static_assert(" << check << ",\n"
        << "                  " << Literal(where + ": " + message) << ");\n";
}

void EmitParamAsserts(std::ostream& out,
                      const MethodFacts& method,
                      const ParamFacts& param,
                      const std::string& logical)
{
    const std::string what = MethodLabel(method);
    const std::string where = Where(logical, param.Line);
    EmitTypeAssert(out, "IsAuthoredArgumentType", param.Type, param.Type, where,
                   what + ": parameter '" + param.Name + "'", "argument");
    if (!param.TargetComponent.empty())
    {
        out << "    static_assert(AuthoredComponentType<" << param.TargetComponent << ">,\n"
            << "                  "
            << Literal(where + ": " + what + ": SENCHA_TARGET(" + param.TargetComponent
                       + ") on parameter '" + param.Name + "' names a type that is not a "
                       "component")
            << ");\n";
    }
    if (!param.Default.empty())
    {
        out << "    static_assert(CanRepresentSchemaDefault<" << param.Type << ">,\n"
            << "                  "
            << Literal(where + ": " + what + ": parameter '" + param.Name + "' of type '"
                       + param.Type + "' cannot declare a schema default")
            << ");\n";
    }
    if (!param.RangeMin.empty())
    {
        EmitBoundAssert(out, "AuthoredRangeFits<" + param.Type + ">(" + param.RangeMin + ", " + param.RangeMax + ")",
                        where,
                        what + ": SENCHA_RANGE(" + param.RangeMin + ", " + param.RangeMax + ") on parameter '"
                            + param.Name + "' is not an ordered range inside what '" + param.Type + "' holds");
    }
    if (!param.Min.empty() && !param.Max.empty())
    {
        EmitBoundAssert(out, "AuthoredRangeFits<" + param.Type + ">(" + param.Min + ", " + param.Max + ")", where,
                        what + ": SENCHA_MIN(" + param.Min + ") and SENCHA_MAX(" + param.Max + ") on parameter '"
                            + param.Name + "' are not an ordered range inside what '" + param.Type + "' holds");
    }
    else
    {
        for (const auto& [bound, annotation] : { std::pair{ param.Min, "SENCHA_MIN" }, std::pair{ param.Max, "SENCHA_MAX" } })
        {
            if (!bound.empty())
            {
                EmitBoundAssert(out, "AuthoredBoundFits<" + param.Type + ">(" + bound + ")", where,
                                what + ": " + annotation + "(" + bound + ") on parameter '" + param.Name
                                    + "' is not a number inside what '" + param.Type + "' holds");
            }
        }
    }
}

void EmitArgumentSchema(std::ostream& out, const ParamFacts& param, const std::string& root)
{
    out << "        {\n"
        << "            DataFieldSchema field;\n"
        << "            field.Key = " << Literal(param.Key) << ";\n";
    if (!param.Label.empty())
        out << "            field.DisplayName = " << Literal(param.Label) << ";\n";
    if (!param.Description.empty())
        out << "            field.Summary = " << Literal(param.Description) << ";\n";
    if (!param.Units.empty())
        out << "            field.Units = " << Literal(param.Units) << ";\n";
    if (!param.Widget.empty())
        out << "            field.Editor.Widget = " << Literal(param.Widget) << ";\n";
    out << "            AuthoredValueTraits<" << param.Type << ">::Describe(field);\n";
    if (!param.TargetComponent.empty())
    {
        out << "            AuthoredValueField(field).Reference.ComponentIdentity =\n"
            << "                AuthoredComponentIdentity<" << param.TargetComponent << ">();\n";
    }
    if (!param.AssetKind.empty())
        out << "            AuthoredValueField(field).Reference.AssetTypeFilter = AssetType::" << param.AssetKind << ";\n";
    if (!param.DataSubtype.empty())
        out << "            AuthoredValueField(field).Reference.DataSubtype = std::string(" << param.DataSubtype << ");\n";
    const std::string& minimum = param.RangeMin.empty() ? param.Min : param.RangeMin;
    const std::string& maximum = param.RangeMax.empty() ? param.Max : param.RangeMax;
    if (!minimum.empty())
    {
        out << "            AuthoredValueField(field).Numeric.Minimum = static_cast<double>("
            << minimum << ");\n";
    }
    if (!maximum.empty())
    {
        out << "            AuthoredValueField(field).Numeric.Maximum = static_cast<double>("
            << maximum << ");\n";
    }
    if (!param.Default.empty())
    {
        out << "            field.Required = false;\n"
            << "            field.Default = AuthoredSchemaDefault<" << param.Type
            << ">::ToDefault(" << param.Default << ");\n";
    }
    out << "            " << root << ".Children.push_back(std::move(field));\n"
        << "        }\n";
}

void EmitPresentation(std::ostream& out, const std::string& label,
                      const std::string& description, const std::string& category)
{
    if (!label.empty())
        out << "        definition.DisplayName = " << Literal(label) << ";\n";
    if (!description.empty())
        out << "        definition.Description = " << Literal(description) << ";\n";
    if (!category.empty())
        out << "        definition.Category = " << Literal(category) << ";\n";
}

std::string ArgumentValue(const ParamFacts& param)
{
    return param.Borrowed ? param.Local + ".Get()" : param.Local;
}

template<typename SlotExpression, typename Refusal>
void EmitDecodes(std::ostream& out,
                 const MethodFacts& method,
                 SlotExpression slot,
                 Refusal refusal)
{
    for (size_t index = 0; index < method.Params.size(); ++index)
    {
        const ParamFacts& param = method.Params[index];
        if (param.Borrowed)
        {
            out << "        AuthoredHeldArgument<" << param.Type << "> " << param.Local << ";\n"
                << "        if (!" << param.Local << ".Read(" << slot(index) << "))\n";
        }
        else
        {
            out << "        " << param.Type << " " << param.Local << "{};\n"
                << "        if (!DecodeAuthoredArgument<" << param.Type << ">(self, " << slot(index)
                << ", " << param.Local << "))\n";
        }
        out << "            return " << refusal("InvalidArguments", index) << ";\n";
        if (method.Dispatched() && param.Target)
        {
            out << "        if (!AuthoredTargetAlive(self.AuthoredWorld(), " << ArgumentValue(param) << "))\n"
                << "            return " << refusal("UnresolvedReference", index) << ";\n";
        }
    }
}

std::string CallArguments(const MethodFacts& method)
{
    std::string out;
    if (method.TakesInvocation)
        out = "invocation";
    for (const ParamFacts& param : method.Params)
    {
        if (!out.empty())
            out += ", ";
        out += param.Borrowed ? ArgumentValue(param) : "std::move(" + param.Local + ")";
    }
    return out;
}

void EmitClassification(std::ostream& out, const MethodFacts& method)
{
    if (method.Role == MethodRole::Lease)
        out << "        definition.Kind = VerbKind::Lease;\n";
    if (method.Cosmetic)
    {
        out << "        definition.Scope = AuthoredScope::Cosmetic;\n"
            << "        definition.Prediction = AuthoredPrediction::Presentation;\n";
    }
    if (!method.Prediction.empty())
        out << "        definition.Prediction = AuthoredPrediction::" << method.Prediction << ";\n";
    if (method.Completes)
        out << "        definition.Completes = true;\n";
}

void EmitVerbAnswer(std::ostream& out, const MethodFacts& method)
{
    if (method.Returns == VerbReturn::Refusal || method.Returns == VerbReturn::Result)
        out << "        definition.Refusals = AuthoredVerbReturn<" << method.ReturnType << ">::Refusals();\n";
    if (method.Returns != VerbReturn::Result)
        return;
    out << "        {\n"
        << "            DataFieldSchema field;\n"
        << "            field.Key = " << Literal(method.ResultKey) << ";\n"
        << "            AuthoredVerbReturn<" << method.ReturnType << ">::DescribeResult(field);\n";
    if (!method.ResultComponent.empty())
    {
        out << "            AuthoredValueField(field).Reference.ComponentIdentity =\n"
            << "                AuthoredComponentIdentity<" << method.ResultComponent << ">();\n";
    }
    out << "            definition.Result = std::move(field);\n"
        << "        }\n";
}

void EmitVerb(std::ostream& out, const ProviderFacts& provider, const MethodFacts& method)
{
    out << "\n    static VerbDefinition Describe_" << method.Method << "()\n    {\n"
        << "        VerbDefinition definition;\n"
        << "        definition.Name = " << Literal(method.Identity) << ";\n";
    EmitPresentation(out, method.Label, method.Description, method.Category);
    EmitClassification(out, method);
    for (const ParamFacts& param : method.Params)
        EmitArgumentSchema(out, param, "definition.Arguments");
    EmitVerbAnswer(out, method);
    out << "        return definition;\n    }\n";

    out << "\n    static VerbAnswer Invoke_" << method.Method << "(" << provider.Type
        << "& self, const VerbInvocation& invocation)\n    {\n";
    if (method.Role == MethodRole::Lease)
    {
        out << "        if (invocation.Step == LeaseStep::Release)\n"
            << "            return AnswerAuthoredVerb(self." << method.Release << "(invocation), invocation.Result);\n"
            << "        if (invocation.Step == LeaseStep::Probe)\n";
        if (method.Holds.empty())
            out << "            return VerbAnswer(VerbAdmission::Refused);\n";
        else
            out << "            return self." << method.Holds
                << "(invocation.Owner) ? VerbAnswer{} : VerbAnswer(VerbAdmission::Refused);\n";
    }
    EmitDecodes(out, method,
                [](size_t index) { return "invocation.Arguments->At(" + std::to_string(index) + ")"; },
                [](const char* status, size_t index) {
                    return "VerbArgumentAnswer(VerbAdmission::" + std::string(status) + ", " + std::to_string(index)
                         + ")";
                });
    out << "        return AnswerAuthoredVerb(self." << method.Method << "(" << CallArguments(method)
        << "), invocation.Result);\n"
        << "    }\n";
}

void EmitQueryResult(std::ostream& out, const std::string& type)
{
    out << "        AuthoredQueryResultTraits<" << type << ">::Describe(definition.Result);\n";
}

void EmitQuery(std::ostream& out, const ProviderFacts& provider, const MethodFacts& method)
{
    out << "\n    static AuthoredQueryDefinition Describe_" << method.Method << "()\n    {\n"
        << "        AuthoredQueryDefinition definition;\n"
        << "        definition.Name = " << Literal(method.Identity) << ";\n";
    EmitPresentation(out, method.Label, method.Description, method.Category);
    for (const ParamFacts& param : method.Params)
        EmitArgumentSchema(out, param, "definition.Arguments");
    EmitQueryResult(out, method.ResultType);
    out << "        return definition;\n    }\n";

    out << "\n    static AuthoredQueryStatus Evaluate_" << method.Method << "(const " << provider.Type
        << "& self, std::span<const AuthoredValue>" << (method.Params.empty() ? "" : " arguments")
        << ", AuthoredValue& result)\n    {\n";
    EmitDecodes(out, method,
                [](size_t index) { return "AuthoredArgumentAt(arguments, " + std::to_string(index) + ")"; },
                [](const char*, size_t) { return std::string("AuthoredQueryStatus::InvalidArguments"); });
    out << "        return AnswerAuthoredQuery(self." << method.Method << "("
        << CallArguments(method) << "), result);\n"
        << "    }\n";
}

void EmitProvider(std::ostream& out, const ProviderFacts& provider, const std::string& logical)
{
    out << "\ntemplate <>\nstruct AuthoredApiDefinition<" << provider.Type << ">\n{\n"
        << "    using Target = " << provider.Type << ";\n\n";

    for (const MethodFacts& method : provider.Methods)
    {
        for (const ParamFacts& param : method.Params)
            EmitParamAsserts(out, method, param, logical);
        if (method.Role == MethodRole::Query)
        {
            EmitResultAsserts(out, method.ResultType, Where(logical, method.Line),
                             "SENCHA_QUERY(\"" + method.Identity + "\"): the result of '"
                                 + method.Method + "'");
        }
    }
    if (provider.ChecksTargets)
    {
        out << "    static_assert(requires(const " << provider.Type << "& provider) {\n"
            << "                      { provider.AuthoredWorld() } -> std::same_as<const World&>;\n"
            << "                  },\n"
            << "                  "
            << Literal(Where(logical, provider.TargetLine) + ": a verb of '" + provider.Type
                       + "' takes a SENCHA_TARGET, so '" + provider.Type
                       + "' answers const World& AuthoredWorld() const, which the adapter asks whether the "
                         "target is alive")
            << ");\n";
    }

    out << "\n";
    for (const MethodFacts& method : provider.Methods)
    {
        out << "    static constexpr std::string_view Name_" << method.Method << " = " << Literal(method.Identity)
            << ";\n";
    }

    size_t verbs = 0;
    size_t queries = 0;
    for (const MethodFacts& method : provider.Methods)
    {
        if (method.Dispatched())
        {
            EmitVerb(out, provider, method);
            ++verbs;
        }
        else
        {
            EmitQuery(out, provider, method);
            ++queries;
        }
    }

    if (verbs != 0)
    {
        out << "\n    static constexpr std::array<AuthoredVerbEntry<" << provider.Type << ">, "
            << verbs << "> Verbs{ {\n";
        for (const MethodFacts& method : provider.Methods)
        {
            if (method.Dispatched())
            {
                out << "        { " << Literal(method.Identity) << ", &Describe_" << method.Method
                    << ", &Invoke_" << method.Method << " },\n";
            }
        }
        out << "    } };\n";
    }
    if (queries != 0)
    {
        out << "\n    static constexpr std::array<AuthoredQueryEntry<" << provider.Type << ">, "
            << queries << "> Queries{ {\n";
        for (const MethodFacts& method : provider.Methods)
        {
            if (method.Role == MethodRole::Query)
            {
                out << "        { " << Literal(method.Identity) << ", &Describe_" << method.Method
                    << ", &Evaluate_" << method.Method << " },\n";
            }
        }
        out << "    } };\n";
    }
    out << "};\n";
}

// Data only; the reader is instantiated where the queries are bound, keeping
// component headers independent of the World.
void EmitComponentQueries(std::ostream& out, const ComponentFacts& component,
                          const std::string& logical)
{
    out << "\ntemplate <>\nstruct AuthoredApiDefinition<" << component.QualifiedType << ">\n{\n"
        << "    using Target = const World;\n\n";
    for (const FieldQueryFacts& query : component.Queries)
    {
        EmitResultAsserts(out, query.Type, Where(logical, query.Line),
                          "SENCHA_QUERY(\"" + query.Query + "\"): member '" + query.Member + "'");
    }
    for (const FieldQueryFacts& query : component.Queries)
    {
        out << "\n    static AuthoredQueryDefinition Describe_" << query.Member << "()\n    {\n"
            << "        AuthoredQueryDefinition definition;\n"
            << "        definition.Name = " << Literal(component.Identity + "." + query.Query) << ";\n";
        EmitPresentation(out, query.Label, query.Description, "");
        out << "        DescribeAuthoredComponentTarget<" << component.QualifiedType
            << ">(definition.Arguments);\n";
        EmitQueryResult(out, query.Type);
        out << "        return definition;\n    }\n";
    }
    out << "\n    static constexpr std::tuple FieldQueries{\n";
    for (const FieldQueryFacts& query : component.Queries)
    {
        out << "        AuthoredFieldQuery<&" << component.QualifiedType << "::" << query.Member
            << ">{ " << Literal(component.Identity + "." + query.Query) << ", &Describe_"
            << query.Member << " },\n";
    }
    out << "    };\n};\n";
}

void EmitMemberAsserts(std::ostream& out, const std::vector<EventFieldFacts>& members, const std::string& logical,
                       const std::string& owner)
{
    for (const EventFieldFacts& field : members)
    {
        EmitTypeAssert(out, "IsAuthoredResultType", field.Type, field.Type, Where(logical, field.Line),
                       owner + ": member '" + field.Member + "'", "result");
    }
}

void EmitMemberSchemas(std::ostream& out, const std::vector<EventFieldFacts>& members, const std::string& parent)
{
    for (const EventFieldFacts& field : members)
    {
        out << "        {\n"
            << "            DataFieldSchema member;\n"
            << "            member.Key = " << Literal(field.Name) << ";\n";
        if (!field.Label.empty())
            out << "            member.DisplayName = " << Literal(field.Label) << ";\n";
        out << "            AuthoredValueTraits<" << field.Type << ">::Describe(member);\n"
            << "            " << parent << ".Children.push_back(std::move(member));\n"
            << "        }\n";
    }
}

std::string MemberEncode(const EventFieldFacts& field, const std::string& value)
{
    return "AuthoredValueTraits<" + field.Type + ">::Encode(" + value + "." + field.Member + ")";
}

void EmitRecord(std::ostream& out, const RecordFacts& record, const std::string& logical)
{
    out << "\ntemplate <>\nstruct AuthoredValueTraits<" << record.Type << ">\n{\n";
    EmitMemberAsserts(out, record.Fields, logical, "SENCHA_RECORD '" + record.Type + "'");
    out << "\n    static void Describe(DataFieldSchema& field)\n    {\n"
        << "        field.Kind = DataFieldKind::Record;\n"
        << "        field.Children.clear();\n";
    EmitMemberSchemas(out, record.Fields, "field");
    out << "    }\n";
    out << "\n    static AuthoredValue Encode(const " << record.Type << "& value)\n    {\n"
        << "        std::vector<AuthoredValue> members;\n"
        << "        members.reserve(" << record.Fields.size() << ");\n";
    for (const EventFieldFacts& field : record.Fields)
        out << "        members.push_back(" << MemberEncode(field, "value") << ");\n";
    out << "        return AuthoredValue::Record(std::move(members));\n"
        << "    }\n};\n";
}

void EmitEvent(std::ostream& out, const EventFacts& event, const std::string& logical)
{
    out << "\ntemplate <>\nstruct AuthoredApiDefinition<" << event.Type << ">\n{\n";
    EmitMemberAsserts(out, event.Fields, logical, "SENCHA_EVENT(\"" + event.Identity + "\")");
    if (!event.Source.empty())
    {
        out << "    static_assert(AuthoredComponentType<" << event.Source << ">,\n"
            << "                  "
            << Literal(Where(logical, event.Line) + ": SENCHA_EVENT(\"" + event.Identity
                       + "\"): SENCHA_EVENT_SOURCE(" + event.Source
                       + ") names a type that is not a component")
            << ");\n";
    }
    if (!event.Fields.empty() || !event.Source.empty())
        out << "\n";
    out << "    static constexpr std::string_view EventName = " << Literal(event.Identity) << ";\n";

    out << "\n    static AuthoredEventDefinition DescribeEvent()\n    {\n"
        << "        AuthoredEventDefinition definition;\n"
        << "        definition.Name = " << Literal(event.Identity) << ";\n";
    EmitPresentation(out, event.Label, event.Description, event.Category);
    if (!event.Source.empty())
    {
        out << "        definition.SourceComponent = AuthoredComponentIdentity<" << event.Source
            << ">();\n";
    }
    EmitMemberSchemas(out, event.Fields, "definition.Payload");
    out << "        return definition;\n    }\n";

    out << "\n    static void Encode(const " << event.Type << "&"
        << (event.Fields.empty() ? "" : " event") << ", AuthoredArguments& payload)\n    {\n"
        << "        payload.Resize(" << event.Fields.size() << ");\n";
    for (size_t index = 0; index < event.Fields.size(); ++index)
        out << "        payload.Set(" << index << ", " << MemberEncode(event.Fields[index], "event") << ");\n";
    out << "    }\n};\n";
}

bool WriteCompanion(const std::string& path,
                    const std::string& logical,
                    const std::vector<ComponentFacts>& components,
                    const std::vector<ProviderFacts>& providers,
                    const std::vector<EventFacts>& events,
                    const std::vector<RecordFacts>& records)
{
    std::ofstream out(path);
    if (!out)
    {
        llvm::errs() << "sencha-component-codegen: cannot write " << path << "\n";
        return false;
    }

    const bool authored = !providers.empty() || !events.empty() || !records.empty()
        || std::any_of(components.begin(), components.end(),
                       [](const ComponentFacts& c) { return !c.Queries.empty(); });

    out << "// Generated by sencha-component-codegen from " << logical << ".\n"
        << "// Do not edit: change the annotations on the declarations instead.\n"
        << "#pragma once\n\n"
        << "#include <core/metadata/ComponentDefinition.h>\n"
        << "#include <core/metadata/Field.h>\n"
        << "#include <core/serialization/FourCC.h>\n"
        // A math member flattens through the schema traits MathSchemas
        // specializes. Seen from every includer, so no translation unit can
        // instantiate a component's fields without them.
        << "#include <math/MathSchemas.h>\n";
    if (authored)
        out << "#include <authored/AuthoredApiDefinition.h>\n";
    out << "\n"
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
        if (component.AuthorityMaterialized)
            out << "    static constexpr bool AuthorityMaterialized = true;\n";
        if (component.NonRemovable) out << "    static constexpr bool Removable = false;\n";
        if (component.Saved)        out << "    static constexpr bool Saved = true;\n";
        if (!component.VisualMesh.empty())
            out << "    static constexpr std::string_view VisualMeshAsset = " << Quoted(component.VisualMesh) << ";\n";
        if (!component.Placeable.empty())
            out << "    static constexpr std::string_view PlaceableLabel = " << Quoted(component.Placeable) << ";\n";
        if (!component.PlaceIcon.empty())
            out << "    static constexpr std::string_view PlaceableIcon = " << Quoted(component.PlaceIcon) << ";\n";

        if (!component.SchemaName.empty())
            EmitFields(out, component);
        out << "};\n";
    }

    for (const ComponentFacts& component : components)
    {
        if (!component.Queries.empty())
            EmitComponentQueries(out, component, logical);
    }
    for (const RecordFacts& record : records)
        EmitRecord(out, record, logical);
    for (const ProviderFacts& provider : providers)
        EmitProvider(out, provider, logical);
    for (const EventFacts& event : events)
        EmitEvent(out, event, logical);
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
    // Authored names are not listed: their catalogs refuse duplicates.
    for (const ComponentFacts& component : components)
    {
        out << component.Type << '\t' << component.Identity << '\t'
            << component.SchemaName << '\t' << component.SceneChunk << '\t'
            << logical << '\t' << component.Line << '\n';
    }
    return true;
}

std::string DepfileEscaped(llvm::StringRef path)
{
    std::string out;
    for (const char c : path)
    {
        if (c == ' ' || c == '#')
            out += '\\';
        if (c == '$')
            out += '$';
        out += c;
    }
    return out;
}

bool WriteDepfile(const std::string& path, const std::string& target, const clang::SourceManager& sources)
{
    std::vector<std::string> read;
    for (auto file = sources.fileinfo_begin(); file != sources.fileinfo_end(); ++file)
    {
        llvm::SmallString<256> absolute(file->first.getName());
        llvm::sys::fs::make_absolute(absolute);
        llvm::sys::path::remove_dots(absolute, true);
        read.emplace_back(absolute.str());
    }
    std::sort(read.begin(), read.end());
    read.erase(std::unique(read.begin(), read.end()), read.end());

    std::ofstream out(path);
    if (!out)
    {
        llvm::errs() << "sencha-component-codegen: cannot write " << path << "\n";
        return false;
    }
    out << DepfileEscaped(target) << ":";
    for (const std::string& file : read)
        out << " \\\n  " << DepfileEscaped(file);
    out << "\n";
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
        Ok = WriteCompanion(gOutput, gLogical, visitor.Components, visitor.Providers,
                            visitor.Events, visitor.Records)
             && WriteIndex(gIndex, gLogical, visitor.Components)
             && (gDepfile.empty() || WriteDepfile(gDepfile, gOutput, context.getSourceManager()));
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
    if (gPrintFormatVersion)
    {
        llvm::outs() << kFormatVersion << "\n";
        return 0;
    }
    if (gSource.empty() || gOutput.empty() || gIndex.empty() || gFlags.empty())
    {
        llvm::errs() << "sencha-component-codegen: <header>, --output, --index and --flags are required\n";
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
