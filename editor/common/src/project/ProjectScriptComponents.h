#pragma once

class AssetRegistry;
class LoggingProvider;

// Parses each cooked script (.tbc) the registry knows and registers a
// serializer for every component it declares, so script-defined components
// appear in the editor inspector and round-trip through scene save/load. Call
// after MountProjectContent (which lists cooked scripts) and before the
// document's World registers component storage. Scripts added later are picked
// up on the next project load, since the serializer registry is process-global
// and built once.
void RegisterProjectScriptComponents(const AssetRegistry& registry, LoggingProvider& logging);
