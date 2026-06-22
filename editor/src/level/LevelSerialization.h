#pragma once

// Registers the scene serializers used by editor level documents. Idempotent;
// call once at editor startup before any save or load.
void RegisterLevelSerializers();
