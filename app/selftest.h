#pragma once

#include <string>

// Drives the app through the same actions its menus and windows call,
// checks the project after each step and saves a screenshot of each into
// `out_dir`. Media comes from `fixtures` (tests/fixtures). Returns 0 when
// every check passes. Needs a display.
int run_selftest(const std::string& out_dir, const std::string& fixtures);
