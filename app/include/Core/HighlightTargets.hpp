#pragma once

#include <vector>

class BaseCube;
class Instance;

// Collect the BaseCube instances targeted by a game Highlight or editor
// selection. A BaseCube parent targets only itself; a Model parent targets all
// descendant BaseCubes recursively, including nested Models.
void collectHighlightTargets(Instance* parent, std::vector<BaseCube*>& out);
