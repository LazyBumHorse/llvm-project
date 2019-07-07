//===-- tysan_interceptors.cc ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of TypeSanitizer.
//
// Interceptors for standard library functions.
//===----------------------------------------------------------------------===//

#include "tysan/tysan.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_common.h"

#if SANITIZER_LINUX && !SANITIZER_ANDROID
# define TYSAN_INTERCEPT___STRDUP 1
#else
# define TYSAN_INTERCEPT___STRDUP 0
#endif

#if SANITIZER_LINUX
extern "C"
int mallopt(int param, int value);
#endif

using namespace __sanitizer;
using namespace __tysan;

static const uptr early_alloc_buf_size = 16384;
static uptr allocated_bytes;
static char early_alloc_buf[early_alloc_buf_size];

static bool isInEarlyAllocBuf(const void *ptr) {
  return ((uptr)ptr >= (uptr)early_alloc_buf &&
          ((uptr)ptr - (uptr)early_alloc_buf) < sizeof(early_alloc_buf));
}

// Handle allocation requests early (before all interceptors are setup). dlsym,
// for example, calls calloc.
static void *handleEarlyAlloc(uptr size) {
  void *mem = (void *)&early_alloc_buf[allocated_bytes];
  allocated_bytes += size;
  CHECK_LT(allocated_bytes, early_alloc_buf_size);
  return mem;
}

INTERCEPTOR(void *, memset, void *dst, int v, uptr size) {
  if (!tysan_inited && REAL(memset) == nullptr)
    return internal_memset(dst, v, size);

  void *res = REAL(memset)(dst, v, size);
  tysan_set_type_unknown(dst, size);
  return res;
}

INTERCEPTOR(void *, memmove, void *dst, const void *src, uptr size) {
  if (!tysan_inited && REAL(memmove) == nullptr)
    return internal_memmove(dst, src, size);

  void *res = REAL(memmove)(dst, src, size);
  tysan_copy_types(dst, src, size);
  return res;
}

INTERCEPTOR(void *, memcpy, void *dst, const void *src, uptr size) {
  if (!tysan_inited && REAL(memcpy) == nullptr) {
      // memmove is used here because on some platforms this will also
      // intercept the memmove implementation.
      return internal_memmove(dst, src, size);
  }

  void *res = REAL(memcpy)(dst, src, size);
  tysan_copy_types(dst, src, size);
  return res;
}

INTERCEPTOR(void *, mmap, void *addr, SIZE_T length, int prot, int flags,
            int fd, OFF_T offset) {
  void *res = REAL(mmap)(addr, length, prot, flags, fd, offset);
  if (res != (void*)-1)
    tysan_set_type_unknown(res, RoundUpTo(length, GetPageSize()));
  return res;
}

INTERCEPTOR(void *, mmap64, void *addr, SIZE_T length, int prot, int flags,
            int fd, OFF64_T offset) {
  void *res = REAL(mmap64)(addr, length, prot, flags, fd, offset);
  if (res != (void*)-1)
    tysan_set_type_unknown(res, RoundUpTo(length, GetPageSize()));
  return res;
}

INTERCEPTOR(char*, strdup, const char *s) {
  char *res = REAL(strdup)(s);
  if (res)
    tysan_copy_types(res, const_cast<char *>(s), internal_strlen(s));
  return res; 
}

#if TYSAN_INTERCEPT___STRDUP
INTERCEPTOR(char*, __strdup, const char *s) {
  char *res = REAL(__strdup)(s);
  if (res)
    tysan_copy_types(res, const_cast<char *>(s), internal_strlen(s));
  return res; 
}
#endif // TYSAN_INTERCEPT___STRDUP

INTERCEPTOR(void*, malloc, uptr size) {
  if (tysan_init_is_running && REAL(malloc) == nullptr)
    return handleEarlyAlloc(size);

  void *res = REAL(malloc)(size);
  if (res)
    tysan_set_type_unknown(res, size);
  return res;
}

INTERCEPTOR(void*, realloc, void *ptr, uptr size) {
  void *res = REAL(realloc)(ptr, size);
  // We might want to copy the types from the original allocation (although
  // that would require that we knew its size).
  if (res)
    tysan_set_type_unknown(res, size);
  return res;
}

INTERCEPTOR(void*, calloc, uptr nmemb, uptr size) {
  if (tysan_init_is_running && REAL(calloc) == nullptr)
    return handleEarlyAlloc(nmemb*size);

  void *res = REAL(calloc)(nmemb, size);
  if (res)
    tysan_set_type_unknown(res, nmemb*size);
  return res;
}

INTERCEPTOR(void, free, void *p) {
  // There are only a few early allocation requests,
  // so we simply skip the free.
  if (isInEarlyAllocBuf(p))
    return;
  REAL(free)(p);
}

INTERCEPTOR(void*, valloc, uptr size) {
  void *res = REAL(valloc)(size);
  if (res)
    tysan_set_type_unknown(res, size);
  return res;
}

INTERCEPTOR(void*, memalign, uptr alignment, uptr size) {
  void *res = REAL(memalign)(alignment, size);
  if (res)
    tysan_set_type_unknown(res, size);
  return res;
}

INTERCEPTOR(void*, __libc_memalign, uptr alignment, uptr size) {
  void *res = REAL(__libc_memalign)(alignment, size);
  if (res)
    tysan_set_type_unknown(res, size);
  return res;
}

INTERCEPTOR(void*, pvalloc, uptr size) {
  void *res = REAL(pvalloc)(size);
  if (res)
    tysan_set_type_unknown(res, size);
  return res;
}

INTERCEPTOR(void*, aligned_alloc, uptr alignment, uptr size) {
  void *res = REAL(aligned_alloc)(alignment, size);
  if (res)
    tysan_set_type_unknown(res, size);
  return res;
}

INTERCEPTOR(int, posix_memalign, void **memptr, uptr alignment, uptr size) {
  int res = REAL(posix_memalign)(memptr, alignment, size);
  if (res == 0 && *memptr)
    tysan_set_type_unknown(*memptr, size);
  return res;
}

namespace __tysan {
void InitializeInterceptors() {
  static int inited = 0;
  CHECK_EQ(inited, 0);

  // Instruct libc malloc to consume less memory.
#if SANITIZER_LINUX
  mallopt(1, 0);  // M_MXFAST
  mallopt(-3, 32*1024);  // M_MMAP_THRESHOLD
#endif

  INTERCEPT_FUNCTION(mmap);
  INTERCEPT_FUNCTION(mmap64);

  INTERCEPT_FUNCTION(strdup);
#if TYSAN_INTERCEPT___STRDUP
  INTERCEPT_FUNCTION(__strdup);
#endif

  INTERCEPT_FUNCTION(malloc);
  INTERCEPT_FUNCTION(calloc);
  INTERCEPT_FUNCTION(free);
  INTERCEPT_FUNCTION(realloc);
  INTERCEPT_FUNCTION(valloc);
  INTERCEPT_FUNCTION(memalign);
  INTERCEPT_FUNCTION(__libc_memalign);
  INTERCEPT_FUNCTION(pvalloc);
  INTERCEPT_FUNCTION(aligned_alloc);
  INTERCEPT_FUNCTION(posix_memalign);

  INTERCEPT_FUNCTION(memset);
  INTERCEPT_FUNCTION(memmove);
  INTERCEPT_FUNCTION(memcpy);

  inited = 1;
}
}  // namespace __tysan
