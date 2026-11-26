#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredValueTraits.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbResult.h>
#include <core/metadata/EnumSchema.h>
#include <ecs/EntityId.h>
#include <ecs/LeaseOwner.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class World;

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

enum class GoldenRefusal : std::uint8_t
{
    None,
    Wet,
    Spent,
};

template<>
struct EnumSchema<GoldenRefusal>
{
    static constexpr std::array Values = {
        EnumValue{ GoldenRefusal::None, "none", "None" },
        EnumValue{ GoldenRefusal::Wet, "wet", "Wet" },
        EnumValue{ GoldenRefusal::Spent, "spent", "Spent" },
    };
};

struct SENCHA_RECORD GoldenFlame
{
    SENCHA_FIELD("height")
    float Height = 0.0f;

    SENCHA_FIELD("lamp")
    SENCHA_LABEL("Lamp")
    GoldenLamp Lamp = GoldenLamp::Off;

    int Flicker = 0;
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

    SENCHA_VERB("test.torch.light")
    SENCHA_RESULT("flame", GoldenTorch)
    VerbResult<EntityId, GoldenRefusal> Light(
        SENCHA_TARGET("torch", GoldenTorch) EntityId torch,
        SENCHA_ARG("fuel") SENCHA_MIN(0) SENCHA_UNITS("seconds") SENCHA_DESCRIPTION("How long it burns.")
            float fuel = 10.0f);

    SENCHA_VERB("test.torch.douse")
    SENCHA_PREDICTION(Simulation)
    VerbRefusal<GoldenRefusal> Douse(SENCHA_TARGET("torch") std::optional<EntityId> torch);

    SENCHA_LEASE("test.torch.hold")
    SENCHA_PREDICTION(Simulation)
    SENCHA_COMPLETES
    VerbAnswer Hold(const VerbInvocation& invocation,
                    SENCHA_TARGET("torch", GoldenTorch) EntityId torch,
                    SENCHA_ARG("seconds") SENCHA_MIN(0) SENCHA_MAX(30) float seconds = 1.0f);
    SENCHA_LEASE_RELEASE("test.torch.hold")
    VerbAnswer EndHold(const VerbInvocation& invocation);
    SENCHA_LEASE_HOLDS("test.torch.hold")
    bool HoldGoesOn(const LeaseOwner& owner) const;

    SENCHA_LEASE("test.torch.flicker")
    SENCHA_COSMETIC
    VerbAdmission Flicker(SENCHA_ARG("rate") SENCHA_MAX(8) std::int32_t rate);
    SENCHA_LEASE_RELEASE("test.torch.flicker")
    VerbAdmission EndFlicker(const VerbInvocation& invocation);

    SENCHA_VERB("test.torch.glint")
    SENCHA_COSMETIC
    VerbAdmission Glint(SENCHA_ARG("mesh") SENCHA_ASSET(StaticMesh) const AssetRef& mesh,
                        SENCHA_ARG("note") std::string_view note = "glint");

    SENCHA_VERB("test.torch.fuel_with")
    VerbAdmission FuelWith(SENCHA_ARG("fuel") SENCHA_DATA_ASSET("test.fuel") AuthoredDataRef fuel);

    SENCHA_QUERY("test.torch.flame")
    std::optional<GoldenFlame> Flame(SENCHA_TARGET("torch", GoldenTorch) EntityId torch) const;

    SENCHA_QUERY("test.torch.lit")
    std::vector<EntityId> LitTorches() const;

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

    void Tick();

    const World* Entities = nullptr;
};

class GoldenBenchVerbs
{
public:
    SENCHA_VERB("test.bench.touch")
    VerbAdmission Touch(SENCHA_TARGET("target") EntityId target)
    {
        Sum += target.Index;
        return VerbAdmission::Accepted;
    }

    SENCHA_VERB("test.bench.paint")
    VerbAdmission Paint(SENCHA_TARGET("target") EntityId target,
                        SENCHA_ARG("label") std::string_view label,
                        SENCHA_ARG("decal") SENCHA_ASSET(Texture) const AssetRef& decal,
                        SENCHA_ARG("scale") float scale,
                        SENCHA_ARG("layer") std::int32_t layer)
    {
        Sum += target.Index + label.size() + static_cast<std::uint64_t>(decal.IsValid())
             + static_cast<std::uint64_t>(scale) + static_cast<std::uint64_t>(layer);
        return VerbAdmission::Accepted;
    }

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

    const World* Entities = nullptr;
    std::uint64_t Sum = 0;
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
