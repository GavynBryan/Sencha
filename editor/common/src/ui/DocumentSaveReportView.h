#pragma once

#include <string>

class DocumentSourceSet;
struct DocumentSaveResult;

// What a panel says after its own Save: empty when the file was written.
[[nodiscard]] std::string DescribeDocumentSave(const DocumentSaveResult& result);

void DrawUnsavedDocuments(const DocumentSourceSet& sources);
// Problems and failures from the last save, and each conflict with Keep mine and Take the file's.
void DrawDocumentSaveReport(DocumentSourceSet& sources, std::string& settleError);
