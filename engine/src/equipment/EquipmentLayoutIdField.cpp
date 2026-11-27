#include <equipment/EquipmentLayout.h>

#include <equipment/EquipmentLayoutData.h>
#include <equipment/EquipmentLayout.h>
#include <world/serialization/DefinitionVocabulary.h>

const NamedIdVocabulary& NamedIdField<EquipmentLayoutId>::Vocabulary()
{
    static const NamedIdVocabulary vocabulary = MakeDefinitionVocabulary<EquipmentLayoutRegistry>("equipment layout", kEquipmentLayoutType);
    return vocabulary;
}
