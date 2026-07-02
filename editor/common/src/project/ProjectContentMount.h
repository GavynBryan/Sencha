#pragma once

class LoggingProvider;
struct ProjectDescriptor;
struct RuntimeAssets;

// Mounts every content root of the project into the asset system: authored
// scan, .cooked overlay, on-demand texture cook (.png to .stex), and the
// root's asset id map. This is the same resolution the runtime uses, so a ref
// an editor resolves is the one the cook stamps and the runtime loads.
void MountProjectContent(const ProjectDescriptor& project,
                         RuntimeAssets& assets,
                         LoggingProvider& logging);
