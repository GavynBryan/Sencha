#pragma once

#include <core/handle/Handle.h>

// One window the renderer presents to. Generational, so an id kept past its
// presentation's destruction never names the one created after it.
using PresentationId = Handle<struct PresentationIdTag>;
