// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdio>

namespace kip_target {
bool IsCommittedSeparatedBoot(std::size_t cust_size);
FILE *Open(const char *mode, std::size_t cust_size);
const char *Path();
bool IsSeparated();
bool UseGuard();
bool ReadTable(FILE *file, void *table, std::size_t size);
bool WriteTable(FILE *file, const void *table, std::size_t size);
}
