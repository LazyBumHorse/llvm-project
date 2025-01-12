//===- OutputSections.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OutputSections.h"
#include "InputChunks.h"
#include "InputFiles.h"
#include "OutputSegment.h"
#include "WriterUtils.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Threads.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/LEB128.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::wasm;
using namespace lld;
using namespace lld::wasm;

static StringRef sectionTypeToString(uint32_t SectionType) {
  switch (SectionType) {
  case WASM_SEC_CUSTOM:
    return "CUSTOM";
  case WASM_SEC_TYPE:
    return "TYPE";
  case WASM_SEC_IMPORT:
    return "IMPORT";
  case WASM_SEC_FUNCTION:
    return "FUNCTION";
  case WASM_SEC_TABLE:
    return "TABLE";
  case WASM_SEC_MEMORY:
    return "MEMORY";
  case WASM_SEC_GLOBAL:
    return "GLOBAL";
  case WASM_SEC_EVENT:
    return "EVENT";
  case WASM_SEC_EXPORT:
    return "EXPORT";
  case WASM_SEC_START:
    return "START";
  case WASM_SEC_ELEM:
    return "ELEM";
  case WASM_SEC_CODE:
    return "CODE";
  case WASM_SEC_DATA:
    return "DATA";
  case WASM_SEC_DATACOUNT:
    return "DATACOUNT";
  default:
    fatal("invalid section type");
  }
}

// Returns a string, e.g. "FUNCTION(.text)".
std::string lld::toString(const OutputSection &Sec) {
  if (!Sec.Name.empty())
    return (Sec.getSectionName() + "(" + Sec.Name + ")").str();
  return Sec.getSectionName();
}

StringRef OutputSection::getSectionName() const {
  return sectionTypeToString(Type);
}

void OutputSection::createHeader(size_t BodySize) {
  raw_string_ostream OS(Header);
  debugWrite(OS.tell(), "section type [" + getSectionName() + "]");
  encodeULEB128(Type, OS);
  writeUleb128(OS, BodySize, "section size");
  OS.flush();
  log("createHeader: " + toString(*this) + " body=" + Twine(BodySize) +
      " total=" + Twine(getSize()));
}

void CodeSection::finalizeContents() {
  raw_string_ostream OS(CodeSectionHeader);
  writeUleb128(OS, Functions.size(), "function count");
  OS.flush();
  BodySize = CodeSectionHeader.size();

  for (InputFunction *Func : Functions) {
    Func->OutputOffset = BodySize;
    Func->calculateSize();
    BodySize += Func->getSize();
  }

  createHeader(BodySize);
}

void CodeSection::writeTo(uint8_t *Buf) {
  log("writing " + toString(*this));
  log(" size=" + Twine(getSize()));
  log(" headersize=" + Twine(Header.size()));
  log(" codeheadersize=" + Twine(CodeSectionHeader.size()));
  Buf += Offset;

  // Write section header
  memcpy(Buf, Header.data(), Header.size());
  Buf += Header.size();

  // Write code section headers
  memcpy(Buf, CodeSectionHeader.data(), CodeSectionHeader.size());

  // Write code section bodies
  for (const InputChunk *Chunk : Functions)
    Chunk->writeTo(Buf);
}

uint32_t CodeSection::numRelocations() const {
  uint32_t Count = 0;
  for (const InputChunk *Func : Functions)
    Count += Func->NumRelocations();
  return Count;
}

void CodeSection::writeRelocations(raw_ostream &OS) const {
  for (const InputChunk *C : Functions)
    C->writeRelocations(OS);
}

void DataSection::finalizeContents() {
  raw_string_ostream OS(DataSectionHeader);

  writeUleb128(OS, Segments.size(), "data segment count");
  OS.flush();
  BodySize = DataSectionHeader.size();

  assert(!Config->Pic ||
         Segments.size() <= 1 &&
             "Currenly only a single data segment is supported in PIC mode");

  for (OutputSegment *Segment : Segments) {
    raw_string_ostream OS(Segment->Header);
    writeUleb128(OS, Segment->InitFlags, "init flags");
    if (Segment->InitFlags & WASM_SEGMENT_HAS_MEMINDEX)
      writeUleb128(OS, 0, "memory index");
    if ((Segment->InitFlags & WASM_SEGMENT_IS_PASSIVE) == 0) {
      WasmInitExpr InitExpr;
      if (Config->Pic) {
        InitExpr.Opcode = WASM_OPCODE_GLOBAL_GET;
        InitExpr.Value.Global = WasmSym::MemoryBase->getGlobalIndex();
      } else {
        InitExpr.Opcode = WASM_OPCODE_I32_CONST;
        InitExpr.Value.Int32 = Segment->StartVA;
      }
      writeInitExpr(OS, InitExpr);
    }
    writeUleb128(OS, Segment->Size, "segment size");
    OS.flush();

    Segment->SectionOffset = BodySize;
    BodySize += Segment->Header.size() + Segment->Size;
    log("Data segment: size=" + Twine(Segment->Size) + ", startVA=" +
        Twine::utohexstr(Segment->StartVA) + ", name=" + Segment->Name);

    for (InputSegment *InputSeg : Segment->InputSegments)
      InputSeg->OutputOffset = Segment->SectionOffset + Segment->Header.size() +
                               InputSeg->OutputSegmentOffset;
  }

  createHeader(BodySize);
}

void DataSection::writeTo(uint8_t *Buf) {
  log("writing " + toString(*this) + " size=" + Twine(getSize()) +
      " body=" + Twine(BodySize));
  Buf += Offset;

  // Write section header
  memcpy(Buf, Header.data(), Header.size());
  Buf += Header.size();

  // Write data section headers
  memcpy(Buf, DataSectionHeader.data(), DataSectionHeader.size());

  for (const OutputSegment *Segment : Segments) {
    // Write data segment header
    uint8_t *SegStart = Buf + Segment->SectionOffset;
    memcpy(SegStart, Segment->Header.data(), Segment->Header.size());

    // Write segment data payload
    for (const InputChunk *Chunk : Segment->InputSegments)
      Chunk->writeTo(Buf);
  }
}

uint32_t DataSection::numRelocations() const {
  uint32_t Count = 0;
  for (const OutputSegment *Seg : Segments)
    for (const InputChunk *InputSeg : Seg->InputSegments)
      Count += InputSeg->NumRelocations();
  return Count;
}

void DataSection::writeRelocations(raw_ostream &OS) const {
  for (const OutputSegment *Seg : Segments)
    for (const InputChunk *C : Seg->InputSegments)
      C->writeRelocations(OS);
}

void CustomSection::finalizeContents() {
  raw_string_ostream OS(NameData);
  encodeULEB128(Name.size(), OS);
  OS << Name;
  OS.flush();

  for (InputSection *Section : InputSections) {
    Section->OutputOffset = PayloadSize;
    Section->OutputSec = this;
    PayloadSize += Section->getSize();
  }

  createHeader(PayloadSize + NameData.size());
}

void CustomSection::writeTo(uint8_t *Buf) {
  log("writing " + toString(*this) + " size=" + Twine(getSize()) +
      " chunks=" + Twine(InputSections.size()));

  assert(Offset);
  Buf += Offset;

  // Write section header
  memcpy(Buf, Header.data(), Header.size());
  Buf += Header.size();
  memcpy(Buf, NameData.data(), NameData.size());
  Buf += NameData.size();

  // Write custom sections payload
  for (const InputSection *Section : InputSections)
    Section->writeTo(Buf);
}

uint32_t CustomSection::numRelocations() const {
  uint32_t Count = 0;
  for (const InputSection *InputSect : InputSections)
    Count += InputSect->NumRelocations();
  return Count;
}

void CustomSection::writeRelocations(raw_ostream &OS) const {
  for (const InputSection *S : InputSections)
    S->writeRelocations(OS);
}
