//===-- tysan.h -------------------------------------------------*- C++ -*-===//
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
// Private TySan header.
//===----------------------------------------------------------------------===//

#ifndef TYSAN_H
#define TYSAN_H

#include "sanitizer_common/sanitizer_internal_defs.h"

using __sanitizer::uptr;
using __sanitizer::sptr;
using __sanitizer::u16;

#include "tysan_platform.h"

extern "C" {
void tysan_set_type_unknown(const void *addr, uptr size);
void tysan_copy_types(const void *daddr, const void *saddr, uptr size);
}

namespace __tysan {
extern bool tysan_inited;
extern bool tysan_init_is_running;

void InitializeInterceptors();

enum {
  TYSAN_MEMBER_TD = 1,
  TYSAN_STRUCT_TD = 2
};

struct tysan_member_type_descriptor {
  struct tysan_type_descriptor *Base;
  struct tysan_type_descriptor *Access;
  uptr Offset;
};

struct tysan_struct_type_descriptor {
  uptr MemberCount;
  struct {
    struct tysan_type_descriptor *Type;
    uptr Offset;
  } Members[1]; // Tail allocated.
  // char Name[]; // Tail allocated.
};

struct tysan_type_descriptor {
  uptr Tag;
  union {
    tysan_member_type_descriptor Member;
    tysan_struct_type_descriptor Struct;
  };
};

inline tysan_type_descriptor **shadow_for(const void *ptr) {
  return (tysan_type_descriptor **)
    ((((uptr) ptr) & AppMask())*sizeof(ptr) + ShadowAddr());
}

struct Flags {
#define TYSAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "tysan_flags.inc"
#undef TYSAN_FLAG

  void SetDefaults();
};

extern Flags flags_data;
inline Flags &flags() {
  return flags_data;
}

}  // namespace __tysan

#endif  // TYSAN_H
