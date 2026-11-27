#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbResult.h>
#include <save/SavedGames.h>

#include <string_view>

class ConsoleService;
class Engine;

class SavedGameOperations
{
public:
    explicit SavedGameOperations(SavedGames& saves)
        : Saves(&saves)
    {
    }

    SENCHA_VERB("save.write")
    SENCHA_LABEL("Save")
    SENCHA_CATEGORY("Saves")
    SENCHA_DESCRIPTION("Writes how the world differs from its authored content.")
    VerbRefusal<SaveAdmission> Write(
        SENCHA_ARG("slot") SENCHA_LABEL("Slot") SENCHA_DESCRIPTION("Letters, digits, '-' and '_'; the default slot when empty.")
            std::string_view slot = "",
        SENCHA_ARG("arrival") SENCHA_LABEL("Arrival") SENCHA_DESCRIPTION("Where the saved body comes back; written with the save.")
            std::string_view arrival = "");

    SENCHA_VERB("save.restore")
    SENCHA_LABEL("Restore")
    SENCHA_CATEGORY("Saves")
    SENCHA_DESCRIPTION("Loads the saved level again and puts back what the save kept.")
    VerbRefusal<SaveAdmission> Restore(
        SENCHA_ARG("slot") SENCHA_LABEL("Slot") SENCHA_DESCRIPTION("Letters, digits, '-' and '_'; the default slot when empty.")
            std::string_view slot = "");

    SENCHA_VERB("save.delete")
    SENCHA_LABEL("Delete save")
    SENCHA_CATEGORY("Saves")
    SENCHA_DESCRIPTION("Deletes a slot's save and its backup.")
    VerbRefusal<SaveAdmission> Delete(
        SENCHA_ARG("slot") SENCHA_LABEL("Slot") SENCHA_DESCRIPTION("Letters, digits, '-' and '_'.") std::string_view slot);

    SENCHA_VERB("save.copy")
    SENCHA_LABEL("Copy save")
    SENCHA_CATEGORY("Saves")
    SENCHA_DESCRIPTION("Writes one slot with what another holds, its backup included.")
    VerbRefusal<SaveAdmission> Copy(SENCHA_ARG("from") SENCHA_LABEL("From") SENCHA_DESCRIPTION("The slot copied.")
                                        std::string_view from,
                                    SENCHA_ARG("to") SENCHA_LABEL("To") SENCHA_DESCRIPTION("The slot written.")
                                        std::string_view to);

private:
    SavedGames* Saves;
};

void RegisterSavedGameCommands(ConsoleService& console, Engine& engine);

#if !defined(SENCHA_CODEGEN)
#  include <save/SavedGameVocabulary.sencha.h>
#endif
