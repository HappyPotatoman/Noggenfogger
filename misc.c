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

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "misc.h"
#include "thread.h"

// Version number. If Version is left empty, then compile date in the format
// DD-MM-YY and show in engine_info.
char Version[] = "";

#ifndef _WIN32
pthread_mutex_t ioMutex = PTHREAD_MUTEX_INITIALIZER;
#else
HANDLE ioMutex;
#endif

// print compiler_info() prints a string trying to describe the compiler


// xorshift64star Pseudo-Random Number Generator
// This class is based on original code written and dedicated
// to the public domain by Sebastiano Vigna (2014).
// It has the following characteristics:
//
//  -  Outputs 64-bit numbers
//  -  Passes Dieharder and SmallCrush test batteries
//  -  Does not require warm-up, no zeroland to escape
//  -  Internal state is a single 64-bit integer
//  -  Period is 2^64 - 1
//  -  Speed: 1.60 ns/call (Core i7 @3.40GHz)
//
// For further analysis see
//   <http://vigna.di.unimi.it/ftp/papers/xorshift.pdf>

void prng_init(PRNG *rng, uint64_t seed)
{
  rng->s = seed;
}

uint64_t prng_rand(PRNG *rng)
{
  uint64_t s = rng->s;

  s ^= s >> 12;
  s ^= s << 25;
  s ^= s >> 27;
  rng->s = s;

  return s * 2685821657736338717LL;
}

uint64_t prng_sparse_rand(PRNG *rng)
{
  uint64_t r1 = prng_rand(rng);
  uint64_t r2 = prng_rand(rng);
  uint64_t r3 = prng_rand(rng);
  return r1 & r2 & r3;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
  if (*n == 0)
    *lineptr = malloc(*n = 100);

  int c = 0;
  size_t i = 0;
  while ((c = getc(stream)) != EOF) {
    (*lineptr)[i++] = c;
    if (i == *n)
      *lineptr = realloc(*lineptr, *n += 100);
    if (c == '\n') break;
  }
  (*lineptr)[i] = 0;
  return i;
}

#ifdef _WIN32
typedef SIZE_T (WINAPI *GLPM)(void);
size_t largePageMinimum;

bool large_pages_supported(void)
{
  GLPM impGetLargePageMinimum =
    (GLPM)(void (*)(void))GetProcAddress(GetModuleHandle("kernel32.dll"),
        "GetLargePageMinimum");
  if (!impGetLargePageMinimum)
    return 0;

  if ((largePageMinimum = impGetLargePageMinimum()) == 0)
    return 0;

  LUID privLuid;
  if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &privLuid))
    return 0;

  HANDLE token;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token))
    return 0;

  TOKEN_PRIVILEGES tokenPrivs;
  tokenPrivs.PrivilegeCount = 1;
  tokenPrivs.Privileges[0].Luid = privLuid;
  tokenPrivs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  if (!AdjustTokenPrivileges(token, FALSE, &tokenPrivs, 0, NULL, NULL))
    return 0;

  return 1;
}

// The following two functions were taken from mingw_lock.c

void __cdecl _lock(int locknum);
void __cdecl _unlock(int locknum);
#define _STREAM_LOCKS 16
#define _IOLOCKED 0x8000
typedef struct {
  FILE f;
  CRITICAL_SECTION lock;
} _FILEX;

void flockfile(FILE *F)
{
  if ((F >= (&__iob_func()[0])) && (F <= (&__iob_func()[_IOB_ENTRIES-1]))) {
    _lock(_STREAM_LOCKS + (int)(F - (&__iob_func()[0])));
    F->_flag |= _IOLOCKED;
  } else
    EnterCriticalSection(&(((_FILEX *)F)->lock));
}

void funlockfile(FILE *F)
{
  if ((F >= (&__iob_func()[0])) && (F <= (&__iob_func()[_IOB_ENTRIES-1]))) {
    F->_flag &= ~_IOLOCKED;
    _unlock(_STREAM_LOCKS + (int)(F - (&__iob_func()[0])));
  } else
    LeaveCriticalSection(&(((_FILEX *)F)->lock));
}
#endif

FD open_file(const char *name)
{
#ifndef _WIN32
  return open(name, O_RDONLY);

#else
  return CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
      FILE_FLAG_RANDOM_ACCESS, NULL);

#endif
}

void close_file(FD fd)
{
#ifndef _WIN32
  close(fd);

#else
  CloseHandle(fd);

#endif
}

size_t file_size(FD fd)
{
#ifndef _WIN32
  struct stat statbuf;
  fstat(fd, &statbuf);
  return statbuf.st_size;

#else
  DWORD sizeLow, sizeHigh;
  sizeLow = GetFileSize(fd, &sizeHigh);
  return ((uint64_t)sizeHigh << 32) | sizeLow;

#endif
}

const void *map_file(FD fd, map_t *map)
{
#ifndef _WIN32
  *map = file_size(fd);
  void *data = mmap(NULL, *map, PROT_READ, MAP_SHARED, fd, 0);
#ifdef MADV_RANDOM
  madvise(data, *map, MADV_RANDOM);
#endif
  return data == MAP_FAILED ? NULL : data;

#else
  DWORD sizeLow, sizeHigh;
  sizeLow = GetFileSize(fd, &sizeHigh);
  *map = CreateFileMapping(fd, NULL, PAGE_READONLY, sizeHigh, sizeLow, NULL);
  if (*map == NULL)
    return NULL;
  return MapViewOfFile(*map, FILE_MAP_READ, 0, 0, 0);

#endif
}

void unmap_file(const void *data, map_t map)
{
  if (!data) return;

#ifndef _WIN32
  munmap((void *)data, map);

#else
  UnmapViewOfFile(data);
  CloseHandle(map);

#endif
}

void *allocate_memory(size_t size, bool lp, alloc_t *alloc)
{
  void *ptr = NULL;

#ifdef _WIN32
  if (lp) {
    size_t pageSize = largePageMinimum;
    size_t lpSize = (size + pageSize - 1) & ~(pageSize - 1);
    ptr = VirtualAlloc(NULL, lpSize,
        MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE);
  } else
    ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  alloc->ptr = ptr;
  return ptr;

#else /* Unix */
  size_t alignment = lp ? 1ULL << 21 : 1;
  size_t allocSize = size + alignment - 1;

#if defined(__APPLE__) && defined(VM_FLAGS_SUPERPAGE_SIZE_2MB)
  if (lp)
    ptr = mmap(NULL, allocSize, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, VM_FLAGS_SUPERPAGE_SIZE_2MB, 0);
  else
    ptr = mmap(NULL, allocSize, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

#else
  ptr = mmap(NULL, allocSize, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#if defined(__linux__) && defined(MADV_HUGEPAGE)
  // Advise the kernel to allocate large pages.
  if (lp)
    madvise(ptr, allocSize, MADV_HUGEPAGE);
#endif

#endif

  alloc->ptr = ptr;
  alloc->size = allocSize;
  return (void *)(((uintptr_t)ptr + alignment - 1) & ~(alignment - 1));

#endif
}

void free_memory(alloc_t *alloc)
{
#ifdef _WIN32
  VirtualFree(alloc->ptr, 0, MEM_RELEASE);
#else
  munmap(alloc->ptr, alloc->size);
#endif
}

const uint8_t piece_to_index[16] = {
    UINT8_MAX,  // 0: Invalid
    0, 1, 2, 3, 4, 5,  // 1–6: White pieces (mapped to 0–5)
    UINT8_MAX, UINT8_MAX,  // 7–8: Unused
    6, 7, 8, 9, 10, 11,  // 9–14: Black pieces (mapped to 6–11)
    UINT8_MAX   // 15: Unused
};