#include <authored/AuthoredQueryBinding.h>
#include <format>

bool CompileAuthoredQueryBinding(const AuthoredQueryBindingDesc &desc, const AuthoredQueryRegistry &queries,
                                 const VerbBindingEnvironment &environment, CompiledAuthoredQueryBinding &out,
                                 std::vector<std::string> &errors)
{
    CompiledAuthoredQueryBinding compiled;
    compiled.Query = queries.Resolve(desc.QueryName);
    if (!compiled.Query.IsValid())
    {
        errors.push_back(std::format("binding '{}': '{}' is not a registered Query", desc.Key, desc.QueryName));
        return false;
    }
    if (!CompileAuthoredArguments(desc.Key, queries.Get(compiled.Query.Slot)->Arguments, desc.Inputs, desc.Arguments,
                                  environment, compiled.Constants, compiled.Inputs, compiled.ReferencesChecked, errors))
        return false;
    out = std::move(compiled);
    return true;
}
