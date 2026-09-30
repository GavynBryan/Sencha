#pragma once

#include "data/DataDocumentStore.h"
#include "documents/DocumentSourceSet.h"

struct RuntimeAssets;

// The journal and the data documents a Kyusu session holds, for a test that
// builds a workspace over them. Declared before the workspace, so it outlives it.
struct EditorDocuments
{
    explicit EditorDocuments(RuntimeAssets& assets)
        : Store(assets, Sources)
    {
    }

    DocumentSourceSet Sources;
    DataDocumentStore Store;
};
