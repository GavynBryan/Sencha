#pragma once

#include <string>

class DocumentSourceSet;

void DrawUnsavedDocuments(const DocumentSourceSet& sources);
// Problems and failures from the last save, and each conflict with Keep mine and Take the file's.
void DrawDocumentSaveReport(DocumentSourceSet& sources, std::string& settleError);
