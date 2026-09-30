#pragma once

class DataDocumentSet;
class DocumentSourceSet;
class PromptQueue;
struct WorkspaceView;

void AddAnimationDocumentPanels(WorkspaceView& ui, DataDocumentSet& documents, DocumentSourceSet& sources,
                                PromptQueue& prompts);
