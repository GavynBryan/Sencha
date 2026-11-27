#include <inventory/ItemDefinition.h>

#include <inventory/ItemDefinitionData.h>
#include <inventory/ItemDefinition.h>
#include <world/serialization/DefinitionVocabulary.h>

const NamedIdVocabulary& NamedIdField<ItemId>::Vocabulary()
{
    static const NamedIdVocabulary vocabulary = MakeDefinitionVocabulary<ItemRegistry>("item", kItemDefinitionType);
    return vocabulary;
}
