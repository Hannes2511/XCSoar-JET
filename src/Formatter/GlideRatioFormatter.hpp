// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include <tchar.h>
#include <cstddef>

void
FormatGlideRatio(TCHAR *buffer, size_t size, double gr, const TCHAR *suffix = NULL);
