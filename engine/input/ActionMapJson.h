#pragma once

#include <deque>
#include <string>

#include "engine/input/ActionMap.h"

namespace engine::input
{

/// Populate an ActionMap from a JSON bindings file.
/// Key and mouse button action strings are stored in ownedStrings so their
/// lifetimes outlive the ActionMap's string_views. A deque is used because
/// it does not invalidate references on push_back.
/// Returns false if the file cannot be parsed.
bool loadActionMap(const char* filepath, ActionMap& map,
                   std::deque<std::string>& ownedStrings);

/// Persist current bindings to JSON (for player rebinding).
bool saveActionMap(const ActionMap& map, const char* filepath);

}  // namespace engine::input
