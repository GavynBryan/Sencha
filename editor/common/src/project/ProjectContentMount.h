#pragma once

#include <string_view>

class AssetImporterRegistry;
class JobSystem;
class LoggingProvider;
struct ProjectDescriptor;
struct ProjectFileChange;
struct RuntimeAssets;

// Mounts every content root of the project into the asset system: authored
// scan, on-demand import (.png to .stex), then the .cooked overlay. This is the
// same resolution the runtime uses, so a ref an editor resolves is the one the
// cook stamps and the runtime loads.
// `jobs` accelerates any texture cooks the mount performs; null cooks serial.
void MountProjectContent(const ProjectDescriptor& project,
                         RuntimeAssets& assets,
                         LoggingProvider& logging,
                         JobSystem* jobs = nullptr);

// Mounted after the project's roots, as at runtime, so project content claiming
// the same path wins. A no-op when the engine has no content root.
void MountEngineContent(RuntimeAssets& assets, LoggingProvider& logging, JobSystem* jobs = nullptr);

// Mounts the editor's own authored UI as a content root: its documents,
// stylesheets and fonts, cooked and registered the same way a project's content
// is. Separate from the project's roots because it ships with the editor and is
// there whether or not a project is open.
void MountEditorContent(std::string_view root,
                        RuntimeAssets& assets,
                        LoggingProvider& logging,
                        JobSystem* jobs);

// One file a step moved, copied or trashed, mounted as the mount would have
// found it: a file gone loses its record and whatever the cook made of it; a
// file appeared is registered, and imported when an importer claims it.
void MountProjectFileChange(const ProjectFileChange& change,
                            RuntimeAssets& assets,
                            const AssetImporterRegistry& importers,
                            LoggingProvider& logging);
