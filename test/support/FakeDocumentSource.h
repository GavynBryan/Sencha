#pragma once

#include "documents/DocumentSourceSet.h"

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A document source over plain flags: what each document saves as, whether a
// settle fails, and a log of what the journal asked of it.
struct FakeDocument
{
    bool Changed = false;
    bool Editing = false;
    DocumentSaveStatus SaveAs = DocumentSaveStatus::Saved;
    std::string SettleError = {};
};

class FakeDocumentSource final : public DocumentSource
{
public:
    FakeDocumentSource(std::string name, DocumentSourceSet& set, std::vector<std::string>& log)
        : Name(std::move(name)), Set(set), Log(log)
    {
        Set.AddSource(*this);
    }
    ~FakeDocumentSource() override { Set.RemoveSource(*this); }

    void Edit(const std::string& key)
    {
        Documents[key].Changed = true;
        Set.Record({ this, key });
    }

    void AppendChangedDocuments(std::vector<DocumentRef>& out) override
    {
        for (const auto& [key, document] : Documents)
            if (document.Changed || document.Editing)
                out.push_back({ this, key });
    }

    DocumentSaveResult SaveDocument(std::string_view key) override
    {
        FakeDocument& document = Documents[std::string(key)];
        document.Editing = false;
        if (document.SaveAs == DocumentSaveStatus::Saved)
            document.Changed = false;
        return { {}, document.SaveAs, document.SaveAs == DocumentSaveStatus::Failed ? "disk full" : "" };
    }

    bool SettleDocument(std::string_view key, ConflictChoice choice, std::string& error) override
    {
        Log.push_back(Name + ":" + std::string(key) + (choice == ConflictChoice::KeepMine ? ":mine" : ":file"));
        FakeDocument& document = Documents[std::string(key)];
        if (!document.SettleError.empty())
        {
            error = document.SettleError;
            return false;
        }
        document = {};
        return true;
    }

    void StepDocument(std::string_view key, DocumentStep step) override
    {
        Log.push_back(Name + ":" + std::string(key) + (step == DocumentStep::Undo ? ":undo" : ":redo"));
        if (RecordWhileStepping)
            Set.Record({ this, std::string(key) });
    }

    void CancelDocumentEdits() override
    {
        for (auto& [key, document] : Documents)
            if (std::exchange(document.Editing, false))
                Log.push_back(Name + ":" + key + ":cancel");
    }

    void DiscardDocument(std::string_view key) override
    {
        Log.push_back(Name + ":" + std::string(key) + ":discard");
        Documents.erase(std::string(key));
        Set.ForgetDocument({ this, std::string(key) });
    }

    std::string Name;
    DocumentSourceSet& Set;
    std::vector<std::string>& Log;
    std::map<std::string, FakeDocument> Documents;
    bool RecordWhileStepping = false;
};
