#pragma once
#include "offsets.h"

namespace config {
    void load();
    // Applies a newly written offsets.ini (mtime/size changed). Values that are
    // missing or fail sanity checks keep their current value.
    bool reloadIfChanged();
}
