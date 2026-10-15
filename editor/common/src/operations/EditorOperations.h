#pragma once

#include "EditorOperationCatalog.h"

#include <core/json/JsonValue.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class ConsoleRegistry;

enum class EditorOperationStatus : std::uint8_t
{
    Ok,
    // Understood and declined; Code names why, for a client to act on.
    Refused,
    // The request or the operation was malformed.
    Error,
};

struct EditorOperationOutcome
{
    EditorOperationStatus Status = EditorOperationStatus::Ok;
    std::string Code;
    std::string Message;
    JsonValue Data;
    // With a refusal, what a client needs to retry, e.g. the candidates of an
    // ambiguous name.
    JsonValue Detail;

    [[nodiscard]] static EditorOperationOutcome Ok(JsonValue data);
    [[nodiscard]] static EditorOperationOutcome Refused(std::string code, std::string message,
                                                        JsonValue detail = {});
};

// Receives the arguments already checked against the operation's parameters,
// with every default filled.
using EditorOperationExecutor = std::function<EditorOperationOutcome(const JsonValue& arguments)>;

struct EditorOperationEntry
{
    EditorOperationDefinition Definition;
    EditorOperationExecutor Execute;
};

// The operation catalog and the executors bound to it. Every result, whatever
// went wrong, is one JSON object:
//   { operation, status: "ok" | "refused" | "error", code?, message?,
//     problems?: [{path, message}], detail?, data? }
class EditorOperations
{
public:
    // All or nothing: false registers none of them and appends why.
    [[nodiscard]] bool Register(std::string provider,
                                std::vector<EditorOperationEntry> entries,
                                std::vector<std::string>& errors);

    // A retired operation answers as unknown until its provider registers again.
    void Retire(std::string_view provider);

    [[nodiscard]] JsonValue Invoke(std::string_view name, const JsonValue& arguments);

    [[nodiscard]] const EditorOperationCatalog& Catalog() const { return Catalog_; }

    // From now on every live operation is also a console command of its name,
    // registered and retired with its provider. The console must outlive this.
    void ExposeTo(ConsoleRegistry& console);

    // What happened that a client may want to know without asking: an event is
    // one JSON object naming itself in "event" (a selection, a save, a draft
    // accepted). Every listener hears every event, in the order emitted.
    using EventListener = std::function<void(const JsonValue& event)>;
    [[nodiscard]] std::uint64_t Listen(EventListener listener);
    void StopListening(std::uint64_t listener);
    void Emit(const JsonValue& event);

private:
    void RegisterCommand(EditorOperationId id);

    EditorOperationCatalog Catalog_;
    // Indexed by catalog slot.
    std::vector<EditorOperationExecutor> Executors;
    ConsoleRegistry* Console = nullptr;
    std::vector<std::pair<std::uint64_t, EventListener>> Listeners;
    std::uint64_t NextListener = 1;
};
