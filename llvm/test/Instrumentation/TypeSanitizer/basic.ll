; Test basic type sanitizer instrumentation.
;
; RUN: opt < %s -tysan -S | FileCheck %s

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; CHECK-DAG: $__tysan_v1_Simple_20C_2b_2b_20TBAA = comdat any
; CHECK-DAG: $__tysan_v1_omnipotent_20char = comdat any
; CHECK-DAG: $__tysan_v1_int = comdat any
; CHECK-DAG: $__tysan_v1_int_o_0 = comdat any
; CHECK-DAG: $__tysan_v1___ZTS1x = comdat any
; CHECK-DAG: $__tysan_v1___ZTS1v = comdat any
; CHECK-DAG: $__tysan_v1___ZTS1v_o_12 = comdat any
; CHECK-DAG: $__tysan_v1_____anonymous__027d9e575c5d34cb5d60d6a1d6276f95 = comdat any
; CHECK-DAG: $__tysan_v1_____anonymous__027d9e575c5d34cb5d60d6a1d6276f95_o_24 = comdat any

; CHECK-DAG: @llvm.global_ctors = appending global [1 x { i32, void ()*, i8* }] [{ i32, void ()*, i8* } { i32 0, void ()* @tysan.module_ctor, i8* null }]

; CHECK-DAG: @__tysan_shadow_memory_address = external global i64
; CHECK-DAG: @__tysan_app_memory_mask = external global i64

; CHECK-DAG: @__tysan_v1_Simple_20C_2b_2b_20TBAA = linkonce_odr constant { i64, i64, [16 x i8] } { i64 2, i64 0, [16 x i8] c"Simple C++ TBAA\00" }, comdat
; CHECK-DAG: @__tysan_v1_omnipotent_20char = linkonce_odr constant { i64, i64, { {{.*}} }*, i64, [16 x i8] } { i64 2, i64 1, { {{.*}} }* @__tysan_v1_Simple_20C_2b_2b_20TBAA, i64 0, [16 x i8] c"omnipotent char\00" }, comdat
; CHECK-DAG: @__tysan_v1_int = linkonce_odr constant { i64, i64, { {{.*}} }*, i64, [4 x i8] } { i64 2, i64 1, { {{.*}} }* @__tysan_v1_omnipotent_20char, i64 0, [4 x i8] c"int\00" }, comdat
; CHECK-DAG: @__tysan_v1_int_o_0 = linkonce_odr constant { i64, { {{.*}} }*, { {{.*}} }*, i64 } { i64 1, { {{.*}} }* @__tysan_v1_int, { {{.*}} }* @__tysan_v1_int, i64 0 }, comdat
; CHECK-DAG: @__tysan_v1___ZTS1x = linkonce_odr constant { i64, i64, { {{.*}} }*, i64, { {{.*}} }*, i64, [7 x i8] } { i64 2, i64 2, { {{.*}} }* @__tysan_v1_int, i64 0, { {{.*}} }* @__tysan_v1_int, i64 4, [7 x i8] c"_ZTS1x\00" }, comdat
; CHECK-DAG: @__tysan_v1___ZTS1v = linkonce_odr constant { i64, i64, { {{.*}} }*, i64, { {{.*}} }*, i64, { {{.*}} }*, i64, [7 x i8] } { i64 2, i64 3, { {{.*}} }* @__tysan_v1_int, i64 8, { {{.*}} }* @__tysan_v1_int, i64 12, { {{.*}} }* @__tysan_v1___ZTS1x, i64 16, [7 x i8] c"_ZTS1v\00" }, comdat
; CHECK-DAG: @__tysan_v1___ZTS1v_o_12 = linkonce_odr constant { i64, { {{.*}} }*, { {{.*}} }*, i64 } { i64 1, { {{.*}} }* @__tysan_v1___ZTS1v, { {{.*}} }* @__tysan_v1_int, i64 12 }, comdat
; CHECK-DAG: @__tysan_v1___ZTSN12__GLOBAL____N__11zE = internal constant { i64, i64, { {{.*}} }*, i64, [23 x i8] } { i64 2, i64 1, { {{.*}} }* @__tysan_v1_int, i64 24, [23 x i8] c"_ZTSN12_GLOBAL__N_11zE\00" }
; CHECK-DAG: @__tysan_v1___ZTSN12__GLOBAL____N__11zE_o_24 = internal constant { i64, { {{.*}} }*, { {{.*}} }*, i64 } { i64 1, { {{.*}} }* @__tysan_v1___ZTSN12__GLOBAL____N__11zE, { {{.*}} }* @__tysan_v1_int, i64 24 }
; CHECK-DAG: @__tysan_v1___ZTS1yIN12__GLOBAL____N__11zEE = internal constant { i64, i64, { {{.*}} }*, i64, [27 x i8] } { i64 2, i64 1, { {{.*}} }* @__tysan_v1_int, i64 24, [27 x i8] c"_ZTS1yIN12_GLOBAL__N_11zEE\00" }
; CHECK-DAG: @__tysan_v1___ZTS1yIN12__GLOBAL____N__11zEE_o_24 = internal constant { i64, { {{.*}} }*, { {{.*}} }*, i64 } { i64 1, { {{.*}} }* @__tysan_v1___ZTS1yIN12__GLOBAL____N__11zEE, { {{.*}} }* @__tysan_v1_int, i64 24 }
; CHECK-DAG: @__tysan_v1_____anonymous__027d9e575c5d34cb5d60d6a1d6276f95 = linkonce_odr constant { i64, i64, { {{.*}} }*, i64, [1 x i8] } { i64 2, i64 1, { {{.*}} }* @__tysan_v1_int, i64 24, [1 x i8] zeroinitializer }, comdat
; CHECK-DAG: @__tysan_v1_____anonymous__027d9e575c5d34cb5d60d6a1d6276f95_o_24 = linkonce_odr constant { i64, { {{.*}} }*, { {{.*}} }*, i64 } { i64 1, { {{.*}} }* @__tysan_v1_____anonymous__027d9e575c5d34cb5d60d6a1d6276f95, { {{.*}} }* @__tysan_v1_int, i64 24 }, comdat

@global1 = global i32 0, align 4
@global2 = global i32 0, align 4

define i32 @test_load(i32* %a) sanitize_type {
entry:
  %tmp1 = load i32, i32* %a, align 4, !tbaa !3
  ret i32 %tmp1

; CHECK-LABEL: @test_load
; CHECK: [[V0:%[0-9]+]] = load i64, i64* @__tysan_app_memory_mask
; CHECK: [[V1:%[0-9]+]] = load i64, i64* @__tysan_shadow_memory_address
; CHECK: [[V2:%[0-9]+]] = ptrtoint i32* %a to i64
; CHECK: [[V3:%[0-9]+]] = and i64 [[V2]], [[V0]]
; CHECK: [[V4:%[0-9]+]] = shl i64 [[V3]], 3
; CHECK: [[V5:%[0-9]+]] = add i64 [[V4]], [[V1]]
; CHECK: [[V6:%[0-9]+]] = inttoptr i64 [[V5]] to i8**
; CHECK: [[V7:%[0-9]+]] = load i8*, i8** [[V6]]
; CHECK: [[V8:%[0-9]+]] = icmp ne i8* [[V7]], bitcast ({ {{.*}} }* @__tysan_v1_int_o_0 to i8*)
; CHECK: br i1 [[V8]], label %{{[0-9]+}}, label %{{[0-9]+}}, !prof ![[PROFMD:[0-9]+]]

; CHECK: [[V10:%[0-9]+]] = icmp eq i8* [[V7]], null
; CHECK: br i1 [[V10]], label %{{[0-9]+}}, label %{{[0-9]+}}

; CHECK: [[V12:%[0-9]+]] = add i64 [[V5]], 8
; CHECK: [[V13:%[0-9]+]] = inttoptr i64 [[V12]] to i8**
; CHECK: [[V14:%[0-9]+]] = load i8*, i8** [[V13]]
; CHECK: [[V15:%[0-9]+]] = icmp ne i8* [[V14]], null
; CHECK: [[V16:%[0-9]+]] = or i1 false, [[V15]]
; CHECK: [[V17:%[0-9]+]] = add i64 [[V5]], 16
; CHECK: [[V18:%[0-9]+]] = inttoptr i64 [[V17]] to i8**
; CHECK: [[V19:%[0-9]+]] = load i8*, i8** [[V18]]
; CHECK: [[V20:%[0-9]+]] = icmp ne i8* [[V19]], null
; CHECK: [[V21:%[0-9]+]] = or i1 [[V16]], [[V20]]
; CHECK: [[V22:%[0-9]+]] = add i64 [[V5]], 24
; CHECK: [[V23:%[0-9]+]] = inttoptr i64 [[V22]] to i8**
; CHECK: [[V24:%[0-9]+]] = load i8*, i8** [[V23]]
; CHECK: [[V25:%[0-9]+]] = icmp ne i8* [[V24]], null
; CHECK: [[V26:%[0-9]+]] = or i1 [[V21]], [[V25]]
; CHECK: br i1 [[V26]], label %{{[0-9]+}}, label %{{[0-9]+}}, !prof ![[PROFMD]]

; CHECK: [[V28:%[0-9]+]] = bitcast i32* %a to i8*
; CHECK: call void @__tysan_check(i8* [[V28]], i32 4, i8* bitcast ({ {{.*}} }* @__tysan_v1_int_o_0 to i8*), i32 1)
; CHECK: br label %{{[0-9]+}}

; CHECK: store i8* bitcast ({ {{.*}} }* @__tysan_v1_int_o_0 to i8*), i8** [[V6]]
; CHECK: [[V30:%[0-9]+]] = add i64 [[V5]], 8
; CHECK: [[V31:%[0-9]+]] = inttoptr i64 [[V30]] to i8**
; CHECK: store i8* inttoptr (i64 -1 to i8*), i8** [[V31]]
; CHECK: [[V32:%[0-9]+]] = add i64 [[V5]], 16
; CHECK: [[V33:%[0-9]+]] = inttoptr i64 [[V32]] to i8**
; CHECK: store i8* inttoptr (i64 -2 to i8*), i8** [[V33]]
; CHECK: [[V34:%[0-9]+]] = add i64 [[V5]], 24
; CHECK: [[V35:%[0-9]+]] = inttoptr i64 [[V34]] to i8**
; CHECK: store i8* inttoptr (i64 -3 to i8*), i8** [[V35]]
; CHECK: br label %{{[0-9]+}}

; CHECK: [[V37:%[0-9]+]] = bitcast i32* %a to i8*
; CHECK: call void @__tysan_check(i8* [[V37]], i32 4, i8* bitcast ({ {{.*}} }* @__tysan_v1_int_o_0 to i8*), i32 1)
; CHECK: br label %{{[0-9]+}}

; CHECK: br label %{{[0-9]+}}

; CHECK: [[V40:%[0-9]+]] = add i64 [[V5]], 8
; CHECK: [[V41:%[0-9]+]] = inttoptr i64 [[V40]] to i8**
; CHECK: [[V42:%[0-9]+]] = load i8*, i8** [[V41]]
; CHECK: [[V42a:%[0-9]+]] = ptrtoint i8* [[V42]] to i64
; CHECK: [[V43:%[0-9]+]] = icmp sge i64 [[V42a]], 0
; CHECK: [[V44:%[0-9]+]] = or i1 false, [[V43]]
; CHECK: [[V45:%[0-9]+]] = add i64 [[V5]], 16
; CHECK: [[V46:%[0-9]+]] = inttoptr i64 [[V45]] to i8**
; CHECK: [[V47:%[0-9]+]] = load i8*, i8** [[V46]]
; CHECK: [[V47a:%[0-9]+]] = ptrtoint i8* [[V47]] to i64
; CHECK: [[V48:%[0-9]+]] = icmp sge i64 [[V47a]], 0
; CHECK: [[V49:%[0-9]+]] = or i1 [[V44]], [[V48]]
; CHECK: [[V50:%[0-9]+]] = add i64 [[V5]], 24
; CHECK: [[V51:%[0-9]+]] = inttoptr i64 [[V50]] to i8**
; CHECK: [[V52:%[0-9]+]] = load i8*, i8** [[V51]]
; CHECK: [[V52a:%[0-9]+]] = ptrtoint i8* [[V52]] to i64
; CHECK: [[V53:%[0-9]+]] = icmp sge i64 [[V52a]], 0
; CHECK: [[V54:%[0-9]+]] = or i1 [[V49]], [[V53]]
; CHECK: br i1 [[V54]], label %{{[0-9]+}}, label %{{[0-9]+}}, !prof ![[PROFMD]]

; CHECK: [[V56:%[0-9]+]] = bitcast i32* %a to i8*
; CHECK: call void @__tysan_check(i8* [[V56]], i32 4, i8* bitcast ({ {{.*}} }* @__tysan_v1_int_o_0 to i8*), i32 1)
; CHECK: br label %{{[0-9]+}}

; CHECK: br label %{{[0-9]+}}

; CHECK: %tmp1 = load i32, i32* %a, align 4, !tbaa !{{[0-9]+}}
; CHECK: ret i32 %tmp1
}

define void @test_store(i32* %a) sanitize_type {
entry:
  store i32 42, i32* %a, align 4, !tbaa !6
  ret void

; CHECK-LABEL: @test_store
; CHECK: [[V0:%[0-9]+]] = load i64, i64* @__tysan_app_memory_mask
; CHECK: [[V1:%[0-9]+]] = load i64, i64* @__tysan_shadow_memory_address
; CHECK: [[V2:%[0-9]+]] = ptrtoint i32* %a to i64
; CHECK: [[V3:%[0-9]+]] = and i64 [[V2]], [[V0]]
; CHECK: [[V4:%[0-9]+]] = shl i64 [[V3]], 3
; CHECK: [[V5:%[0-9]+]] = add i64 [[V4]], [[V1]]
; CHECK: [[V6:%[0-9]+]] = inttoptr i64 [[V5]] to i8**
; CHECK: [[V7:%[0-9]+]] = load i8*, i8** [[V6]]
; CHECK: [[V8:%[0-9]+]] = icmp ne i8* [[V7]], bitcast ({ {{.*}} }* @__tysan_v1___ZTS1v_o_12 to i8*)
; CHECK: br i1 [[V8]], label %{{[0-9]+}}, label %{{[0-9]+}}, !prof ![[PROFMD]]

; CHECK: [[V10:%[0-9]+]] = icmp eq i8* [[V7]], null
; CHECK: br i1 [[V10]], label %{{[0-9]+}}, label %{{[0-9]+}}

; CHECK: [[V12:%[0-9]+]] = add i64 [[V5]], 8
; CHECK: [[V13:%[0-9]+]] = inttoptr i64 [[V12]] to i8**
; CHECK: [[V14:%[0-9]+]] = load i8*, i8** [[V13]]
; CHECK: [[V15:%[0-9]+]] = icmp ne i8* [[V14]], null
; CHECK: [[V16:%[0-9]+]] = or i1 false, [[V15]]
; CHECK: [[V17:%[0-9]+]] = add i64 [[V5]], 16
; CHECK: [[V18:%[0-9]+]] = inttoptr i64 [[V17]] to i8**
; CHECK: [[V19:%[0-9]+]] = load i8*, i8** [[V18]]
; CHECK: [[V20:%[0-9]+]] = icmp ne i8* [[V19]], null
; CHECK: [[V21:%[0-9]+]] = or i1 [[V16]], [[V20]]
; CHECK: [[V22:%[0-9]+]] = add i64 [[V5]], 24
; CHECK: [[V23:%[0-9]+]] = inttoptr i64 [[V22]] to i8**
; CHECK: [[V24:%[0-9]+]] = load i8*, i8** [[V23]]
; CHECK: [[V25:%[0-9]+]] = icmp ne i8* [[V24]], null
; CHECK: [[V26:%[0-9]+]] = or i1 [[V21]], [[V25]]
; CHECK: br i1 [[V26]], label %{{[0-9]+}}, label %{{[0-9]+}}, !prof ![[PROFMD]]

; CHECK: [[V28:%[0-9]+]] = bitcast i32* %a to i8*
; CHECK: call void @__tysan_check(i8* [[V28]], i32 4, i8* bitcast ({ {{.*}} }* @__tysan_v1___ZTS1v_o_12 to i8*), i32 2)
; CHECK: br label %{{[0-9]+}}

; CHECK: store i8* bitcast ({ {{.*}} }* @__tysan_v1___ZTS1v_o_12 to i8*), i8** [[V6]]
; CHECK: [[V30:%[0-9]+]] = add i64 [[V5]], 8
; CHECK: [[V31:%[0-9]+]] = inttoptr i64 [[V30]] to i8**
; CHECK: store i8* inttoptr (i64 -1 to i8*), i8** [[V31]]
; CHECK: [[V32:%[0-9]+]] = add i64 [[V5]], 16
; CHECK: [[V33:%[0-9]+]] = inttoptr i64 [[V32]] to i8**
; CHECK: store i8* inttoptr (i64 -2 to i8*), i8** [[V33]]
; CHECK: [[V34:%[0-9]+]] = add i64 [[V5]], 24
; CHECK: [[V35:%[0-9]+]] = inttoptr i64 [[V34]] to i8**
; CHECK: store i8* inttoptr (i64 -3 to i8*), i8** [[V35]]
; CHECK: br label %{{[0-9]+}}

; CHECK: [[V37:%[0-9]+]] = bitcast i32* %a to i8*
; CHECK: call void @__tysan_check(i8* [[V37]], i32 4, i8* bitcast ({ {{.*}} }* @__tysan_v1___ZTS1v_o_12 to i8*), i32 2)
; CHECK: br label %{{[0-9]+}}

; CHECK: br label %{{[0-9]+}}

; CHECK: [[V40:%[0-9]+]] = add i64 [[V5]], 8
; CHECK: [[V41:%[0-9]+]] = inttoptr i64 [[V40]] to i8**
; CHECK: [[V42:%[0-9]+]] = load i8*, i8** [[V41]]
; CHECK: [[V42a:%[0-9]+]] = ptrtoint i8* [[V42]] to i64
; CHECK: [[V43:%[0-9]+]] = icmp sge i64 [[V42a]], 0
; CHECK: [[V44:%[0-9]+]] = or i1 false, [[V43]]
; CHECK: [[V45:%[0-9]+]] = add i64 [[V5]], 16
; CHECK: [[V46:%[0-9]+]] = inttoptr i64 [[V45]] to i8**
; CHECK: [[V47:%[0-9]+]] = load i8*, i8** [[V46]]
; CHECK: [[V47a:%[0-9]+]] = ptrtoint i8* [[V47]] to i64
; CHECK: [[V48:%[0-9]+]] = icmp sge i64 [[V47a]], 0
; CHECK: [[V49:%[0-9]+]] = or i1 [[V44]], [[V48]]
; CHECK: [[V50:%[0-9]+]] = add i64 [[V5]], 24
; CHECK: [[V51:%[0-9]+]] = inttoptr i64 [[V50]] to i8**
; CHECK: [[V52:%[0-9]+]] = load i8*, i8** [[V51]]
; CHECK: [[V52a:%[0-9]+]] = ptrtoint i8* [[V52]] to i64
; CHECK: [[V53:%[0-9]+]] = icmp sge i64 [[V52a]], 0
; CHECK: [[V54:%[0-9]+]] = or i1 [[V49]], [[V53]]
; CHECK: br i1 [[V54]], label %{{[0-9]+}}, label %{{[0-9]+}}, !prof ![[PROFMD]]

; CHECK: [[V56:%[0-9]+]] = bitcast i32* %a to i8*
; CHECK: call void @__tysan_check(i8* [[V56]], i32 4, i8* bitcast ({ {{.*}} }* @__tysan_v1___ZTS1v_o_12 to i8*), i32 2)
; CHECK: br label %{{[0-9]+}}

; CHECK: br label %{{[0-9]+}}

; CHECK: store i32 42, i32* %a, align 4, !tbaa !{{[0-9]+}}
; CHECK: ret void
}

define i32 @test_load_unk(i32* %a) sanitize_type {
entry:
  %tmp1 = load i32, i32* %a, align 4
  ret i32 %tmp1

; CHECK-LABEL: @test_load_unk
; CHECK: [[PTR:%[0-9]+]] = bitcast i32* %a to i8*
; CHECK: call void @__tysan_check(i8* [[PTR]], i32 4, i8* null, i32 1)
; CHECK: ret i32
}

define void @test_store_unk(i32* %a) sanitize_type {
entry:
  store i32 42, i32* %a, align 4
  ret void

; CHECK-LABEL: @test_store_unk
; CHECK: [[PTR:%[0-9]+]] = bitcast i32* %a to i8*
; CHECK: call void @__tysan_check(i8* [[PTR]], i32 4, i8* null, i32 2)
; CHECK: ret void
}

define i32 @test_load_nsan(i32* %a) {
entry:
  %tmp1 = load i32, i32* %a, align 4, !tbaa !3
  ret i32 %tmp1

; CHECK-LABEL: @test_load_nsan
; CHECK: [[V0:%[0-9]+]] = load i64, i64* @__tysan_app_memory_mask
; CHECK: [[V1:%[0-9]+]] = load i64, i64* @__tysan_shadow_memory_address
; CHECK: [[V2:%[0-9]+]] = ptrtoint i32* %a to i64
; CHECK: [[V3:%[0-9]+]] = and i64 [[V2]], [[V0]]
; CHECK: [[V4:%[0-9]+]] = shl i64 [[V3]], 3
; CHECK: [[V5:%[0-9]+]] = add i64 [[V4]], [[V1]]
; CHECK: [[V6:%[0-9]+]] = inttoptr i64 [[V5]] to i8**
; CHECK: [[V7:%[0-9]+]] = load i8*, i8** [[V6]]
; CHECK: [[V8:%[0-9]+]] = icmp eq i8* [[V7]], null
; CHECK: br i1 [[V8]], label %{{[0-9]+}}, label %{{[0-9]+}}, !prof ![[PROFMD]]

; CHECK: store i8* bitcast ({ {{.*}} }* @__tysan_v1_int_o_0 to i8*), i8** [[V6]]
; CHECK: [[V10:%[0-9]+]] = add i64 [[V5]], 8
; CHECK: [[V11:%[0-9]+]] = inttoptr i64 [[V10]] to i8**
; CHECK: store i8* inttoptr (i64 -1 to i8*), i8** [[V11]]
; CHECK: [[V12:%[0-9]+]] = add i64 [[V5]], 16
; CHECK: [[V13:%[0-9]+]] = inttoptr i64 [[V12]] to i8**
; CHECK: store i8* inttoptr (i64 -2 to i8*), i8** [[V13]]
; CHECK: [[V14:%[0-9]+]] = add i64 [[V5]], 24
; CHECK: [[V15:%[0-9]+]] = inttoptr i64 [[V14]] to i8**
; CHECK: store i8* inttoptr (i64 -3 to i8*), i8** [[V15]]
; CHECK: br label %{{[0-9]+}}

; CHECK: %tmp1 = load i32, i32* %a, align 4, !tbaa !{{[0-9]+}}
; CHECK: ret i32 %tmp1
}

define void @test_store_nsan(i32* %a) {
entry:
  store i32 42, i32* %a, align 4, !tbaa !3
  ret void

; CHECK-LABEL: @test_store_nsan
; CHECK: [[V0:%[0-9]+]] = load i64, i64* @__tysan_app_memory_mask
; CHECK: [[V1:%[0-9]+]] = load i64, i64* @__tysan_shadow_memory_address
; CHECK: [[V2:%[0-9]+]] = ptrtoint i32* %a to i64
; CHECK: [[V3:%[0-9]+]] = and i64 [[V2]], [[V0]]
; CHECK: [[V4:%[0-9]+]] = shl i64 [[V3]], 3
; CHECK: [[V5:%[0-9]+]] = add i64 [[V4]], [[V1]]
; CHECK: [[V6:%[0-9]+]] = inttoptr i64 [[V5]] to i8**
; CHECK: [[V7:%[0-9]+]] = load i8*, i8** [[V6]]
; CHECK: [[V8:%[0-9]+]] = icmp eq i8* [[V7]], null
; CHECK: br i1 [[V8]], label %{{[0-9]+}}, label %{{[0-9]+}}, !prof ![[PROFMD]]

; CHECK: store i8* bitcast ({ {{.*}} }* @__tysan_v1_int_o_0 to i8*), i8** [[V6]]
; CHECK: [[V10:%[0-9]+]] = add i64 [[V5]], 8
; CHECK: [[V11:%[0-9]+]] = inttoptr i64 [[V10]] to i8**
; CHECK: store i8* inttoptr (i64 -1 to i8*), i8** [[V11]]
; CHECK: [[V12:%[0-9]+]] = add i64 [[V5]], 16
; CHECK: [[V13:%[0-9]+]] = inttoptr i64 [[V12]] to i8**
; CHECK: store i8* inttoptr (i64 -2 to i8*), i8** [[V13]]
; CHECK: [[V14:%[0-9]+]] = add i64 [[V5]], 24
; CHECK: [[V15:%[0-9]+]] = inttoptr i64 [[V14]] to i8**
; CHECK: store i8* inttoptr (i64 -3 to i8*), i8** [[V15]]
; CHECK: br label %{{[0-9]+}}

; CHECK: store i32 42, i32* %a, align 4, !tbaa !{{[0-9]+}}
; CHECK: ret void
}

define void @test_anon_ns(i32* %a, i32* %b) sanitize_type {
entry:
  store i32 42, i32* %a, align 4, !tbaa !8
  store i32 43, i32* %b, align 4, !tbaa !10
  ret void

; CHECK-LABEL: @test_anon_ns
; CHECK: store i8* bitcast ({ {{.*}} }* @__tysan_v1___ZTSN12__GLOBAL____N__11zE_o_24 to i8*), i8**
; CHECK: ret void
}

define void @test_anon_type(i32* %a) sanitize_type {
entry:
  store i32 42, i32* %a, align 4, !tbaa !12
  ret void

; CHECK-LABEL: @test_anon_type
; CHECK: store i8* bitcast ({ {{.*}} }* @__tysan_v1_____anonymous__027d9e575c5d34cb5d60d6a1d6276f95_o_24 to i8*), i8**
; CHECK: ret void
}

declare void @alloca_test_use([10 x i8]*)
define void @alloca_test() sanitize_type {
entry:
  %x = alloca [10 x i8], align 1
  call void @alloca_test_use([10 x i8]* %x)
  ret void

; CHECK-LABEL: @alloca_test
; CHECK: [[V0:%[0-9]+]] = load i64, i64* @__tysan_app_memory_mask
; CHECK: [[V1:%[0-9]+]] = load i64, i64* @__tysan_shadow_memory_address
; CHECK: %x = alloca [10 x i8], align 1
; CHECK: [[V2:%[0-9]+]] = ptrtoint [10 x i8]* %x to i64
; CHECK: [[V3:%[0-9]+]] = and i64 [[V2]], [[V0]]
; CHECK: [[V4:%[0-9]+]] = shl i64 [[V3]], 3
; CHECK: [[V5:%[0-9]+]] = add i64 [[V4]], [[V1]]
; CHECK: [[V6:%[0-9]+]] = inttoptr i64 [[V5]] to i8*
; CHECK: call void @llvm.memset.p0i8.i64(i8* [[V6]], i8 0, i64 80, i32 8, i1 false)
; CHECK: call void @alloca_test_use([10 x i8]* %x)
; CHECK: ret void
}

%struct.s20 = type { i32, i32, [24 x i8] }
define void @byval_test(%struct.s20* byval align 32 %x) sanitize_type {
entry:
  ret void

; CHECK-LABEL: @byval_test
; CHECK: [[V0:%[0-9]+]] = load i64, i64* @__tysan_app_memory_mask
; CHECK: [[V1:%[0-9]+]] = load i64, i64* @__tysan_shadow_memory_address
; CHECK: [[V2:%[0-9]+]] = ptrtoint %struct.s20* %x to i64
; CHECK: [[V3:%[0-9]+]] = and i64 [[V2]], [[V0]]
; CHECK: [[V4:%[0-9]+]] = shl i64 [[V3]], 3
; CHECK: [[V5:%[0-9]+]] = add i64 [[V4]], [[V1]]
; CHECK: [[V6:%[0-9]+]] = inttoptr i64 [[V5]] to i8*
; CHECK: call void @llvm.memset.p0i8.i64(i8* [[V6]], i8 0, i64 256, i32 8, i1 false)
; CHECK: ret void
; NOTE: Ideally, we'd get the type from the caller's copy of the data (instead
; of setting it all to unknown).
}

declare void @llvm.memset.p0i8.i64(i8* nocapture, i8, i64, i32, i1) nounwind
declare void @llvm.memmove.p0i8.p0i8.i64(i8* nocapture, i8* nocapture readonly, i64, i32, i1) nounwind
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* nocapture, i8* nocapture readonly, i64, i32, i1) nounwind

define void @memintr_test(i8* %a, i8* %b) nounwind uwtable sanitize_type {
  entry:
  tail call void @llvm.memset.p0i8.i64(i8* %a, i8 0, i64 100, i32 1, i1 false)
  tail call void @llvm.memmove.p0i8.p0i8.i64(i8* %a, i8* %b, i64 100, i32 1, i1 false)
  tail call void @llvm.memcpy.p0i8.p0i8.i64(i8* %a, i8* %b, i64 100, i32 1, i1 false)
  ret void

; CHECK-LABEL: @memintr_test
; CHECK: [[V0:%[0-9]+]] = load i64, i64* @__tysan_app_memory_mask
; CHECK: [[V1:%[0-9]+]] = load i64, i64* @__tysan_shadow_memory_address
; CHECK: [[V2:%[0-9]+]] = ptrtoint i8* %a to i64
; CHECK: [[V3:%[0-9]+]] = and i64 [[V2]], [[V0]]
; CHECK: [[V4:%[0-9]+]] = shl i64 [[V3]], 3
; CHECK: [[V5:%[0-9]+]] = add i64 [[V4]], [[V1]]
; CHECK: [[V6:%[0-9]+]] = inttoptr i64 [[V5]] to i8*
; CHECK: call void @llvm.memset.p0i8.i64(i8* [[V6]], i8 0, i64 800, i32 8, i1 false)
; CHECK: tail call void @llvm.memset.p0i8.i64(i8* %a, i8 0, i64 100, i32 1, i1 false)
; CHECK: [[V7:%[0-9]+]] = ptrtoint i8* %a to i64
; CHECK: [[V8:%[0-9]+]] = and i64 [[V7]], [[V0]]
; CHECK: [[V9:%[0-9]+]] = shl i64 [[V8]], 3
; CHECK: [[V10:%[0-9]+]] = add i64 [[V9]], [[V1]]
; CHECK: [[V11:%[0-9]+]] = inttoptr i64 [[V10]] to i8*
; CHECK: [[V12:%[0-9]+]] = ptrtoint i8* %b to i64
; CHECK: [[V13:%[0-9]+]] = and i64 [[V12]], [[V0]]
; CHECK: [[V14:%[0-9]+]] = shl i64 [[V13]], 3
; CHECK: [[V15:%[0-9]+]] = add i64 [[V14]], [[V1]]
; CHECK: [[V16:%[0-9]+]] = inttoptr i64 [[V15]] to i8*
; CHECK: call void @llvm.memmove.p0i8.p0i8.i64(i8* [[V11]], i8* [[V16]], i64 800, i32 8, i1 false)
; CHECK: tail call void @llvm.memmove.p0i8.p0i8.i64(i8* %a, i8* %b, i64 100, i32 1, i1 false)
; CHECK: [[V17:%[0-9]+]] = ptrtoint i8* %a to i64
; CHECK: [[V18:%[0-9]+]] = and i64 [[V17]], [[V0]]
; CHECK: [[V19:%[0-9]+]] = shl i64 [[V18]], 3
; CHECK: [[V20:%[0-9]+]] = add i64 [[V19]], [[V1]]
; CHECK: [[V21:%[0-9]+]] = inttoptr i64 [[V20]] to i8*
; CHECK: [[V22:%[0-9]+]] = ptrtoint i8* %b to i64
; CHECK: [[V23:%[0-9]+]] = and i64 [[V22]], [[V0]]
; CHECK: [[V24:%[0-9]+]] = shl i64 [[V23]], 3
; CHECK: [[V25:%[0-9]+]] = add i64 [[V24]], [[V1]]
; CHECK: [[V26:%[0-9]+]] = inttoptr i64 [[V25]] to i8*
; CHECK: call void @llvm.memcpy.p0i8.p0i8.i64(i8* [[V21]], i8* [[V26]], i64 800, i32 8, i1 false)
; CHECK: tail call void @llvm.memcpy.p0i8.p0i8.i64(i8* %a, i8* %b, i64 100, i32 1, i1 false)
; CHECK: ret void
}

define void @test_swifterror(i8** swifterror) sanitize_type {
  %swifterror_ptr_value = load i8*, i8** %0
  ret void

; CHECK-LABEL: @test_swifterror
; CHECK-NOT: __tysan_check
; CHECK: ret void
}

define void @test_swifterror_2(i8** swifterror) sanitize_type {
  store i8* null, i8** %0
  ret void

; CHECK-LABEL: @test_swifterror_2
; CHECK-NOT: __tysan_check
; CHECK: ret void
}

; CHECK-LABEL: define internal void @tysan.module_ctor() {
; CHECK:   call void @__tysan_init()
; CHECK:   call void @__tysan_set_globals_types()
; CHECK:   ret void
; CHECK: }

; CHECK-LABEL: define internal void @__tysan_set_globals_types() {
; CHECK:   [[V1:%[0-9]+]] = load i64, i64* @__tysan_app_memory_mask
; CHECK:   [[V2:%[0-9]+]] = load i64, i64* @__tysan_shadow_memory_address
; CHECK:   [[V3:%[0-9]+]] = and i64 ptrtoint (i32* @global1 to i64), [[V1]]
; CHECK:   [[V4:%[0-9]+]] = shl i64 [[V3]], 3
; CHECK:   [[V5:%[0-9]+]] = add i64 [[V4]], [[V2]]
; CHECK:   [[V6:%[0-9]+]] = inttoptr i64 [[V5]] to i8**
; CHECK:   store i8* bitcast ({ {{.*}} }* @__tysan_v1_int to i8*), i8** [[V6]]
; CHECK:   [[V7:%[0-9]+]] = add i64 [[V5]], 8
; CHECK:   [[V8:%[0-9]+]] = inttoptr i64 [[V7]] to i8**
; CHECK:   store i8* inttoptr (i64 -1 to i8*), i8** [[V8]]
; CHECK:   [[V9:%[0-9]+]] = add i64 [[V5]], 16
; CHECK:   [[V10:%[0-9]+]] = inttoptr i64 [[V9]] to i8**
; CHECK:   store i8* inttoptr (i64 -2 to i8*), i8** [[V10]]
; CHECK:   [[V11:%[0-9]+]] = add i64 [[V5]], 24
; CHECK:   [[V12:%[0-9]+]] = inttoptr i64 [[V11]] to i8**
; CHECK:   store i8* inttoptr (i64 -3 to i8*), i8** [[V12]]
; CHECK:   [[V13:%[0-9]+]] = and i64 ptrtoint (i32* @global1 to i64), [[V1]]
; CHECK:   [[V14:%[0-9]+]] = shl i64 [[V13]], 3
; CHECK:   [[V15:%[0-9]+]] = add i64 [[V14]], [[V2]]
; CHECK:   [[V16:%[0-9]+]] = inttoptr i64 [[V15]] to i8**
; CHECK:   store i8* bitcast ({ {{.*}} }* @__tysan_v1_int to i8*), i8** [[V16]]
; CHECK:   [[V17:%[0-9]+]] = add i64 [[V15]], 8
; CHECK:   [[V18:%[0-9]+]] = inttoptr i64 [[V17]] to i8**
; CHECK:   store i8* inttoptr (i64 -1 to i8*), i8** [[V18]]
; CHECK:   [[V19:%[0-9]+]] = add i64 [[V15]], 16
; CHECK:   [[V20:%[0-9]+]] = inttoptr i64 [[V19]] to i8**
; CHECK:   store i8* inttoptr (i64 -2 to i8*), i8** [[V20]]
; CHECK:   [[V21:%[0-9]+]] = add i64 [[V15]], 24
; CHECK:   [[V22:%[0-9]+]] = inttoptr i64 [[V21]] to i8**
; CHECK:   store i8* inttoptr (i64 -3 to i8*), i8** [[V22]]
; CHECK:   ret void
; CHECK: }

; CHECK: ![[PROFMD]] = !{!"branch_weights", i32 1, i32 100000}

!llvm.tysan.globals = !{!13, !14}

!0 = !{!"Simple C++ TBAA"}
!1 = !{!"omnipotent char", !0, i64 0}
!2 = !{!"int", !1, i64 0}
!3 = !{!2, !2, i64 0}
!4 = !{!"_ZTS1x", !2, i64 0, !2, i64 4}
!5 = !{!"_ZTS1v", !2, i64 8, !2, i64 12, !4, i64 16}
!6 = !{!5, !2, i64 12}
!7 = !{!"_ZTSN12_GLOBAL__N_11zE", !2, i64 24}
!8 = !{!7, !2, i64 24}
!9 = !{!"_ZTS1yIN12_GLOBAL__N_11zEE", !2, i64 24}
!10 = !{!9, !2, i64 24}
!11 = !{!"", !2, i64 24}
!12 = !{!11, !2, i64 24}
!13 = !{i32* @global1, !2}
!14 = !{i32* @global1, !2}

