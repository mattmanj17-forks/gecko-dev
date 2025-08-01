/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/glean/bindings/TimingDistribution.h"

#include "mozilla/AppShutdown.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/dom/GleanMetricsBinding.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/glean/bindings/HistogramGIFFTMap.h"
#include "mozilla/glean/bindings/ScalarGIFFTMap.h"
#include "mozilla/glean/fog_ffi_generated.h"
#include "nsJSUtils.h"
#include "nsPrintfCString.h"
#include "nsString.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "GIFFTFwd.h"

using mozilla::TimeDuration;
using mozilla::TimeStamp;

namespace mozilla::glean {

using MetricId = uint32_t;  // Same type as in api/src/private/mod.rs
struct MetricTimerTuple {
  MetricId mMetricId;
  TimerId mTimerId;
};
class MetricTimerTupleHashKey : public PLDHashEntryHdr {
 public:
  using KeyType = const MetricTimerTuple&;
  using KeyTypePointer = const MetricTimerTuple*;

  explicit MetricTimerTupleHashKey(KeyTypePointer aKey) : mValue(*aKey) {}
  MetricTimerTupleHashKey(MetricTimerTupleHashKey&& aOther)
      : PLDHashEntryHdr(std::move(aOther)), mValue(aOther.mValue) {}
  ~MetricTimerTupleHashKey() = default;

  KeyType GetKey() const { return mValue; }
  bool KeyEquals(KeyTypePointer aKey) const {
    return aKey->mMetricId == mValue.mMetricId &&
           aKey->mTimerId == mValue.mTimerId;
  }

  static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }
  static PLDHashNumber HashKey(KeyTypePointer aKey) {
    // Chosen because this is how nsIntegralHashKey does it.
    return HashGeneric(aKey->mMetricId, aKey->mTimerId);
  }
  enum { ALLOW_MEMMOVE = true };
  static_assert(std::is_trivially_copyable_v<MetricTimerTuple>);

 private:
  const MetricTimerTuple mValue;
};

using TimerToStampMutex =
    StaticDataMutex<UniquePtr<nsTHashMap<MetricTimerTupleHashKey, TimeStamp>>>;
static Maybe<TimerToStampMutex::AutoLock> GetTimerIdToStartsLock() {
  static TimerToStampMutex sTimerIdToStarts("sTimerIdToStarts");
  auto lock = sTimerIdToStarts.Lock();
  // GIFFT will work up to the end of AppShutdownTelemetry.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMWillShutdown)) {
    return Nothing();
  }
  if (!*lock) {
    *lock = MakeUnique<nsTHashMap<MetricTimerTupleHashKey, TimeStamp>>();
    RefPtr<nsIRunnable> cleanupFn = NS_NewRunnableFunction(__func__, [&] {
      if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMWillShutdown)) {
        auto lock = sTimerIdToStarts.Lock();
        *lock = nullptr;  // deletes, see UniquePtr.h
        return;
      }
      RunOnShutdown(
          [&] {
            auto lock = sTimerIdToStarts.Lock();
            *lock = nullptr;  // deletes, see UniquePtr.h
          },
          ShutdownPhase::XPCOMWillShutdown);
    });
    // Both getting the main thread and dispatching to it can fail.
    // In that event we leak. Grab a pointer so we have something to NS_RELEASE
    // in that case.
    nsIRunnable* temp = cleanupFn.get();
    nsCOMPtr<nsIThread> mainThread;
    if (NS_FAILED(NS_GetMainThread(getter_AddRefs(mainThread))) ||
        NS_FAILED(mainThread->Dispatch(cleanupFn.forget(),
                                       nsIThread::DISPATCH_NORMAL))) {
      // Failed to dispatch cleanup routine.
      // First, un-leak the runnable (but only if we actually attempted
      // dispatch)
      if (!cleanupFn) {
        NS_RELEASE(temp);
      }
      // Next, cleanup immediately, and allow metrics to try again later.
      *lock = nullptr;
      return Nothing();
    }
  }
  return Some(std::move(lock));
}

struct MetricLabelTimerTuple {
  MetricId mMetricId;
  nsCString mLabel;
  TimerId mTimerId;
};
class MetricLabelTimerTupleHashKey : public PLDHashEntryHdr {
 public:
  using KeyType = const MetricLabelTimerTuple&;
  using KeyTypePointer = const MetricLabelTimerTuple*;

  explicit MetricLabelTimerTupleHashKey(KeyTypePointer aKey) : mValue(*aKey) {}
  MetricLabelTimerTupleHashKey(MetricLabelTimerTupleHashKey&& aOther)
      : PLDHashEntryHdr(std::move(aOther)), mValue(aOther.mValue) {}
  ~MetricLabelTimerTupleHashKey() = default;

  KeyType GetKey() const { return mValue; }
  bool KeyEquals(KeyTypePointer aKey) const {
    return aKey->mMetricId == mValue.mMetricId &&
           aKey->mTimerId == mValue.mTimerId;
  }

  static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }
  static PLDHashNumber HashKey(KeyTypePointer aKey) {
    return HashGeneric(aKey->mMetricId, HashString(aKey->mLabel),
                       aKey->mTimerId);
  }
  // Permitted to memmove nsCString even though it's not trivially copyable.
  enum { ALLOW_MEMMOVE = true };

 private:
  const MetricLabelTimerTuple mValue;
};

using LabelTimerToStampMutex = StaticDataMutex<
    UniquePtr<nsTHashMap<MetricLabelTimerTupleHashKey, TimeStamp>>>;
static Maybe<LabelTimerToStampMutex::AutoLock> GetLabelTimerIdToStartsLock() {
  static LabelTimerToStampMutex sLabelTimerIdToStarts("sLabelTimerIdToStarts");
  auto lock = sLabelTimerIdToStarts.Lock();
  // GIFFT will work up to the end of AppShutdownTelemetry.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMWillShutdown)) {
    return Nothing();
  }
  if (!*lock) {
    *lock = MakeUnique<nsTHashMap<MetricLabelTimerTupleHashKey, TimeStamp>>();
    RefPtr<nsIRunnable> cleanupFn = NS_NewRunnableFunction(__func__, [&] {
      if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMWillShutdown)) {
        auto lock = sLabelTimerIdToStarts.Lock();
        *lock = nullptr;  // deletes, see UniquePtr.h
        return;
      }
      RunOnShutdown(
          [&] {
            auto lock = sLabelTimerIdToStarts.Lock();
            *lock = nullptr;  // deletes, see UniquePtr.h
          },
          ShutdownPhase::XPCOMWillShutdown);
    });
    // Both getting the main thread and dispatching to it can fail.
    // In that event we leak. Grab a pointer so we have something to NS_RELEASE
    // in that case.
    nsIRunnable* temp = cleanupFn.get();
    nsCOMPtr<nsIThread> mainThread;
    if (NS_FAILED(NS_GetMainThread(getter_AddRefs(mainThread))) ||
        NS_FAILED(mainThread->Dispatch(cleanupFn.forget(),
                                       nsIThread::DISPATCH_NORMAL))) {
      // Failed to dispatch cleanup routine.
      // First, un-leak the runnable (but only if we actually attempted
      // dispatch)
      if (!cleanupFn) {
        NS_RELEASE(temp);
      }
      // Next, cleanup immediately, and allow metrics to try again later.
      *lock = nullptr;
      return Nothing();
    }
  }
  return Some(std::move(lock));
}

}  // namespace mozilla::glean

using mozilla::glean::TimerId;

// Called from within FOG's Rust impl.
extern "C" NS_EXPORT void GIFFT_TimingDistributionStart(uint32_t aMetricId,
                                                        TimerId aTimerId) {
  auto mirrorId = mozilla::glean::HistogramIdForMetric(aMetricId);
  if (mirrorId) {
    mozilla::glean::GetTimerIdToStartsLock().apply([&](const auto& lock) {
      auto tuple = mozilla::glean::MetricTimerTuple{aMetricId, aTimerId};
      // It should be all but impossible for anyone to have already inserted
      // this timer for this metric given the monotonicity of timer ids.
      (void)NS_WARN_IF(lock.ref()->Remove(tuple));
      lock.ref()->InsertOrUpdate(tuple, mozilla::TimeStamp::Now());
    });
  }
}

// Called from within FOG's Rust impl.
extern "C" NS_EXPORT void GIFFT_TimingDistributionStopAndAccumulate(
    uint32_t aMetricId, TimerId aTimerId, int32_t aUnit) {
  auto mirrorId = mozilla::glean::HistogramIdForMetric(aMetricId);
  if (mirrorId) {
    mozilla::glean::GetTimerIdToStartsLock().apply([&](const auto& lock) {
      auto tuple = mozilla::glean::MetricTimerTuple{aMetricId, aTimerId};
      auto optStart = lock.ref()->Extract(tuple);
      // The timer might not be in the map to be removed if it's already been
      // cancelled or stop_and_accumulate'd.
      if (!NS_WARN_IF(!optStart)) {
        TimeDuration duration = TimeStamp::Now() - optStart.extract();
        // Values are from Glean's `TimeUnit`
        switch (aUnit) {
          case 0:  // Nanos
            TelemetryHistogram::Accumulate(mirrorId.extract(),
                                           duration.ToMicroseconds() * 1000);
            break;
          case 1:  // Micros
            TelemetryHistogram::Accumulate(mirrorId.extract(),
                                           duration.ToMicroseconds());
            break;
          case 2:  // Millis
            TelemetryHistogram::Accumulate(mirrorId.extract(),
                                           duration.ToMilliseconds());
            break;
          case 3:  // Seconds
            TelemetryHistogram::Accumulate(mirrorId.extract(),
                                           duration.ToSeconds());
            break;
          case 4:  // Minutes
            TelemetryHistogram::Accumulate(mirrorId.extract(),
                                           duration.ToSeconds() / 60);
            break;
          case 5:  // Hours
            TelemetryHistogram::Accumulate(mirrorId.extract(),
                                           duration.ToSeconds() / 60 / 60);
            break;
          case 6:  // Days
            TelemetryHistogram::Accumulate(mirrorId.extract(),
                                           duration.ToSeconds() / 60 / 60 / 24);
            break;
          default:
            MOZ_ASSERT_UNREACHABLE("Invalid/Unsupported time unit");
            return;
        }
      }
    });
  }
}

// Called from within FOG's Rust impl.
extern "C" NS_EXPORT void GIFFT_TimingDistributionAccumulateRawSample(
    uint32_t aMetricId, uint32_t aSample) {
  auto mirrorId = mozilla::glean::HistogramIdForMetric(aMetricId);
  if (mirrorId) {
    TelemetryHistogram::Accumulate(mirrorId.extract(), aSample);
  }
}

// Called from within FOG's Rust impl.
extern "C" NS_EXPORT void GIFFT_TimingDistributionAccumulateRawSamples(
    uint32_t aMetricId, const nsTArray<uint32_t>& aSamples) {
  auto mirrorId = mozilla::glean::HistogramIdForMetric(aMetricId);
  if (mirrorId) {
    TelemetryHistogram::Accumulate(mirrorId.extract(), aSamples);
  }
}

// Called from within FOG's Rust impl.
extern "C" NS_EXPORT void GIFFT_TimingDistributionCancel(uint32_t aMetricId,
                                                         TimerId aTimerId) {
  auto mirrorId = mozilla::glean::HistogramIdForMetric(aMetricId);
  if (mirrorId) {
    mozilla::glean::GetTimerIdToStartsLock().apply([&](const auto& lock) {
      // The timer might not be in the map to be removed if it's already been
      // cancelled or stop_and_accumulate'd.
      auto tuple = mozilla::glean::MetricTimerTuple{aMetricId, aTimerId};
      (void)NS_WARN_IF(!lock.ref()->Remove(tuple));
    });
  }
}

// Called from within FOG's Rust impl.
extern "C" NS_EXPORT void GIFFT_LabeledTimingDistributionStart(
    uint32_t aMetricId, const nsACString& aLabel, TimerId aTimerId) {
  auto mirrorId = mozilla::glean::HistogramIdForMetric(aMetricId);
  if (mirrorId) {
    mozilla::glean::GetLabelTimerIdToStartsLock().apply([&](const auto& lock) {
      auto tuple = mozilla::glean::MetricLabelTimerTuple{
          aMetricId, PromiseFlatCString(aLabel), aTimerId};
      lock.ref()->InsertOrUpdate(tuple, mozilla::TimeStamp::Now());
    });
  }
}

// Called from within FOG's Rust impl.
extern "C" NS_EXPORT void GIFFT_LabeledTimingDistributionStopAndAccumulate(
    uint32_t aMetricId, const nsACString& aLabel, TimerId aTimerId) {
  auto mirrorId = mozilla::glean::HistogramIdForMetric(aMetricId);
  if (mirrorId) {
    mozilla::glean::GetLabelTimerIdToStartsLock().apply([&](const auto& lock) {
      auto tuple = mozilla::glean::MetricLabelTimerTuple{
          aMetricId, PromiseFlatCString(aLabel), aTimerId};
      auto optStart = lock.ref()->Extract(tuple);
      // The timer might not be in the map to be removed if it's already been
      // cancelled or stop_and_accumulate'd.
      if (!NS_WARN_IF(!optStart)) {
        TelemetryHistogram::Accumulate(
            mirrorId.extract(), PromiseFlatCString(aLabel),
            static_cast<uint32_t>(
                (mozilla::TimeStamp::Now() - optStart.extract())
                    .ToMilliseconds()));
      }
    });
  }
}

// Called from within FOG's Rust impl.
extern "C" NS_EXPORT void GIFFT_LabeledTimingDistributionAccumulateRawMillis(
    uint32_t aMetricId, const nsACString& aLabel, uint32_t aMS) {
  auto mirrorId = mozilla::glean::HistogramIdForMetric(aMetricId);
  if (mirrorId) {
    TelemetryHistogram::Accumulate(mirrorId.extract(),
                                   PromiseFlatCString(aLabel), aMS);
  }
}

// Called from within FOG's Rust impl.
extern "C" NS_EXPORT void GIFFT_LabeledTimingDistributionCancel(
    uint32_t aMetricId, const nsACString& aLabel, TimerId aTimerId) {
  auto mirrorId = mozilla::glean::HistogramIdForMetric(aMetricId);
  if (mirrorId) {
    mozilla::glean::GetLabelTimerIdToStartsLock().apply([&](const auto& lock) {
      // The timer might not be in the map to be removed if it's already been
      // cancelled or stop_and_accumulate'd.
      auto tuple = mozilla::glean::MetricLabelTimerTuple{
          aMetricId, PromiseFlatCString(aLabel), aTimerId};
      (void)NS_WARN_IF(!lock.ref()->Remove(tuple));
    });
  }
}

namespace mozilla::glean {

namespace impl {

TimerId TimingDistributionMetric::Start() const {
  return fog_timing_distribution_start(mId);
}

void TimingDistributionMetric::StopAndAccumulate(const TimerId&& aId) const {
  fog_timing_distribution_stop_and_accumulate(mId, aId);
}

// Intentionally not exposed to JS for lack of use case and a time duration
// type.
void TimingDistributionMetric::AccumulateRawDuration(
    const TimeDuration& aDuration) const {
  // `* 1000.0` is an acceptable overflow risk as durations are unlikely to be
  // on the order of (-)10^282 years.
  double durationNs = aDuration.ToMicroseconds() * 1000.0;
  double roundedDurationNs = std::round(durationNs);
  if (MOZ_UNLIKELY(
          roundedDurationNs <
              static_cast<double>(std::numeric_limits<uint64_t>::min()) ||
          roundedDurationNs >
              static_cast<double>(std::numeric_limits<uint64_t>::max()))) {
    // TODO(bug 1691073): Instrument this error.
    return;
  }
  fog_timing_distribution_accumulate_raw_nanos(
      mId, static_cast<uint64_t>(roundedDurationNs));
}

void TimingDistributionMetric::Cancel(const TimerId&& aId) const {
  fog_timing_distribution_cancel(mId, aId);
}

Result<Maybe<DistributionData>, nsCString>
TimingDistributionMetric::TestGetValue(const nsACString& aPingName) const {
  nsCString err;
  if (fog_timing_distribution_test_get_error(mId, &err)) {
    return Err(err);
  }
  if (!fog_timing_distribution_test_has_value(mId, &aPingName)) {
    return Maybe<DistributionData>();
  }
  nsTArray<uint64_t> buckets;
  nsTArray<uint64_t> counts;
  uint64_t sum;
  uint64_t count;
  fog_timing_distribution_test_get_value(mId, &aPingName, &sum, &count,
                                         &buckets, &counts);
  return Some(DistributionData(buckets, counts, sum, count));
}

TimingDistributionMetric::AutoTimer TimingDistributionMetric::Measure() const {
  return AutoTimer(mId, this->Start());
}

void TimingDistributionMetric::AutoTimer::Cancel() {
  fog_timing_distribution_cancel(mMetricId, std::move(mTimerId));
  mTimerId = 0;
}

TimingDistributionMetric::AutoTimer::~AutoTimer() {
  if (mTimerId) {
    fog_timing_distribution_stop_and_accumulate(mMetricId, std::move(mTimerId));
  }
}

}  // namespace impl

/* virtual */
JSObject* GleanTimingDistribution::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return dom::GleanTimingDistribution_Binding::Wrap(aCx, this, aGivenProto);
}

uint64_t GleanTimingDistribution::Start() { return mTimingDist.Start(); }

void GleanTimingDistribution::StopAndAccumulate(uint64_t aId) {
  mTimingDist.StopAndAccumulate(std::move(aId));
}

void GleanTimingDistribution::Cancel(uint64_t aId) {
  mTimingDist.Cancel(std::move(aId));
}

void GleanTimingDistribution::AccumulateSamples(
    const nsTArray<int64_t>& aSamples) {
  impl::fog_timing_distribution_accumulate_samples(mTimingDist.mId, &aSamples);
}

void GleanTimingDistribution::AccumulateSingleSample(int64_t aSample) {
  impl::fog_timing_distribution_accumulate_single_sample(mTimingDist.mId,
                                                         aSample);
}

void GleanTimingDistribution::TestGetValue(
    const nsACString& aPingName,
    dom::Nullable<dom::GleanDistributionData>& aRetval, ErrorResult& aRv) {
  auto result = mTimingDist.TestGetValue(aPingName);
  if (result.isErr()) {
    aRv.ThrowDataError(result.unwrapErr());
    return;
  }
  auto optresult = result.unwrap();
  if (optresult.isNothing()) {
    return;
  }

  dom::GleanDistributionData ret;
  ret.mSum = optresult.ref().sum;
  ret.mCount = optresult.ref().count;
  auto& data = optresult.ref().values;
  for (const auto& entry : data) {
    dom::binding_detail::RecordEntry<nsCString, uint64_t> bucket;
    bucket.mKey = nsPrintfCString("%" PRIu64, entry.GetKey());
    bucket.mValue = entry.GetData();
    ret.mValues.Entries().EmplaceBack(std::move(bucket));
  }
  aRetval.SetValue(std::move(ret));
}

void GleanTimingDistribution::TestAccumulateRawMillis(uint64_t aSample) {
  mTimingDist.AccumulateRawDuration(TimeDuration::FromMilliseconds(aSample));
}

}  // namespace mozilla::glean
