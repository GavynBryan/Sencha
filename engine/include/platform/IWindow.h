#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <platform/WindowTypes.h>

class IWindow
{
public:
    virtual ~IWindow() = default;

    [[nodiscard]] virtual bool IsValid() const = 0;
    virtual void Close() = 0;

    [[nodiscard]] virtual std::string GetTitle() const = 0;
    virtual void SetTitle(std::string_view title) = 0;

    [[nodiscard]] virtual WindowExtent GetExtent() const = 0;
    virtual void SetSize(uint32_t width, uint32_t height) = 0;

    [[nodiscard]] virtual bool IsResizable() const = 0;
    virtual void SetResizable(bool resizable) = 0;

    [[nodiscard]] virtual WindowMode GetMode() const = 0;
    virtual void SetMode(WindowMode mode) = 0;

    virtual void Show() = 0;
    virtual void Hide() = 0;
};
