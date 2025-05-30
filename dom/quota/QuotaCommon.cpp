/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/quota/QuotaCommon.h"

#ifdef QM_ERROR_STACKS_ENABLED
#  include "base/process_util.h"
#endif
#include "mozIStorageConnection.h"
#include "mozIStorageStatement.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Logging.h"
#include "mozilla/TextUtils.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/dom/quota/ScopedLogExtraInfo.h"
#include "mozilla/glean/DomQuotaMetrics.h"
#include "nsIConsoleService.h"
#include "nsIFile.h"
#include "nsServiceManagerUtils.h"
#include "nsStringFlags.h"
#include "nsTStringRepr.h"
#include "nsUnicharUtils.h"
#include "nsXPCOM.h"
#include "nsXULAppAPI.h"

#ifdef XP_WIN
#  include "mozilla/Atomics.h"
#  include "mozilla/ipc/BackgroundParent.h"
#  include "mozilla/StaticPrefs_dom.h"
#  include "nsILocalFileWin.h"
#endif

namespace mozilla {

RefPtr<BoolPromise> CreateAndRejectBoolPromise(StaticString aFunc,
                                               nsresult aRv) {
  return CreateAndRejectMozPromise<BoolPromise>(aFunc, aRv);
}

RefPtr<Int64Promise> CreateAndRejectInt64Promise(StaticString aFunc,
                                                 nsresult aRv) {
  return CreateAndRejectMozPromise<Int64Promise>(aFunc, aRv);
}

RefPtr<BoolPromise> CreateAndRejectBoolPromiseFromQMResult(
    StaticString aFunc, const QMResult& aRv) {
  return CreateAndRejectMozPromise<BoolPromise>(aFunc, aRv);
}

namespace dom::quota {

namespace {

#ifdef DEBUG
constexpr auto kDSStoreFileName = u".DS_Store"_ns;
constexpr auto kDesktopFileName = u".desktop"_ns;
constexpr auto kDesktopIniFileName = u"desktop.ini"_ns;
constexpr auto kThumbsDbFileName = u"thumbs.db"_ns;
#endif

#ifdef XP_WIN
Atomic<int32_t> gUseDOSDevicePathSyntax(-1);
#endif

LazyLogModule gLogger("QuotaManager");

void AnonymizeCString(nsACString& aCString, uint32_t aStart) {
  MOZ_ASSERT(!aCString.IsEmpty());
  MOZ_ASSERT(aStart < aCString.Length());

  char* iter = aCString.BeginWriting() + aStart;
  char* end = aCString.EndWriting();

  while (iter != end) {
    char c = *iter;

    if (IsAsciiAlpha(c)) {
      *iter = 'a';
    } else if (IsAsciiDigit(c)) {
      *iter = 'D';
    }

    ++iter;
  }
}

}  // namespace

const char kQuotaGenericDelimiter = '|';

#ifdef NIGHTLY_BUILD
const nsLiteralCString kQuotaInternalError = "internal"_ns;
const nsLiteralCString kQuotaExternalError = "external"_ns;
#endif

LogModule* GetQuotaManagerLogger() { return gLogger; }

void AnonymizeCString(nsACString& aCString) {
  if (aCString.IsEmpty()) {
    return;
  }
  AnonymizeCString(aCString, /* aStart */ 0);
}

void AnonymizeOriginString(nsACString& aOriginString) {
  if (aOriginString.IsEmpty()) {
    return;
  }

  int32_t start = aOriginString.FindChar(':');
  if (start < 0) {
    start = 0;
  }

  AnonymizeCString(aOriginString, start);
}

#ifdef XP_WIN
void CacheUseDOSDevicePathSyntaxPrefValue() {
  MOZ_ASSERT(XRE_IsParentProcess());
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (gUseDOSDevicePathSyntax == -1) {
    bool useDOSDevicePathSyntax =
        StaticPrefs::dom_quotaManager_useDOSDevicePathSyntax_DoNotUseDirectly();
    gUseDOSDevicePathSyntax = useDOSDevicePathSyntax ? 1 : 0;
  }
}
#endif

Result<nsCOMPtr<nsIFile>, nsresult> QM_NewLocalFile(const nsAString& aPath) {
  QM_TRY_UNWRAP(
      auto file,
      MOZ_TO_RESULT_INVOKE_TYPED(nsCOMPtr<nsIFile>, NS_NewLocalFile, aPath),
      QM_PROPAGATE, [&aPath](const nsresult rv) {
        QM_WARNING("Failed to construct a file for path (%s)",
                   NS_ConvertUTF16toUTF8(aPath).get());
      });

#ifdef XP_WIN
  MOZ_ASSERT(gUseDOSDevicePathSyntax != -1);

  if (gUseDOSDevicePathSyntax) {
    QM_TRY_INSPECT(
        const auto& winFile,
        MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<nsILocalFileWin>,
                                MOZ_SELECT_OVERLOAD(do_QueryInterface), file));

    MOZ_ASSERT(winFile);
    winFile->SetUseDOSDevicePathSyntax(true);
  }
#endif

  return file;
}

nsDependentCSubstring GetLeafName(const nsACString& aPath) {
  nsACString::const_iterator start, end;
  aPath.BeginReading(start);
  aPath.EndReading(end);

  bool found = RFindInReadable("/"_ns, start, end);
  if (found) {
    start = end;
  }

  aPath.EndReading(end);

  return nsDependentCSubstring(start.get(), end.get());
}

Result<nsCOMPtr<nsIFile>, nsresult> CloneFileAndAppend(
    nsIFile& aDirectory, const nsAString& aPathElement) {
  QM_TRY_UNWRAP(auto resultFile, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                     nsCOMPtr<nsIFile>, aDirectory, Clone));

  QM_TRY(MOZ_TO_RESULT(resultFile->Append(aPathElement)));

  return resultFile;
}

Result<nsIFileKind, nsresult> GetDirEntryKind(nsIFile& aFile) {
  // Callers call this function without checking if the directory already
  // exists (idempotent usage). QM_OR_ELSE_WARN_IF is not used here since we
  // just want to log NS_ERROR_FILE_NOT_FOUND and NS_ERROR_FILE_FS_CORRUPTED
  // results and not spam the reports.
  QM_TRY_RETURN(QM_OR_ELSE_LOG_VERBOSE_IF(
      MOZ_TO_RESULT_INVOKE_MEMBER(aFile, IsDirectory)
          .map([](const bool isDirectory) {
            return isDirectory ? nsIFileKind::ExistsAsDirectory
                               : nsIFileKind::ExistsAsFile;
          }),
      ([](const nsresult rv) {
        return rv == NS_ERROR_FILE_NOT_FOUND ||
               // We treat NS_ERROR_FILE_FS_CORRUPTED as if the file did not
               // exist at all.
               rv == NS_ERROR_FILE_FS_CORRUPTED;
      }),
      ErrToOk<nsIFileKind::DoesNotExist>));
}

Result<nsCOMPtr<mozIStorageStatement>, nsresult> CreateStatement(
    mozIStorageConnection& aConnection, const nsACString& aStatementString) {
  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
      nsCOMPtr<mozIStorageStatement>, aConnection, CreateStatement,
      aStatementString));
}

template <SingleStepResult ResultHandling>
Result<SingleStepSuccessType<ResultHandling>, nsresult> ExecuteSingleStep(
    nsCOMPtr<mozIStorageStatement>&& aStatement) {
  QM_TRY_INSPECT(const bool& hasResult,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aStatement, ExecuteStep));

  if constexpr (ResultHandling == SingleStepResult::AssertHasResult) {
    MOZ_ASSERT(hasResult);
    (void)hasResult;

    return WrapNotNullUnchecked(std::move(aStatement));
  } else {
    return hasResult ? std::move(aStatement) : nullptr;
  }
}

template Result<SingleStepSuccessType<SingleStepResult::AssertHasResult>,
                nsresult>
ExecuteSingleStep<SingleStepResult::AssertHasResult>(
    nsCOMPtr<mozIStorageStatement>&&);

template Result<SingleStepSuccessType<SingleStepResult::ReturnNullIfNoResult>,
                nsresult>
ExecuteSingleStep<SingleStepResult::ReturnNullIfNoResult>(
    nsCOMPtr<mozIStorageStatement>&&);

template <SingleStepResult ResultHandling>
Result<SingleStepSuccessType<ResultHandling>, nsresult>
CreateAndExecuteSingleStepStatement(mozIStorageConnection& aConnection,
                                    const nsACString& aStatementString) {
  QM_TRY_UNWRAP(auto stmt, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                               nsCOMPtr<mozIStorageStatement>, aConnection,
                               CreateStatement, aStatementString));

  return ExecuteSingleStep<ResultHandling>(std::move(stmt));
}

template Result<SingleStepSuccessType<SingleStepResult::AssertHasResult>,
                nsresult>
CreateAndExecuteSingleStepStatement<SingleStepResult::AssertHasResult>(
    mozIStorageConnection& aConnection, const nsACString& aStatementString);

template Result<SingleStepSuccessType<SingleStepResult::ReturnNullIfNoResult>,
                nsresult>
CreateAndExecuteSingleStepStatement<SingleStepResult::ReturnNullIfNoResult>(
    mozIStorageConnection& aConnection, const nsACString& aStatementString);

namespace detail {

// Given aPath of /foo/bar/baz and aRelativePath of /bar/baz, returns the
// absolute portion of aPath /foo by removing the common suffix from aPath.
nsDependentCSubstring GetTreeBase(const nsLiteralCString& aPath,
                                  const nsLiteralCString& aRelativePath) {
  MOZ_ASSERT(StringEndsWith(aPath, aRelativePath));
  return Substring(aPath, 0, aPath.Length() - aRelativePath.Length());
}

nsDependentCSubstring GetSourceTreeBase() {
  static constexpr auto thisSourceFileRelativePath =
      "/dom/quota/QuotaCommon.cpp"_ns;

  return GetTreeBase(nsLiteralCString(__FILE__), thisSourceFileRelativePath);
}

nsDependentCSubstring GetObjdirDistIncludeTreeBase(
    const nsLiteralCString& aQuotaCommonHPath) {
  static constexpr auto quotaCommonHSourceFileRelativePath =
      "/mozilla/dom/quota/QuotaCommon.h"_ns;

  return GetTreeBase(aQuotaCommonHPath, quotaCommonHSourceFileRelativePath);
}

static constexpr auto kSourceFileRelativePathMap =
    std::array<std::pair<nsLiteralCString, nsLiteralCString>, 1>{
        {{"mozilla/dom/LocalStorageCommon.h"_ns,
          "dom/localstorage/LocalStorageCommon.h"_ns}}};

nsDependentCSubstring MakeSourceFileRelativePath(
    const nsACString& aSourceFilePath) {
  static constexpr auto error = "ERROR"_ns;
  static constexpr auto mozillaRelativeBase = "mozilla/"_ns;

  static const auto sourceTreeBase = GetSourceTreeBase();

  if (MOZ_LIKELY(StringBeginsWith(aSourceFilePath, sourceTreeBase))) {
    return Substring(aSourceFilePath, sourceTreeBase.Length() + 1);
  }

  // The source file could have been exported to the OBJDIR/dist/include
  // directory, so we need to check that case as well.
  static const auto objdirDistIncludeTreeBase = GetObjdirDistIncludeTreeBase();

  if (MOZ_LIKELY(
          StringBeginsWith(aSourceFilePath, objdirDistIncludeTreeBase))) {
    const auto sourceFileRelativePath =
        Substring(aSourceFilePath, objdirDistIncludeTreeBase.Length() + 1);

    // Exported source files don't have to use the same directory structure as
    // original source files. Check if we have a mapping for the exported
    // source file.
    const auto foundIt = std::find_if(
        kSourceFileRelativePathMap.cbegin(), kSourceFileRelativePathMap.cend(),
        [&sourceFileRelativePath](const auto& entry) {
          return entry.first == sourceFileRelativePath;
        });

    if (MOZ_UNLIKELY(foundIt != kSourceFileRelativePathMap.cend())) {
      return Substring(foundIt->second, 0);
    }

    // If we don't have a mapping for it, just remove the mozilla/ prefix
    // (if there's any).
    if (MOZ_LIKELY(
            StringBeginsWith(sourceFileRelativePath, mozillaRelativeBase))) {
      return Substring(sourceFileRelativePath, mozillaRelativeBase.Length());
    }

    // At this point, we don't know how to transform the relative path of the
    // exported source file back to the relative path of the original source
    // file. This can happen when QM_TRY is used in an exported nsIFoo.h file.
    // If you really need to use QM_TRY there, consider adding a new mapping
    // for the exported source file.
    return sourceFileRelativePath;
  }

  nsCString::const_iterator begin, end;
  if (RFindInReadable("/"_ns, aSourceFilePath.BeginReading(begin),
                      aSourceFilePath.EndReading(end))) {
    // Use the basename as a fallback, to avoid exposing any user parts of the
    // path.
    ++begin;
    return Substring(begin, aSourceFilePath.EndReading(end));
  }

  return nsDependentCSubstring{static_cast<mozilla::Span<const char>>(
      static_cast<const nsCString&>(error))};
}

}  // namespace detail

#ifdef QM_LOG_ERROR_ENABLED
#  ifdef QM_ERROR_STACKS_ENABLED
void LogError(const nsACString& aExpr, const ResultType& aResult,
              const nsACString& aSourceFilePath, const int32_t aSourceFileLine,
              const Severity aSeverity)
#  else
void LogError(const nsACString& aExpr, const Maybe<nsresult> aMaybeRv,
              const nsACString& aSourceFilePath, const int32_t aSourceFileLine,
              const Severity aSeverity)
#  endif
{
  // TODO: Add MOZ_LOG support, bug 1711661.

  // We have to ignore failures with the Verbose severity until we have support
  // for MOZ_LOG.
  if (aSeverity == Severity::Verbose) {
    return;
  }

  const Tainted<nsCString>* contextTaintedPtr = nullptr;

#  ifdef QM_SCOPED_LOG_EXTRA_INFO_ENABLED
  const auto& extraInfoMap = ScopedLogExtraInfo::GetExtraInfoMap();

  if (const auto contextIt =
          extraInfoMap.find(ScopedLogExtraInfo::kTagContextTainted);
      contextIt != extraInfoMap.cend()) {
    contextTaintedPtr = contextIt->second;
  }
#  endif

  const auto severityString = [&aSeverity]() -> nsLiteralCString {
    switch (aSeverity) {
      case Severity::Error:
        return "ERROR"_ns;
      case Severity::Warning:
        return "WARNING"_ns;
      case Severity::Info:
        return "INFO"_ns;
      case Severity::Verbose:
        return "VERBOSE"_ns;
    }
    MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Bad severity value!");
  }();

  Maybe<nsresult> maybeRv;

#  ifdef QM_ERROR_STACKS_ENABLED
  if (aResult.is<QMResult>()) {
    maybeRv = Some(aResult.as<QMResult>().NSResult());
  } else if (aResult.is<nsresult>()) {
    maybeRv = Some(aResult.as<nsresult>());
  }
#  else
  maybeRv = aMaybeRv;
#  endif

  nsAutoCString rvCode;
  nsAutoCString rvName;

  if (maybeRv) {
    nsresult rv = *maybeRv;

    // Ignore this special error code, as it's an expected failure in certain
    // cases, especially preloading of datastores for LSNG. See the related
    // comment in InitializeTemporaryClientOp::DoDirectoryWork.
    //
    // Note: For now, this simple check is sufficient. However, if more cases
    // like this are added in the future, it may be worth introducing a more
    // structured system for handling expected errors.
    if (rv == NS_ERROR_DOM_QM_CLIENT_INIT_ORIGIN_UNINITIALIZED) {
      return;
    }

    rvCode = nsPrintfCString("0x%" PRIX32, static_cast<uint32_t>(rv));

    // XXX NS_ERROR_MODULE_WIN32 should be handled in GetErrorName directly.
    if (NS_ERROR_GET_MODULE(rv) == NS_ERROR_MODULE_WIN32) {
      // XXX We could also try to get the Win32 error name here.
      rvName = nsPrintfCString(
          "NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_WIN32, 0x%" PRIX16 ")",
          NS_ERROR_GET_CODE(rv));
    } else {
      mozilla::GetErrorName(rv, rvName);
    }
  }

#  ifdef QM_ERROR_STACKS_ENABLED
  nsAutoCString frameIdString;
  nsAutoCString stackIdString;
  nsAutoCString processIdString;

  if (aResult.is<QMResult>()) {
    const QMResult& result = aResult.as<QMResult>();
    frameIdString = IntToCString(result.FrameId());
    stackIdString = IntToCString(result.StackId());
    processIdString =
        IntToCString(static_cast<uint32_t>(base::GetCurrentProcId()));
  }
#  endif

  auto extraInfosStringTainted = Tainted<nsAutoCString>([&] {
    nsAutoCString extraInfosString;

    if (!rvCode.IsEmpty()) {
      extraInfosString.Append(" failed with resultCode "_ns + rvCode);
    }

    if (!rvName.IsEmpty()) {
      extraInfosString.Append(", resultName "_ns + rvName);
    }

#  ifdef QM_ERROR_STACKS_ENABLED
    if (!frameIdString.IsEmpty()) {
      extraInfosString.Append(", frameId "_ns + frameIdString);
    }

    if (!stackIdString.IsEmpty()) {
      extraInfosString.Append(", stackId "_ns + stackIdString);
    }

    if (!processIdString.IsEmpty()) {
      extraInfosString.Append(", processId "_ns + processIdString);
    }
#  endif

#  ifdef QM_SCOPED_LOG_EXTRA_INFO_ENABLED
    for (const auto& item : extraInfoMap) {
      const auto& valueTainted = *item.second;

      extraInfosString.Append(
          ", "_ns + nsDependentCString(item.first) + " "_ns +
          MOZ_NO_VALIDATE(valueTainted,
                          "It's okay to append any `extraInfoMap` value to "
                          "`extraInfosString`."));
    }
#  endif

    return extraInfosString;
  }());

  const auto sourceFileRelativePath =
      detail::MakeSourceFileRelativePath(aSourceFilePath);

#  ifdef QM_LOG_ERROR_TO_CONSOLE_ENABLED
  NS_DebugBreak(
      NS_DEBUG_WARNING,
      nsAutoCString("QM_TRY failure ("_ns + severityString + ")"_ns).get(),
      (MOZ_NO_VALIDATE(extraInfosStringTainted,
                       "It's okay to check if `extraInfosString` is empty.")
               .IsEmpty()
           ? nsPromiseFlatCString(aExpr)
           : static_cast<const nsCString&>(nsAutoCString(
                 aExpr + MOZ_NO_VALIDATE(extraInfosStringTainted,
                                         "It's okay to log `extraInfosString` "
                                         "to stdout/console."))))
          .get(),
      nsPromiseFlatCString(sourceFileRelativePath).get(), aSourceFileLine);
#  endif

#  ifdef QM_LOG_ERROR_TO_BROWSER_CONSOLE_ENABLED
  // XXX We might want to allow reporting to the browsing console even when
  // there's no context in future once we are sure that it can't spam the
  // browser console or when we have special about:quotamanager for the
  // reporting (instead of the browsing console).
  // Another option is to keep the current check and rely on MOZ_LOG reporting
  // in future once that's available.
  if (contextTaintedPtr) {
    nsCOMPtr<nsIConsoleService> console =
        do_GetService(NS_CONSOLESERVICE_CONTRACTID);
    if (console) {
      NS_ConvertUTF8toUTF16 message(
          "QM_TRY failure ("_ns + severityString + ")"_ns + ": '"_ns + aExpr +
          MOZ_NO_VALIDATE(
              extraInfosStringTainted,
              "It's okay to log `extraInfosString` to the browser console.") +
          "', file "_ns + sourceFileRelativePath + ":"_ns +
          IntToCString(aSourceFileLine));

      // The concatenation above results in a message like:
      // QM_TRY failure (ERROR): 'MaybeRemoveLocalStorageArchiveTmpFile() failed
      // with resultCode 0x80004005, resultName NS_ERROR_FAILURE, frameId 1,
      // stackId 1, processId 53978, context Initialization::Storage', file
      // dom/quota/ActorsParent.cpp:6029

      console->LogStringMessage(message.get());
    }
  }
#  endif

#  ifdef QM_LOG_ERROR_TO_TELEMETRY_ENABLED
  // The context tag is special because it's used to enable logging to
  // telemetry (besides carrying information). Other tags (like query) don't
  // enable logging to telemetry.

  if (contextTaintedPtr) {
    const auto& contextTainted = *contextTaintedPtr;

    // Do NOT CHANGE this if you don't know what you're doing.

    // `extraInfoString` is not included in the telemetry event on purpose
    // since it can contain sensitive information.

    // For now, we don't include aExpr in the telemetry event. It might help to
    // match locations across versions, but they might be large.

    // New extra entries (with potentially sensitive content) can't be easily
    // (accidentally) added because they would have to be added to Events.yaml
    // under "dom.quota.try" which would require a data review.

    mozilla::glean::dom_quota_try::ErrorStepExtra extra;
    extra.context = Some(MOZ_NO_VALIDATE(
        contextTainted,
        "Context has been data-reviewed for telemetry transmission."));

#    ifdef QM_ERROR_STACKS_ENABLED
    if (!frameIdString.IsEmpty()) {
      extra.frameId = Some(frameIdString);
    }

    if (!processIdString.IsEmpty()) {
      extra.processId = Some(processIdString);
    }
#    endif

    if (!rvName.IsEmpty()) {
      extra.result = Some(rvName);
    }

    // Here, we are generating thread local sequence number and thread Id
    // information which could be useful for summarizing and categorizing
    // log statistics in QM_TRY stack propagation scripts. Since, this is
    // a thread local object, we do not need to worry about data races.
    static MOZ_THREAD_LOCAL(uint32_t) sSequenceNumber;

    // This would be initialized once, all subsequent calls would be a no-op.
    MOZ_ALWAYS_TRUE(sSequenceNumber.init());

    // sequence number should always starts at number 1.
    // `sSequenceNumber` gets initialized to 0; so we have to increment here.
    const auto newSeqNum = sSequenceNumber.get() + 1;
    const auto threadId = baseprofiler::profiler_current_thread_id().ToNumber();

    const auto threadIdAndSequence =
        (static_cast<uint64_t>(threadId) << 32) | (newSeqNum & 0xFFFFFFFF);

    extra.seq = Some(threadIdAndSequence);

    sSequenceNumber.set(newSeqNum);

    extra.severity = Some(severityString);

    extra.sourceFile = Some(sourceFileRelativePath);

    extra.sourceLine = Some(aSourceFileLine);

#    ifdef QM_ERROR_STACKS_ENABLED
    if (!stackIdString.IsEmpty()) {
      extra.stackId = Some(stackIdString);
    }
#    endif

    glean::dom_quota_try::error_step.Record(Some(extra));
  }
#  endif
}
#endif

#ifdef DEBUG
Result<bool, nsresult> WarnIfFileIsUnknown(nsIFile& aFile,
                                           const char* aSourceFilePath,
                                           const int32_t aSourceFileLine) {
  nsString leafName;
  nsresult rv = aFile.GetLeafName(leafName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Err(rv);
  }

  bool isDirectory;
  rv = aFile.IsDirectory(&isDirectory);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Err(rv);
  }

  if (!isDirectory) {
    // Don't warn about OS metadata files. These files are only used in
    // different platforms, but the profile can be shared across different
    // operating systems, so we check it on all platforms.
    if (leafName.Equals(kDSStoreFileName) ||
        leafName.Equals(kDesktopFileName) ||
        leafName.Equals(kDesktopIniFileName,
                        nsCaseInsensitiveStringComparator) ||
        leafName.Equals(kThumbsDbFileName, nsCaseInsensitiveStringComparator)) {
      return false;
    }

    // Don't warn about files starting with ".".
    if (leafName.First() == char16_t('.')) {
      return false;
    }
  }

  NS_DebugBreak(
      NS_DEBUG_WARNING,
      nsPrintfCString("Something (%s) in the directory that doesn't belong!",
                      NS_ConvertUTF16toUTF8(leafName).get())
          .get(),
      nullptr, aSourceFilePath, aSourceFileLine);

  return true;
}
#endif

}  // namespace dom::quota
}  // namespace mozilla
