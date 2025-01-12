//===-- Background.cpp - Build an index in a background thread ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "index/Background.h"
#include "ClangdUnit.h"
#include "Compiler.h"
#include "Headers.h"
#include "Logger.h"
#include "Path.h"
#include "SourceCode.h"
#include "Symbol.h"
#include "Threading.h"
#include "Trace.h"
#include "URI.h"
#include "index/IndexAction.h"
#include "index/MemIndex.h"
#include "index/Ref.h"
#include "index/Relation.h"
#include "index/Serialization.h"
#include "index/SymbolCollector.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/Threading.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <thread>

namespace clang {
namespace clangd {
namespace {

static std::atomic<bool> PreventStarvation = {false};

// Resolves URI to file paths with cache.
class URIToFileCache {
public:
  URIToFileCache(llvm::StringRef HintPath) : HintPath(HintPath) {}

  llvm::StringRef resolve(llvm::StringRef FileURI) {
    auto I = URIToPathCache.try_emplace(FileURI);
    if (I.second) {
      auto U = URI::parse(FileURI);
      if (!U) {
        elog("Failed to parse URI {0}: {1}", FileURI, U.takeError());
        assert(false && "Failed to parse URI");
        return "";
      }
      auto Path = URI::resolve(*U, HintPath);
      if (!Path) {
        elog("Failed to resolve URI {0}: {1}", FileURI, Path.takeError());
        assert(false && "Failed to resolve URI");
        return "";
      }
      I.first->second = *Path;
    }
    return I.first->second;
  }

private:
  std::string HintPath;
  llvm::StringMap<std::string> URIToPathCache;
};

// We keep only the node "U" and its edges. Any node other than "U" will be
// empty in the resultant graph.
IncludeGraph getSubGraph(const URI &U, const IncludeGraph &FullGraph) {
  IncludeGraph IG;

  std::string FileURI = U.toString();
  auto Entry = IG.try_emplace(FileURI).first;
  auto &Node = Entry->getValue();
  Node = FullGraph.lookup(Entry->getKey());
  Node.URI = Entry->getKey();

  // URIs inside nodes must point into the keys of the same IncludeGraph.
  for (auto &Include : Node.DirectIncludes) {
    auto I = IG.try_emplace(Include).first;
    I->getValue().URI = I->getKey();
    Include = I->getKey();
  }

  return IG;
}

// We cannot use vfs->makeAbsolute because Cmd.FileName is either absolute or
// relative to Cmd.Directory, which might not be the same as current working
// directory.
llvm::SmallString<128> getAbsolutePath(const tooling::CompileCommand &Cmd) {
  llvm::SmallString<128> AbsolutePath;
  if (llvm::sys::path::is_absolute(Cmd.Filename)) {
    AbsolutePath = Cmd.Filename;
  } else {
    AbsolutePath = Cmd.Directory;
    llvm::sys::path::append(AbsolutePath, Cmd.Filename);
    llvm::sys::path::remove_dots(AbsolutePath, true);
  }
  return AbsolutePath;
}
} // namespace

BackgroundIndex::BackgroundIndex(
    Context BackgroundContext, const FileSystemProvider &FSProvider,
    const GlobalCompilationDatabase &CDB,
    BackgroundIndexStorage::Factory IndexStorageFactory,
    size_t BuildIndexPeriodMs, size_t ThreadPoolSize)
    : SwapIndex(llvm::make_unique<MemIndex>()), FSProvider(FSProvider),
      CDB(CDB), BackgroundContext(std::move(BackgroundContext)),
      BuildIndexPeriodMs(BuildIndexPeriodMs),
      SymbolsUpdatedSinceLastIndex(false),
      IndexStorageFactory(std::move(IndexStorageFactory)),
      CommandsChanged(
          CDB.watch([&](const std::vector<std::string> &ChangedFiles) {
            enqueue(ChangedFiles);
          })) {
  assert(ThreadPoolSize > 0 && "Thread pool size can't be zero.");
  assert(this->IndexStorageFactory && "Storage factory can not be null!");
  for (unsigned I = 0; I < ThreadPoolSize; ++I) {
    ThreadPool.runAsync("background-worker-" + llvm::Twine(I + 1),
                        [this] { run(); });
  }
  if (BuildIndexPeriodMs > 0) {
    log("BackgroundIndex: build symbol index periodically every {0} ms.",
        BuildIndexPeriodMs);
    ThreadPool.runAsync("background-index-builder", [this] { buildIndex(); });
  }
}

BackgroundIndex::~BackgroundIndex() {
  stop();
  ThreadPool.wait();
}

void BackgroundIndex::stop() {
  {
    std::lock_guard<std::mutex> QueueLock(QueueMu);
    std::lock_guard<std::mutex> IndexLock(IndexMu);
    ShouldStop = true;
  }
  QueueCV.notify_all();
  IndexCV.notify_all();
}

void BackgroundIndex::run() {
  WithContext Background(BackgroundContext.clone());
  while (true) {
    llvm::Optional<Task> Task;
    llvm::ThreadPriority Priority;
    {
      std::unique_lock<std::mutex> Lock(QueueMu);
      QueueCV.wait(Lock, [&] { return ShouldStop || !Queue.empty(); });
      if (ShouldStop) {
        Queue.clear();
        QueueCV.notify_all();
        return;
      }
      ++NumActiveTasks;
      std::tie(Task, Priority) = std::move(Queue.front());
      Queue.pop_front();
    }

    if (Priority != llvm::ThreadPriority::Default && !PreventStarvation.load())
      llvm::set_thread_priority(Priority);
    (*Task)();
    if (Priority != llvm::ThreadPriority::Default)
      llvm::set_thread_priority(llvm::ThreadPriority::Default);

    {
      std::unique_lock<std::mutex> Lock(QueueMu);
      assert(NumActiveTasks > 0 && "before decrementing");
      --NumActiveTasks;
    }
    QueueCV.notify_all();
  }
}

bool BackgroundIndex::blockUntilIdleForTest(
    llvm::Optional<double> TimeoutSeconds) {
  std::unique_lock<std::mutex> Lock(QueueMu);
  return wait(Lock, QueueCV, timeoutSeconds(TimeoutSeconds),
              [&] { return Queue.empty() && NumActiveTasks == 0; });
}

void BackgroundIndex::enqueue(const std::vector<std::string> &ChangedFiles) {
  enqueueTask(
      [this, ChangedFiles] {
        trace::Span Tracer("BackgroundIndexEnqueue");
        // We're doing this asynchronously, because we'll read shards here too.
        log("Enqueueing {0} commands for indexing", ChangedFiles.size());
        SPAN_ATTACH(Tracer, "files", int64_t(ChangedFiles.size()));

        auto NeedsReIndexing = loadShards(std::move(ChangedFiles));
        // Run indexing for files that need to be updated.
        std::shuffle(NeedsReIndexing.begin(), NeedsReIndexing.end(),
                     std::mt19937(std::random_device{}()));
        for (auto &Elem : NeedsReIndexing)
          enqueue(std::move(Elem.first), Elem.second);
      },
      llvm::ThreadPriority::Default);
}

void BackgroundIndex::enqueue(tooling::CompileCommand Cmd,
                              BackgroundIndexStorage *Storage) {
  enqueueTask(Bind(
                  [this, Storage](tooling::CompileCommand Cmd) {
                    // We can't use llvm::StringRef here since we are going to
                    // move from Cmd during the call below.
                    const std::string FileName = Cmd.Filename;
                    if (auto Error = index(std::move(Cmd), Storage))
                      elog("Indexing {0} failed: {1}", FileName,
                           std::move(Error));
                  },
                  std::move(Cmd)),
              llvm::ThreadPriority::Background);
}

void BackgroundIndex::enqueueTask(Task T, llvm::ThreadPriority Priority) {
  {
    std::lock_guard<std::mutex> Lock(QueueMu);
    auto I = Queue.end();
    // We first store the tasks with Normal priority in the front of the queue.
    // Then we store low priority tasks. Normal priority tasks are pretty rare,
    // they should not grow beyond single-digit numbers, so it is OK to do
    // linear search and insert after that.
    if (Priority == llvm::ThreadPriority::Default) {
      I = llvm::find_if(
          Queue, [](const std::pair<Task, llvm::ThreadPriority> &Elem) {
            return Elem.second == llvm::ThreadPriority::Background;
          });
    }
    Queue.insert(I, {std::move(T), Priority});
  }
  QueueCV.notify_all();
}

/// Given index results from a TU, only update symbols coming from files that
/// are different or missing from than \p ShardVersionsSnapshot. Also stores new
/// index information on IndexStorage.
void BackgroundIndex::update(
    llvm::StringRef MainFile, IndexFileIn Index,
    const llvm::StringMap<ShardVersion> &ShardVersionsSnapshot,
    BackgroundIndexStorage *IndexStorage, bool HadErrors) {
  // Partition symbols/references into files.
  struct File {
    llvm::DenseSet<const Symbol *> Symbols;
    llvm::DenseSet<const Ref *> Refs;
    llvm::DenseSet<const Relation *> Relations;
    FileDigest Digest;
  };
  llvm::StringMap<File> Files;
  URIToFileCache URICache(MainFile);
  for (const auto &IndexIt : *Index.Sources) {
    const auto &IGN = IndexIt.getValue();
    // Note that sources do not contain any information regarding missing
    // headers, since we don't even know what absolute path they should fall in.
    const auto AbsPath = URICache.resolve(IGN.URI);
    const auto DigestIt = ShardVersionsSnapshot.find(AbsPath);
    // File has different contents, or indexing was successfull this time.
    if (DigestIt == ShardVersionsSnapshot.end() ||
        DigestIt->getValue().Digest != IGN.Digest ||
        (DigestIt->getValue().HadErrors && !HadErrors))
      Files.try_emplace(AbsPath).first->getValue().Digest = IGN.Digest;
  }
  // This map is used to figure out where to store relations.
  llvm::DenseMap<SymbolID, File *> SymbolIDToFile;
  for (const auto &Sym : *Index.Symbols) {
    if (Sym.CanonicalDeclaration) {
      auto DeclPath = URICache.resolve(Sym.CanonicalDeclaration.FileURI);
      const auto FileIt = Files.find(DeclPath);
      if (FileIt != Files.end()) {
        FileIt->second.Symbols.insert(&Sym);
        SymbolIDToFile[Sym.ID] = &FileIt->second;
      }
    }
    // For symbols with different declaration and definition locations, we store
    // the full symbol in both the header file and the implementation file, so
    // that merging can tell the preferred symbols (from canonical headers) from
    // other symbols (e.g. forward declarations).
    if (Sym.Definition &&
        Sym.Definition.FileURI != Sym.CanonicalDeclaration.FileURI) {
      auto DefPath = URICache.resolve(Sym.Definition.FileURI);
      const auto FileIt = Files.find(DefPath);
      if (FileIt != Files.end())
        FileIt->second.Symbols.insert(&Sym);
    }
  }
  llvm::DenseMap<const Ref *, SymbolID> RefToIDs;
  for (const auto &SymRefs : *Index.Refs) {
    for (const auto &R : SymRefs.second) {
      auto Path = URICache.resolve(R.Location.FileURI);
      const auto FileIt = Files.find(Path);
      if (FileIt != Files.end()) {
        auto &F = FileIt->getValue();
        RefToIDs[&R] = SymRefs.first;
        F.Refs.insert(&R);
      }
    }
  }
  for (const auto &Rel : *Index.Relations) {
    const auto FileIt = SymbolIDToFile.find(Rel.Subject);
    if (FileIt != SymbolIDToFile.end())
      FileIt->second->Relations.insert(&Rel);
  }

  // Build and store new slabs for each updated file.
  for (const auto &FileIt : Files) {
    llvm::StringRef Path = FileIt.getKey();
    SymbolSlab::Builder Syms;
    RefSlab::Builder Refs;
    RelationSlab::Builder Relations;
    for (const auto *S : FileIt.second.Symbols)
      Syms.insert(*S);
    for (const auto *R : FileIt.second.Refs)
      Refs.insert(RefToIDs[R], *R);
    for (const auto *Rel : FileIt.second.Relations)
      Relations.insert(*Rel);
    auto SS = llvm::make_unique<SymbolSlab>(std::move(Syms).build());
    auto RS = llvm::make_unique<RefSlab>(std::move(Refs).build());
    auto RelS = llvm::make_unique<RelationSlab>(std::move(Relations).build());
    auto IG = llvm::make_unique<IncludeGraph>(
        getSubGraph(URI::create(Path), Index.Sources.getValue()));

    // We need to store shards before updating the index, since the latter
    // consumes slabs.
    // FIXME: Also skip serializing the shard if it is already up-to-date.
    if (IndexStorage) {
      IndexFileOut Shard;
      Shard.Symbols = SS.get();
      Shard.Refs = RS.get();
      Shard.Relations = RelS.get();
      Shard.Sources = IG.get();

      // Only store command line hash for main files of the TU, since our
      // current model keeps only one version of a header file.
      if (Path == MainFile)
        Shard.Cmd = Index.Cmd.getPointer();

      if (auto Error = IndexStorage->storeShard(Path, Shard))
        elog("Failed to write background-index shard for file {0}: {1}", Path,
             std::move(Error));
    }

    {
      std::lock_guard<std::mutex> Lock(ShardVersionsMu);
      auto Hash = FileIt.second.Digest;
      auto DigestIt = ShardVersions.try_emplace(Path);
      ShardVersion &SV = DigestIt.first->second;
      // Skip if file is already up to date, unless previous index was broken
      // and this one is not.
      if (!DigestIt.second && SV.Digest == Hash && SV.HadErrors && !HadErrors)
        continue;
      SV.Digest = Hash;
      SV.HadErrors = HadErrors;

      // This can override a newer version that is added in another thread, if
      // this thread sees the older version but finishes later. This should be
      // rare in practice.
      IndexedSymbols.update(Path, std::move(SS), std::move(RS), std::move(RelS),
                            Path == MainFile);
    }
  }
}

void BackgroundIndex::buildIndex() {
  assert(BuildIndexPeriodMs > 0);
  while (true) {
    {
      std::unique_lock<std::mutex> Lock(IndexMu);
      if (ShouldStop) // Avoid waiting if stopped.
        break;
      // Wait until this is notified to stop or `BuildIndexPeriodMs` has past.
      IndexCV.wait_for(Lock, std::chrono::milliseconds(BuildIndexPeriodMs));
      if (ShouldStop) // Avoid rebuilding index if stopped.
        break;
    }
    if (!SymbolsUpdatedSinceLastIndex.exchange(false))
      continue;
    // There can be symbol update right after the flag is reset above and before
    // index is rebuilt below. The new index would contain the updated symbols
    // but the flag would still be true. This is fine as we would simply run an
    // extra index build.
    reset(
        IndexedSymbols.buildIndex(IndexType::Heavy, DuplicateHandling::Merge));
    log("BackgroundIndex: rebuilt symbol index with estimated memory {0} "
        "bytes.",
        estimateMemoryUsage());
  }
}

llvm::Error BackgroundIndex::index(tooling::CompileCommand Cmd,
                                   BackgroundIndexStorage *IndexStorage) {
  trace::Span Tracer("BackgroundIndex");
  SPAN_ATTACH(Tracer, "file", Cmd.Filename);
  auto AbsolutePath = getAbsolutePath(Cmd);

  auto FS = FSProvider.getFileSystem();
  auto Buf = FS->getBufferForFile(AbsolutePath);
  if (!Buf)
    return llvm::errorCodeToError(Buf.getError());
  auto Hash = digest(Buf->get()->getBuffer());

  // Take a snapshot of the versions to avoid locking for each file in the TU.
  llvm::StringMap<ShardVersion> ShardVersionsSnapshot;
  {
    std::lock_guard<std::mutex> Lock(ShardVersionsMu);
    ShardVersionsSnapshot = ShardVersions;
  }

  vlog("Indexing {0} (digest:={1})", Cmd.Filename, llvm::toHex(Hash));
  ParseInputs Inputs;
  Inputs.FS = std::move(FS);
  Inputs.FS->setCurrentWorkingDirectory(Cmd.Directory);
  Inputs.CompileCommand = std::move(Cmd);
  auto CI = buildCompilerInvocation(Inputs);
  if (!CI)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Couldn't build compiler invocation");
  IgnoreDiagnostics IgnoreDiags;
  auto Clang = prepareCompilerInstance(std::move(CI), /*Preamble=*/nullptr,
                                       std::move(*Buf), Inputs.FS, IgnoreDiags);
  if (!Clang)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Couldn't build compiler instance");

  SymbolCollector::Options IndexOpts;
  // Creates a filter to not collect index results from files with unchanged
  // digests.
  IndexOpts.FileFilter = [&ShardVersionsSnapshot](const SourceManager &SM,
                                                  FileID FID) {
    const auto *F = SM.getFileEntryForID(FID);
    if (!F)
      return false; // Skip invalid files.
    auto AbsPath = getCanonicalPath(F, SM);
    if (!AbsPath)
      return false; // Skip files without absolute path.
    auto Digest = digestFile(SM, FID);
    if (!Digest)
      return false;
    auto D = ShardVersionsSnapshot.find(*AbsPath);
    if (D != ShardVersionsSnapshot.end() && D->second.Digest == Digest &&
        !D->second.HadErrors)
      return false; // Skip files that haven't changed, without errors.
    return true;
  };

  IndexFileIn Index;
  auto Action = createStaticIndexingAction(
      IndexOpts, [&](SymbolSlab S) { Index.Symbols = std::move(S); },
      [&](RefSlab R) { Index.Refs = std::move(R); },
      [&](RelationSlab R) { Index.Relations = std::move(R); },
      [&](IncludeGraph IG) { Index.Sources = std::move(IG); });

  // We're going to run clang here, and it could potentially crash.
  // We could use CrashRecoveryContext to try to make indexing crashes nonfatal,
  // but the leaky "recovery" is pretty scary too in a long-running process.
  // If crashes are a real problem, maybe we should fork a child process.

  const FrontendInputFile &Input = Clang->getFrontendOpts().Inputs.front();
  if (!Action->BeginSourceFile(*Clang, Input))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "BeginSourceFile() failed");
  if (llvm::Error Err = Action->Execute())
    return Err;

  Action->EndSourceFile();

  Index.Cmd = Inputs.CompileCommand;
  assert(Index.Symbols && Index.Refs && Index.Sources &&
         "Symbols, Refs and Sources must be set.");

  log("Indexed {0} ({1} symbols, {2} refs, {3} files)",
      Inputs.CompileCommand.Filename, Index.Symbols->size(),
      Index.Refs->numRefs(), Index.Sources->size());
  SPAN_ATTACH(Tracer, "symbols", int(Index.Symbols->size()));
  SPAN_ATTACH(Tracer, "refs", int(Index.Refs->numRefs()));
  SPAN_ATTACH(Tracer, "sources", int(Index.Sources->size()));

  bool HadErrors = Clang->hasDiagnostics() &&
                   Clang->getDiagnostics().hasUncompilableErrorOccurred();
  if (HadErrors) {
    log("Failed to compile {0}, index may be incomplete", AbsolutePath);
    for (auto &It : *Index.Sources)
      It.second.Flags |= IncludeGraphNode::SourceFlag::HadErrors;
  }
  update(AbsolutePath, std::move(Index), ShardVersionsSnapshot, IndexStorage,
         HadErrors);

  if (BuildIndexPeriodMs > 0)
    SymbolsUpdatedSinceLastIndex = true;
  else
    reset(
        IndexedSymbols.buildIndex(IndexType::Light, DuplicateHandling::Merge));

  return llvm::Error::success();
}

std::vector<BackgroundIndex::Source>
BackgroundIndex::loadShard(const tooling::CompileCommand &Cmd,
                           BackgroundIndexStorage *IndexStorage,
                           llvm::StringSet<> &LoadedShards) {
  struct ShardInfo {
    std::string AbsolutePath;
    std::unique_ptr<IndexFileIn> Shard;
    FileDigest Digest = {};
    bool CountReferences = false;
    bool HadErrors = false;
  };
  std::vector<ShardInfo> IntermediateSymbols;
  // Make sure we don't have duplicate elements in the queue. Keys are absolute
  // paths.
  llvm::StringSet<> InQueue;
  auto FS = FSProvider.getFileSystem();
  // Dependencies of this TU, paired with the information about whether they
  // need to be re-indexed or not.
  std::vector<Source> Dependencies;
  std::queue<Source> ToVisit;
  std::string AbsolutePath = getAbsolutePath(Cmd).str();
  // Up until we load the shard related to a dependency it needs to be
  // re-indexed.
  ToVisit.emplace(AbsolutePath, true);
  InQueue.insert(AbsolutePath);
  // Goes over each dependency.
  while (!ToVisit.empty()) {
    Dependencies.push_back(std::move(ToVisit.front()));
    // Dependencies is not modified during the rest of the loop, so it is safe
    // to keep the reference.
    auto &CurDependency = Dependencies.back();
    ToVisit.pop();
    // If we have already seen this shard before(either loaded or failed) don't
    // re-try again. Since the information in the shard won't change from one TU
    // to another.
    if (!LoadedShards.try_emplace(CurDependency.Path).second) {
      // If the dependency needs to be re-indexed, first occurence would already
      // have detected that, so we don't need to issue it again.
      CurDependency.NeedsReIndexing = false;
      continue;
    }

    auto Shard = IndexStorage->loadShard(CurDependency.Path);
    if (!Shard || !Shard->Sources) {
      // File will be returned as requiring re-indexing to caller.
      vlog("Failed to load shard: {0}", CurDependency.Path);
      continue;
    }
    // These are the edges in the include graph for current dependency.
    for (const auto &I : *Shard->Sources) {
      auto U = URI::parse(I.getKey());
      if (!U)
        continue;
      auto AbsolutePath = URI::resolve(*U, CurDependency.Path);
      if (!AbsolutePath)
        continue;
      // Add file as dependency if haven't seen before.
      if (InQueue.try_emplace(*AbsolutePath).second)
        ToVisit.emplace(*AbsolutePath, true);
      // The node contains symbol information only for current file, the rest is
      // just edges.
      if (*AbsolutePath != CurDependency.Path)
        continue;

      // We found source file info for current dependency.
      assert(I.getValue().Digest != FileDigest{{0}} && "Digest is empty?");
      ShardInfo SI;
      SI.AbsolutePath = CurDependency.Path;
      SI.Shard = std::move(Shard);
      SI.Digest = I.getValue().Digest;
      SI.CountReferences =
          I.getValue().Flags & IncludeGraphNode::SourceFlag::IsTU;
      SI.HadErrors =
          I.getValue().Flags & IncludeGraphNode::SourceFlag::HadErrors;
      IntermediateSymbols.push_back(std::move(SI));
      // Check if the source needs re-indexing.
      // Get the digest, skip it if file doesn't exist.
      auto Buf = FS->getBufferForFile(CurDependency.Path);
      if (!Buf) {
        elog("Couldn't get buffer for file: {0}: {1}", CurDependency.Path,
             Buf.getError().message());
        continue;
      }
      // If digests match then dependency doesn't need re-indexing.
      // FIXME: Also check for dependencies(sources) of this shard and compile
      // commands for cache invalidation.
      CurDependency.NeedsReIndexing =
          digest(Buf->get()->getBuffer()) != I.getValue().Digest;
    }
  }
  // Load shard information into background-index.
  {
    std::lock_guard<std::mutex> Lock(ShardVersionsMu);
    // This can override a newer version that is added in another thread,
    // if this thread sees the older version but finishes later. This
    // should be rare in practice.
    for (const ShardInfo &SI : IntermediateSymbols) {
      auto SS =
          SI.Shard->Symbols
              ? llvm::make_unique<SymbolSlab>(std::move(*SI.Shard->Symbols))
              : nullptr;
      auto RS = SI.Shard->Refs
                    ? llvm::make_unique<RefSlab>(std::move(*SI.Shard->Refs))
                    : nullptr;
      auto RelS =
          SI.Shard->Relations
              ? llvm::make_unique<RelationSlab>(std::move(*SI.Shard->Relations))
              : nullptr;
      ShardVersion &SV = ShardVersions[SI.AbsolutePath];
      SV.Digest = SI.Digest;
      SV.HadErrors = SI.HadErrors;

      IndexedSymbols.update(SI.AbsolutePath, std::move(SS), std::move(RS),
                            std::move(RelS), SI.CountReferences);
    }
  }

  return Dependencies;
}

// Goes over each changed file and loads them from index. Returns the list of
// TUs that had out-of-date/no shards.
std::vector<std::pair<tooling::CompileCommand, BackgroundIndexStorage *>>
BackgroundIndex::loadShards(std::vector<std::string> ChangedFiles) {
  std::vector<std::pair<tooling::CompileCommand, BackgroundIndexStorage *>>
      NeedsReIndexing;
  // Keeps track of the files that will be reindexed, to make sure we won't
  // re-index same dependencies more than once. Keys are AbsolutePaths.
  llvm::StringSet<> FilesToIndex;
  // Keeps track of the loaded shards to make sure we don't perform redundant
  // disk IO. Keys are absolute paths.
  llvm::StringSet<> LoadedShards;
  for (const auto &File : ChangedFiles) {
    ProjectInfo PI;
    auto Cmd = CDB.getCompileCommand(File, &PI);
    if (!Cmd)
      continue;
    BackgroundIndexStorage *IndexStorage = IndexStorageFactory(PI.SourceRoot);
    auto Dependencies = loadShard(*Cmd, IndexStorage, LoadedShards);
    for (const auto &Dependency : Dependencies) {
      if (!Dependency.NeedsReIndexing || FilesToIndex.count(Dependency.Path))
        continue;
      // FIXME: Currently, we simply schedule indexing on a TU whenever any of
      // its dependencies needs re-indexing. We might do it smarter by figuring
      // out a minimal set of TUs that will cover all the stale dependencies.
      vlog("Enqueueing TU {0} because its dependency {1} needs re-indexing.",
           Cmd->Filename, Dependency.Path);
      NeedsReIndexing.push_back({std::move(*Cmd), IndexStorage});
      // Mark all of this TU's dependencies as to-be-indexed so that we won't
      // try to re-index those.
      for (const auto &Dependency : Dependencies)
        FilesToIndex.insert(Dependency.Path);
      break;
    }
  }
  vlog("Loaded all shards");
  reset(IndexedSymbols.buildIndex(IndexType::Heavy, DuplicateHandling::Merge));
  vlog("BackgroundIndex: built symbol index with estimated memory {0} "
       "bytes.",
       estimateMemoryUsage());
  return NeedsReIndexing;
}

void BackgroundIndex::preventThreadStarvationInTests() {
  PreventStarvation.store(true);
}

} // namespace clangd
} // namespace clang
