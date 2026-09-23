#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <core/metadata/EnumSchema.h>
#include <ecs/EntityId.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

// Golden input; expected output is GoldenAuthoredApi.sencha.h.expected.

enum class GoldenLamp : std::uint8_t
{
    Off,
    Dim,
    Bright,
};

template<>
struct EnumSchema<GoldenLamp>
{
    static constexpr std::array Values = {
        EnumValue{ GoldenLamp::Off, "off", "Off" },
        EnumValue{ GoldenLamp::Dim, "dim", "Dim" },
        EnumValue{ GoldenLamp::Bright, "bright", "Bright" },
    };
};

struct SENCHA_COMPONENT("test.codegen.golden_torch")
       SENCHA_SCHEMA("GoldenTorch")
GoldenTorch
{
    SENCHA_FIELD("lit")
    SENCHA_QUERY("lit")
    SENCHA_LABEL("Lit")
    bool Lit = false;

    SENCHA_FIELD("fuel")
    float Fuel = 1.0f;

    // Queryable, not serialized.
    SENCHA_QUERY("hot")
    SENCHA_DESCRIPTION("Whether the flame is hot enough to burn.")
    bool Hot = false;

    // Neither.
    float Timer = 0.0f;
};

class GoldenTorchSystem
{
public:
    SENCHA_VERB("test.torch.ignite")
    SENCHA_LABEL("Ignite")
    SENCHA_DESCRIPTION("Lights the torch.")
    SENCHA_CATEGORY("Test")
    VerbAdmission Ignite(const VerbInvocation& invocation,
                         SENCHA_TARGET("torch", GoldenTorch) EntityId torch,
                         SENCHA_ARG("brightness") GoldenLamp brightness = GoldenLamp::Bright,
                         SENCHA_ARG("seconds") SENCHA_RANGE(0.5, 60) float seconds = 5.0f,
                         SENCHA_ARG("instigator") std::optional<EntityId> instigator = std::nullopt);

    SENCHA_VERB("test.torch.extinguish_all")
    VerbAdmission ExtinguishAll();

    SENCHA_VERB("test.torch.rename")
    VerbAdmission Rename(SENCHA_TARGET("torch") EntityId torch,
                         SENCHA_ARG("name") SENCHA_LABEL("Name") const std::string& name = "torch");

    SENCHA_QUERY("test.torch.can_ignite")
    SENCHA_LABEL("Can ignite")
    bool CanIgnite(SENCHA_TARGET("torch", GoldenTorch) EntityId torch,
                   SENCHA_ARG("level") SENCHA_RANGE(0, 10) std::int32_t level) const;

    SENCHA_QUERY("test.torch.burn_time")
    std::optional<double> BurnTime(SENCHA_TARGET("torch", GoldenTorch) EntityId torch) const;

    void Tick();
};

struct SENCHA_EVENT("test.torch.lit")
       SENCHA_EVENT_SOURCE(GoldenTorch)
       SENCHA_LABEL("Lit")
GoldenTorchLitEvent
{
    SENCHA_FIELD("instigator")
    EntityId Instigator{};

    SENCHA_FIELD("brightness")
    SENCHA_LABEL("Brightness")
    GoldenLamp Brightness = GoldenLamp::Bright;

    int Scratch = 0;
};

struct SENCHA_EVENT("test.torch.extinguished")
GoldenTorchExtinguishedEvent
{
};
