#pragma once

#include <cstdlib>
#include <optional>
#include <string>

// The environment is process state; a test that changes it puts it back.
class ScopedEnv
{
public:
    ScopedEnv(const char* name, const char* value)
        : Name(name)
    {
        if (const char* old = std::getenv(name))
            Previous = old;
        if (value != nullptr)
            setenv(name, value, 1);
        else
            unsetenv(name);
    }

    ~ScopedEnv()
    {
        if (Previous.has_value())
            setenv(Name, Previous->c_str(), 1);
        else
            unsetenv(Name);
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const char* Name;
    std::optional<std::string> Previous;
};
