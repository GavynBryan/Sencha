#include <assets/cook/ContentImporters.h>

ContentImporterSet::ContentImporterSet(JobSystem* jobs)
    : Texture(jobs)
{
    // Registration refuses an extension another importer already claims, so a
    // collision is caught here rather than resolving to whichever registered
    // first.
    (void)Importers.Register(Texture);
    (void)Importers.Register(Gltf);
    (void)Importers.Register(Blend);
    (void)Importers.Register(Font);
    (void)Importers.Register(UiPackage);
}
