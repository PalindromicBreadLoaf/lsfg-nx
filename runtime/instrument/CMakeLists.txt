# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(lsfg_instrument STATIC src/presentation.cpp)
add_library(lsfg::instrument ALIAS lsfg_instrument)
target_include_directories(lsfg_instrument PUBLIC include)
target_link_libraries(lsfg_instrument PUBLIC lsfg::error lsfg_project_options)
