/*
 * Copyright (c) 2022, Tobias <git@tsmr.eu>
 * 
 */

#pragma once

#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef __linux__
typedef u_int8_t uint8_t;
#endif

namespace Hash {

void sha384 (uint8_t *input, uint8_t *output, size_t length);

} // namespace Hash
