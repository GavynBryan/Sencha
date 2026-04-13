#pragma once

#include <core/service/IService.h>
#include <core/logging/LoggingProvider.h>

class SdlVideoService : public IService
{
public:
    explicit SdlVideoService(LoggingProvider& logging);
    ~SdlVideoService() override;

    SdlVideoService(const SdlVideoService&) = delete;
    SdlVideoService& operator=(const SdlVideoService&) = delete;
    SdlVideoService(SdlVideoService&&) = delete;
    SdlVideoService& operator=(SdlVideoService&&) = delete;

    [[nodiscard]] bool IsValid() const { return Initialized; }
    [[nodiscard]] bool OwnsSubsystem() const { return OwnsVideoSubsystem; }

private:
    Logger& Log;
    bool Initialized = false;
    bool OwnsVideoSubsystem = false;
};
