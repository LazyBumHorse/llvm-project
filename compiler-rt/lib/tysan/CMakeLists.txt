include_directories(..)

# Runtime library sources and build flags.
set(TYSAN_RTL_SOURCES
  tysan.cc
  tysan_interceptors.cc)
set(TYSAN_COMMON_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF TYSAN_COMMON_CFLAGS)
# Prevent clang from generating libc calls.
append_list_if(COMPILER_RT_HAS_FFREESTANDING_FLAG -ffreestanding TYSAN_COMMON_CFLAGS)

# Static runtime library.
add_compiler_rt_component(tysan)

foreach(arch ${TYSAN_SUPPORTED_ARCH})
  set(TYSAN_CFLAGS ${TYSAN_COMMON_CFLAGS})
  append_list_if(COMPILER_RT_HAS_FPIE_FLAG -fPIE TYSAN_CFLAGS)
  add_compiler_rt_runtime(clang_rt.tysan
    STATIC
    ARCHS ${arch}
    SOURCES ${TYSAN_RTL_SOURCES}
            $<TARGET_OBJECTS:RTInterception.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
    CFLAGS ${TYSAN_CFLAGS}
    PARENT_TARGET tysan)
  add_sanitizer_rt_symbols(clang_rt.tysan
    ARCHS ${arch}
    EXTRA tysan.syms.extra)
  add_dependencies(tysan
    clang_rt.tysan-${arch}-symbols)
endforeach()

