//===- Symbols.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputEvent.h"
#include "InputFiles.h"
#include "InputGlobal.h"
#include "OutputSections.h"
#include "OutputSegment.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Strings.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::wasm;
using namespace lld;
using namespace lld::wasm;

DefinedFunction *WasmSym::CallCtors;
DefinedFunction *WasmSym::InitMemory;
DefinedFunction *WasmSym::ApplyRelocs;
DefinedData *WasmSym::DsoHandle;
DefinedData *WasmSym::DataEnd;
DefinedData *WasmSym::GlobalBase;
DefinedData *WasmSym::HeapBase;
GlobalSymbol *WasmSym::StackPointer;
UndefinedGlobal *WasmSym::TableBase;
UndefinedGlobal *WasmSym::MemoryBase;

WasmSymbolType Symbol::getWasmType() const {
  if (isa<FunctionSymbol>(this))
    return WASM_SYMBOL_TYPE_FUNCTION;
  if (isa<DataSymbol>(this))
    return WASM_SYMBOL_TYPE_DATA;
  if (isa<GlobalSymbol>(this))
    return WASM_SYMBOL_TYPE_GLOBAL;
  if (isa<EventSymbol>(this))
    return WASM_SYMBOL_TYPE_EVENT;
  if (isa<SectionSymbol>(this) || isa<OutputSectionSymbol>(this))
    return WASM_SYMBOL_TYPE_SECTION;
  llvm_unreachable("invalid symbol kind");
}

const WasmSignature *Symbol::getSignature() const {
  if (auto* F = dyn_cast<FunctionSymbol>(this))
    return F->Signature;
  if (auto *L = dyn_cast<LazySymbol>(this))
    return L->Signature;
  return nullptr;
}

InputChunk *Symbol::getChunk() const {
  if (auto *F = dyn_cast<DefinedFunction>(this))
    return F->Function;
  if (auto *D = dyn_cast<DefinedData>(this))
    return D->Segment;
  return nullptr;
}

bool Symbol::isDiscarded() const {
  if (InputChunk *C = getChunk())
    return C->Discarded;
  return false;
}

bool Symbol::isLive() const {
  if (auto *G = dyn_cast<DefinedGlobal>(this))
    return G->Global->Live;
  if (auto *E = dyn_cast<DefinedEvent>(this))
    return E->Event->Live;
  if (InputChunk *C = getChunk())
    return C->Live;
  return Referenced;
}

void Symbol::markLive() {
  assert(!isDiscarded());
  if (auto *G = dyn_cast<DefinedGlobal>(this))
    G->Global->Live = true;
  if (auto *E = dyn_cast<DefinedEvent>(this))
    E->Event->Live = true;
  if (InputChunk *C = getChunk())
    C->Live = true;
  Referenced = true;
}

uint32_t Symbol::getOutputSymbolIndex() const {
  assert(OutputSymbolIndex != INVALID_INDEX);
  return OutputSymbolIndex;
}

void Symbol::setOutputSymbolIndex(uint32_t Index) {
  LLVM_DEBUG(dbgs() << "setOutputSymbolIndex " << Name << " -> " << Index
                    << "\n");
  assert(OutputSymbolIndex == INVALID_INDEX);
  OutputSymbolIndex = Index;
}

void Symbol::setGOTIndex(uint32_t Index) {
  LLVM_DEBUG(dbgs() << "setGOTIndex " << Name << " -> " << Index << "\n");
  assert(GOTIndex == INVALID_INDEX);
  // Any symbol that is assigned a GOT entry must be exported othewise the
  // dynamic linker won't be able create the entry that contains it.
  ForceExport = true;
  GOTIndex = Index;
}

bool Symbol::isWeak() const {
  return (Flags & WASM_SYMBOL_BINDING_MASK) == WASM_SYMBOL_BINDING_WEAK;
}

bool Symbol::isLocal() const {
  return (Flags & WASM_SYMBOL_BINDING_MASK) == WASM_SYMBOL_BINDING_LOCAL;
}

bool Symbol::isHidden() const {
  return (Flags & WASM_SYMBOL_VISIBILITY_MASK) == WASM_SYMBOL_VISIBILITY_HIDDEN;
}

void Symbol::setHidden(bool IsHidden) {
  LLVM_DEBUG(dbgs() << "setHidden: " << Name << " -> " << IsHidden << "\n");
  Flags &= ~WASM_SYMBOL_VISIBILITY_MASK;
  if (IsHidden)
    Flags |= WASM_SYMBOL_VISIBILITY_HIDDEN;
  else
    Flags |= WASM_SYMBOL_VISIBILITY_DEFAULT;
}

bool Symbol::isExported() const {
  if (!isDefined() || isLocal())
    return false;

  if (ForceExport || Config->ExportAll)
    return true;

  if (Config->ExportDynamic && !isHidden())
    return true;

  return Flags & WASM_SYMBOL_EXPORTED;
}

uint32_t FunctionSymbol::getFunctionIndex() const {
  if (auto *F = dyn_cast<DefinedFunction>(this))
    return F->Function->getFunctionIndex();
  assert(FunctionIndex != INVALID_INDEX);
  return FunctionIndex;
}

void FunctionSymbol::setFunctionIndex(uint32_t Index) {
  LLVM_DEBUG(dbgs() << "setFunctionIndex " << Name << " -> " << Index << "\n");
  assert(FunctionIndex == INVALID_INDEX);
  FunctionIndex = Index;
}

bool FunctionSymbol::hasFunctionIndex() const {
  if (auto *F = dyn_cast<DefinedFunction>(this))
    return F->Function->hasFunctionIndex();
  return FunctionIndex != INVALID_INDEX;
}

uint32_t FunctionSymbol::getTableIndex() const {
  if (auto *F = dyn_cast<DefinedFunction>(this))
    return F->Function->getTableIndex();
  assert(TableIndex != INVALID_INDEX);
  return TableIndex;
}

bool FunctionSymbol::hasTableIndex() const {
  if (auto *F = dyn_cast<DefinedFunction>(this))
    return F->Function->hasTableIndex();
  return TableIndex != INVALID_INDEX;
}

void FunctionSymbol::setTableIndex(uint32_t Index) {
  // For imports, we set the table index here on the Symbol; for defined
  // functions we set the index on the InputFunction so that we don't export
  // the same thing twice (keeps the table size down).
  if (auto *F = dyn_cast<DefinedFunction>(this)) {
    F->Function->setTableIndex(Index);
    return;
  }
  LLVM_DEBUG(dbgs() << "setTableIndex " << Name << " -> " << Index << "\n");
  assert(TableIndex == INVALID_INDEX);
  TableIndex = Index;
}

DefinedFunction::DefinedFunction(StringRef Name, uint32_t Flags, InputFile *F,
                                 InputFunction *Function)
    : FunctionSymbol(Name, DefinedFunctionKind, Flags, F,
                     Function ? &Function->Signature : nullptr),
      Function(Function) {}

uint32_t DefinedData::getVirtualAddress() const {
  LLVM_DEBUG(dbgs() << "getVirtualAddress: " << getName() << "\n");
  if (Segment)
    return Segment->OutputSeg->StartVA + Segment->OutputSegmentOffset + Offset;
  return Offset;
}

void DefinedData::setVirtualAddress(uint32_t Value) {
  LLVM_DEBUG(dbgs() << "setVirtualAddress " << Name << " -> " << Value << "\n");
  assert(!Segment);
  Offset = Value;
}

uint32_t DefinedData::getOutputSegmentOffset() const {
  LLVM_DEBUG(dbgs() << "getOutputSegmentOffset: " << getName() << "\n");
  return Segment->OutputSegmentOffset + Offset;
}

uint32_t DefinedData::getOutputSegmentIndex() const {
  LLVM_DEBUG(dbgs() << "getOutputSegmentIndex: " << getName() << "\n");
  return Segment->OutputSeg->Index;
}

uint32_t GlobalSymbol::getGlobalIndex() const {
  if (auto *F = dyn_cast<DefinedGlobal>(this))
    return F->Global->getGlobalIndex();
  assert(GlobalIndex != INVALID_INDEX);
  return GlobalIndex;
}

void GlobalSymbol::setGlobalIndex(uint32_t Index) {
  LLVM_DEBUG(dbgs() << "setGlobalIndex " << Name << " -> " << Index << "\n");
  assert(GlobalIndex == INVALID_INDEX);
  GlobalIndex = Index;
}

bool GlobalSymbol::hasGlobalIndex() const {
  if (auto *F = dyn_cast<DefinedGlobal>(this))
    return F->Global->hasGlobalIndex();
  return GlobalIndex != INVALID_INDEX;
}

DefinedGlobal::DefinedGlobal(StringRef Name, uint32_t Flags, InputFile *File,
                             InputGlobal *Global)
    : GlobalSymbol(Name, DefinedGlobalKind, Flags, File,
                   Global ? &Global->getType() : nullptr),
      Global(Global) {}

uint32_t EventSymbol::getEventIndex() const {
  if (auto *F = dyn_cast<DefinedEvent>(this))
    return F->Event->getEventIndex();
  assert(EventIndex != INVALID_INDEX);
  return EventIndex;
}

void EventSymbol::setEventIndex(uint32_t Index) {
  LLVM_DEBUG(dbgs() << "setEventIndex " << Name << " -> " << Index << "\n");
  assert(EventIndex == INVALID_INDEX);
  EventIndex = Index;
}

bool EventSymbol::hasEventIndex() const {
  if (auto *F = dyn_cast<DefinedEvent>(this))
    return F->Event->hasEventIndex();
  return EventIndex != INVALID_INDEX;
}

DefinedEvent::DefinedEvent(StringRef Name, uint32_t Flags, InputFile *File,
                           InputEvent *Event)
    : EventSymbol(Name, DefinedEventKind, Flags, File,
                  Event ? &Event->getType() : nullptr,
                  Event ? &Event->Signature : nullptr),
      Event(Event) {}

const OutputSectionSymbol *SectionSymbol::getOutputSectionSymbol() const {
  assert(Section->OutputSec && Section->OutputSec->SectionSym);
  return Section->OutputSec->SectionSym;
}

void LazySymbol::fetch() { cast<ArchiveFile>(File)->addMember(&ArchiveSymbol); }

std::string lld::toString(const wasm::Symbol &Sym) {
  return lld::maybeDemangleSymbol(Sym.getName());
}

std::string lld::maybeDemangleSymbol(StringRef Name) {
  if (Config->Demangle)
    if (Optional<std::string> S = demangleItanium(Name))
      return *S;
  return Name;
}

std::string lld::toString(wasm::Symbol::Kind Kind) {
  switch (Kind) {
  case wasm::Symbol::DefinedFunctionKind:
    return "DefinedFunction";
  case wasm::Symbol::DefinedDataKind:
    return "DefinedData";
  case wasm::Symbol::DefinedGlobalKind:
    return "DefinedGlobal";
  case wasm::Symbol::DefinedEventKind:
    return "DefinedEvent";
  case wasm::Symbol::UndefinedFunctionKind:
    return "UndefinedFunction";
  case wasm::Symbol::UndefinedDataKind:
    return "UndefinedData";
  case wasm::Symbol::UndefinedGlobalKind:
    return "UndefinedGlobal";
  case wasm::Symbol::LazyKind:
    return "LazyKind";
  case wasm::Symbol::SectionKind:
    return "SectionKind";
  case wasm::Symbol::OutputSectionKind:
    return "OutputSectionKind";
  }
  llvm_unreachable("invalid symbol kind");
}


void lld::wasm::printTraceSymbolUndefined(StringRef Name, const InputFile* File) {
  message(toString(File) + ": reference to " + Name);
}

// Print out a log message for --trace-symbol.
void lld::wasm::printTraceSymbol(Symbol *Sym) {
  // Undefined symbols are traced via printTraceSymbolUndefined
  if (Sym->isUndefined())
    return;

  std::string S;
  if (Sym->isLazy())
    S = ": lazy definition of ";
  else
    S = ": definition of ";

  message(toString(Sym->getFile()) + S + Sym->getName());
}

const char *lld::wasm::DefaultModule = "env";
const char *lld::wasm::FunctionTableName = "__indirect_function_table";
