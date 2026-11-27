#include <abilities/AbilityId.h>

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityRegistry.h>
#include <world/serialization/DefinitionVocabulary.h>

const NamedIdVocabulary& NamedIdField<AbilityId>::Vocabulary()
{
    static const NamedIdVocabulary vocabulary = MakeDefinitionVocabulary<AbilityRegistry>("ability", kAbilityDefinitionType);
    return vocabulary;
}
