#pragma once

#include "DocumentCook.h"
#include "DocumentCookRequest.h"
#include "DocumentCookSnapshot.h"

// The immutable envelope's payload: resolved policy plus captured document
// state. Collection fills both on the owner thread; execution consumes only
// this, never the live document.
struct DocumentCookInput::Data
{
    DocumentCookRequest Request;
    DocumentCookSnapshot Snapshot;
};
