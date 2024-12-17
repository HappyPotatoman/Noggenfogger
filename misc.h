/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MISC_H
#define MISC_H

#include <assert.h>
#include <stdio.h>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <stdatomic.h>
#include <sys/time.h>
#include <unistd.h>

#include "types.h"

void print_engine_info(bool to_uci);

// prefetch() preloads the given address in L1/L2 cache. This is
// a non-blocking function that doesn't stall the CPU waiting for data
// to be loaded from memory, which can be quite slow.

INLINE void prefetch(void *addr)
{
#ifndef NO_PREFETCH

#if defined(__INTEL_COMPILER)
// This hack prevents prefetches from being optimized away by
// Intel compiler. Both MSVC and gcc seem not be affected by this.
  __asm__ ("");
#endif

#if defined(__INTEL_COMPILER) || defined(_MSC_VER)
  _mm_prefetch((char *)addr, _MM_HINT_T0);
#else
  __builtin_prefetch(addr);
#endif
#else
  (void)addr;
#endif
}

INLINE void prefetch2(void *addr)
{
  prefetch(addr);
  prefetch((uint8_t *)addr + 64);
}

typedef int64_t TimePoint; // A value in milliseconds

INLINE TimePoint now(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return 1000 * (uint64_t)tv.tv_sec + (uint64_t)tv.tv_usec / 1000;
}

#ifdef _WIN32
bool large_pages_supported(void);
extern size_t largePageMinimum;

typedef HANDLE FD;
#define FD_ERR INVALID_HANDLE_VALUE
typedef HANDLE map_t;
typedef struct {
  void *ptr;
} alloc_t;

void flockfile(FILE *F);
void funlockfile(FILE *F);

#else /* Unix */

typedef int FD;
#define FD_ERR -1
typedef size_t map_t;
typedef struct {
  void *ptr;
  size_t size;
} alloc_t;

#endif

ssize_t getline(char **lineptr, size_t *n, FILE *stream);

FD open_file(const char *name);
void close_file(FD fd);
size_t file_size(FD fd);
const void *map_file(FD fd, map_t *map);
void unmap_file(const void *data, map_t map);
void *allocate_memory(size_t size, bool lp, alloc_t *alloc);
void free_memory(alloc_t *alloc);

struct PRNG
{
  uint64_t s;
};

typedef struct PRNG PRNG;

void prng_init(PRNG *rng, uint64_t seed);
uint64_t prng_rand(PRNG *rng);
uint64_t prng_sparse_rand(PRNG *rng);

INLINE uint64_t mul_hi64(uint64_t a, uint64_t b)
{
#if defined(__GNUC__) && defined(IS_64BIT)
  __extension__ typedef unsigned __int128 uint128;
  return ((uint128)a * (uint128)b) >> 64;
#else
  uint64_t aL = (uint32_t)a, aH = a >> 32;
  uint64_t bL = (uint32_t)b, bH = b >> 32;
  uint64_t c1 = (aL * bL) >> 32;
  uint64_t c2 = aH * bL + c1;
  uint64_t c3 = aL * bH + (uint32_t)c2;
  return aH * bH + (c2 >> 32) + (c3 >> 32);
#endif
}

INLINE bool is_little_endian(void)
{
  int num = 1;
  return *(uint8_t *)&num == 1;
}

INLINE uint32_t from_le_u32(uint32_t v)
{
  return is_little_endian() ? v : __builtin_bswap32(v);
}

INLINE uint16_t from_le_u16(uint16_t v)
{
  return is_little_endian() ? v : __builtin_bswap16(v);
}

INLINE uint64_t from_be_u64(uint64_t v)
{
  return is_little_endian() ? __builtin_bswap64(v) : v;
}

INLINE uint32_t from_be_u32(uint32_t v)
{
  return is_little_endian() ? __builtin_bswap32(v) : v;
}

INLINE uint16_t from_be_u16(uint16_t v)
{
  return is_little_endian() ? __builtin_bswap16(v) : v;
}

INLINE uint32_t read_le_u32(const void *p)
{
  return from_le_u32(*(uint32_t *)p);
}

INLINE uint16_t read_le_u16(const void *p)
{
  return from_le_u16(*(uint16_t *)p);
}

INLINE uint32_t readu_le_u32(const void *p)
{
  const uint8_t *q = p;
  return q[0] | (q[1] << 8) | (q[2] << 16) | (q[3] << 24);
}

INLINE uint16_t readu_le_u16(const void *p)
{
  const uint8_t *q = p;
  return q[0] | (q[1] << 8);
}

#endif
