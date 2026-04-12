#pragma once

#include <service/IService.h>
#include <logging/Logger.h>

class SdlVideoService : public IService
{
public:
    explicit SdlVideoService(Logger& logger);
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
