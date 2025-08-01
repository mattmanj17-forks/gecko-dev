/* vim:set ts=2 sw=2 sts=0 et: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

#include "core/TelemetryHistogram.h"
#include "gtest/gtest.h"
#include "js/Conversions.h"
#include "mozilla/glean/GleanTestsTestMetrics.h"
#include "mozilla/glean/TelemetryMetrics.h"
#include "mozilla/Telemetry.h"
#include "TelemetryFixture.h"
#include "TelemetryTestHelpers.h"

using namespace mozilla;
using namespace TelemetryTestHelpers;

TEST_F(TelemetryTestFixture, AccumulateCountHistogram) {
  const uint32_t kExpectedValue = 200;
  AutoJSContextWithGlobal cx(mCleanGlobal);

  const char* telemetryTestCountName =
      Telemetry::GetHistogramName(Telemetry::TELEMETRY_TEST_COUNT);
  ASSERT_STREQ(telemetryTestCountName, "TELEMETRY_TEST_COUNT")
      << "The histogram name is wrong";

  GetAndClearHistogram(cx.GetJSContext(), mTelemetry, "TELEMETRY_TEST_COUNT"_ns,
                       false);

  // Accumulate in the histogram
  TelemetryHistogram::Accumulate(Telemetry::TELEMETRY_TEST_COUNT,
                                 kExpectedValue);

  // Get a snapshot for all the histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry, telemetryTestCountName, &snapshot,
               false);

  // Get the histogram from the snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), telemetryTestCountName, snapshot, &histogram);

  // Get "sum" property from histogram
  JS::Rooted<JS::Value> sum(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "sum", histogram, &sum);

  // Check that the "sum" stored in the histogram matches with |kExpectedValue|
  uint32_t uSum = 0;
  JS::ToUint32(cx.GetJSContext(), sum, &uSum);
  ASSERT_EQ(uSum, kExpectedValue)
      << "The histogram is not returning expected value";
}

TEST_F(TelemetryTestFixture, AccumulateKeyedCountHistogram) {
  const uint32_t kExpectedValue = 100;
  AutoJSContextWithGlobal cx(mCleanGlobal);

  GetAndClearHistogram(cx.GetJSContext(), mTelemetry,
                       "TELEMETRY_TEST_KEYED_COUNT"_ns, true);

  // Accumulate data in the provided key within the histogram
  TelemetryHistogram::Accumulate(Telemetry::TELEMETRY_TEST_KEYED_COUNT,
                                 "sample"_ns, kExpectedValue);

  // Get a snapshot for all the histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry, "TELEMETRY_TEST_KEYED_COUNT",
               &snapshot, true);

  // Get the histogram from the snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "TELEMETRY_TEST_KEYED_COUNT", snapshot,
              &histogram);

  // Get "sample" property from histogram
  JS::Rooted<JS::Value> expectedKeyData(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "sample", histogram, &expectedKeyData);

  // Get "sum" property from keyed data
  JS::Rooted<JS::Value> sum(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "sum", expectedKeyData, &sum);

  // Check that the sum stored in the histogram matches with |kExpectedValue|
  uint32_t uSum = 0;
  JS::ToUint32(cx.GetJSContext(), sum, &uSum);
  ASSERT_EQ(uSum, kExpectedValue)
      << "The histogram is not returning expected sum";
}

TEST_F(TelemetryTestFixture, TestKeyedKeysHistogram) {
  AutoJSContextWithGlobal cx(mCleanGlobal);

  JS::Rooted<JS::Value> testHistogram(cx.GetJSContext());
  JS::Rooted<JS::Value> rval(cx.GetJSContext());

  GetAndClearHistogram(cx.GetJSContext(), mTelemetry,
                       "TELEMETRY_TEST_KEYED_KEYS"_ns, true);

  // Test the accumulation on both the allowed and unallowed keys, using
  // the API that accepts histogram IDs.
  TelemetryHistogram::Accumulate(Telemetry::TELEMETRY_TEST_KEYED_KEYS,
                                 "not-allowed"_ns, 1);
  TelemetryHistogram::Accumulate(Telemetry::TELEMETRY_TEST_KEYED_KEYS,
                                 "testkey"_ns, 0);
  TelemetryHistogram::Accumulate(Telemetry::TELEMETRY_TEST_KEYED_KEYS,
                                 "CommonKey"_ns, 1);

  // Get a snapshot for all the histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry, "TELEMETRY_TEST_KEYED_KEYS",
               &snapshot, true);

  // Get the histogram from the snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "TELEMETRY_TEST_KEYED_KEYS", snapshot,
              &histogram);

  // Get "testkey" property from histogram and check that it stores the correct
  // data.
  JS::Rooted<JS::Value> expectedKeyData(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "testkey", histogram, &expectedKeyData);
  ASSERT_TRUE(!expectedKeyData.isUndefined())
  << "Cannot find the expected key in the histogram data";
  JS::Rooted<JS::Value> sum(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "sum", expectedKeyData, &sum);
  uint32_t uSum = 0;
  JS::ToUint32(cx.GetJSContext(), sum, &uSum);
  ASSERT_EQ(uSum, 0U)
      << "The histogram is not returning expected sum for 'testkey'";

  // Do the same for the "CommonKey" property.
  GetProperty(cx.GetJSContext(), "CommonKey", histogram, &expectedKeyData);
  ASSERT_TRUE(!expectedKeyData.isUndefined())
  << "Cannot find the expected key in the histogram data";
  GetProperty(cx.GetJSContext(), "sum", expectedKeyData, &sum);
  JS::ToUint32(cx.GetJSContext(), sum, &uSum);
  ASSERT_EQ(uSum, 1U)
      << "The histogram is not returning expected sum for 'CommonKey'";

  GetProperty(cx.GetJSContext(), "not-allowed", histogram, &expectedKeyData);
  ASSERT_TRUE(expectedKeyData.isUndefined())
  << "Unallowed keys must not be recorded in the histogram data";

  // We expect the count of 'telemetry.accumulate_unknown_histogram_keys' to be
  // 1 for the 'not-allowed' key accumulation.
  const uint32_t expectedAccumulateUnknownCount = 1;
  JS::Rooted<JS::Value> scalarsSnapshot(cx.GetJSContext());
  GetScalarsSnapshot(true, cx.GetJSContext(), &scalarsSnapshot);
  CheckKeyedUintScalar("telemetry.accumulate_unknown_histogram_keys",
                       "TELEMETRY_TEST_KEYED_KEYS", cx.GetJSContext(),
                       scalarsSnapshot, expectedAccumulateUnknownCount);
}

TEST_F(TelemetryTestFixture, AccumulateCategoricalHistogram) {
  const uint32_t kExpectedValue = 1;

  AutoJSContextWithGlobal cx(mCleanGlobal);

  GetAndClearHistogram(cx.GetJSContext(), mTelemetry,
                       "TELEMETRY_TEST_CATEGORICAL"_ns, false);

  // Accumulate one unit into the categorical histogram using a string label
  TelemetryHistogram::AccumulateCategorical(
      Telemetry::TELEMETRY_TEST_CATEGORICAL, "CommonLabel"_ns);

  // Get a snapshot for all the histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry, "TELEMETRY_TEST_CATEGORICAL",
               &snapshot, false);

  // Get our histogram from the snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "TELEMETRY_TEST_CATEGORICAL", snapshot,
              &histogram);

  // Get values object from histogram. Each entry in the object maps to a label
  // in the histogram.
  JS::Rooted<JS::Value> values(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "values", histogram, &values);

  // Get the value for the label we care about
  JS::Rooted<JS::Value> value(cx.GetJSContext());
  GetElement(cx.GetJSContext(),
             static_cast<uint32_t>(
                 Telemetry::LABELS_TELEMETRY_TEST_CATEGORICAL::CommonLabel),
             values, &value);

  // Check that the value stored in the histogram matches with |kExpectedValue|
  uint32_t uValue = 0;
  JS::ToUint32(cx.GetJSContext(), value, &uValue);
  ASSERT_EQ(uValue, kExpectedValue)
      << "The histogram is not returning expected value";
}

template <class E>
void AccumulateCategoricalKeyed(const nsCString& key, E enumValue) {
  TelemetryHistogram::Accumulate(static_cast<Telemetry::HistogramID>(
                                     Telemetry::CategoricalLabelId<E>::value),
                                 key, static_cast<uint32_t>(enumValue));
};

TEST_F(TelemetryTestFixture, AccumulateKeyedCategoricalHistogram) {
  const uint32_t kSampleExpectedValue = 2;
  const uint32_t kOtherSampleExpectedValue = 1;

  AutoJSContextWithGlobal cx(mCleanGlobal);

  GetAndClearHistogram(cx.GetJSContext(), mTelemetry,
                       "TELEMETRY_TEST_KEYED_CATEGORICAL"_ns, true);

  // Accumulate one unit into the categorical histogram with label
  // Telemetry::LABELS_TELEMETRY_TEST_KEYED_CATEGORICAL::CommonLabel
  AccumulateCategoricalKeyed(
      "sample"_ns,
      Telemetry::LABELS_TELEMETRY_TEST_KEYED_CATEGORICAL::CommonLabel);
  // Accumulate another unit into the same categorical histogram
  AccumulateCategoricalKeyed(
      "sample"_ns,
      Telemetry::LABELS_TELEMETRY_TEST_KEYED_CATEGORICAL::CommonLabel);
  // Accumulate another unit into a different categorical histogram
  AccumulateCategoricalKeyed(
      "other-sample"_ns,
      Telemetry::LABELS_TELEMETRY_TEST_KEYED_CATEGORICAL::CommonLabel);

  // Get a snapshot for all the histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry,
               "TELEMETRY_TEST_KEYED_CATEGORICAL", &snapshot, true);
  // Get the histogram from the snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "TELEMETRY_TEST_KEYED_CATEGORICAL", snapshot,
              &histogram);

  // Check that the sample histogram contains the values we expect
  JS::Rooted<JS::Value> sample(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "sample", histogram, &sample);
  // Get values object from sample. Each entry in the object maps to a label in
  // the histogram.
  JS::Rooted<JS::Value> sampleValues(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "values", sample, &sampleValues);
  // Get the value for the label we care about
  JS::Rooted<JS::Value> sampleValue(cx.GetJSContext());
  GetElement(
      cx.GetJSContext(),
      static_cast<uint32_t>(
          Telemetry::LABELS_TELEMETRY_TEST_KEYED_CATEGORICAL::CommonLabel),
      sampleValues, &sampleValue);
  // Check that the value stored in the histogram matches with
  // |kSampleExpectedValue|
  uint32_t uSampleValue = 0;
  JS::ToUint32(cx.GetJSContext(), sampleValue, &uSampleValue);
  ASSERT_EQ(uSampleValue, kSampleExpectedValue)
      << "The sample histogram is not returning expected value";

  // Check that the other-sample histogram contains the values we expect
  JS::Rooted<JS::Value> otherSample(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "other-sample", histogram, &otherSample);
  // Get values object from the other-sample. Each entry in the object maps to a
  // label in the histogram.
  JS::Rooted<JS::Value> otherValues(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "values", otherSample, &otherValues);
  // Get the value for the label we care about
  JS::Rooted<JS::Value> otherValue(cx.GetJSContext());
  GetElement(
      cx.GetJSContext(),
      static_cast<uint32_t>(
          Telemetry::LABELS_TELEMETRY_TEST_KEYED_CATEGORICAL::CommonLabel),
      otherValues, &otherValue);
  // Check that the value stored in the histogram matches with
  // |kOtherSampleExpectedValue|
  uint32_t uOtherValue = 0;
  JS::ToUint32(cx.GetJSContext(), otherValue, &uOtherValue);
  ASSERT_EQ(uOtherValue, kOtherSampleExpectedValue)
      << "The other-sample histogram is not returning expected value";
}

TEST_F(TelemetryTestFixture, AccumulateCountHistogram_MultipleSamples) {
  nsTArray<uint32_t> samples({4, 4, 4});
  const uint32_t kExpectedSum = 12;

  AutoJSContextWithGlobal cx(mCleanGlobal);

  GetAndClearHistogram(cx.GetJSContext(), mTelemetry, "TELEMETRY_TEST_COUNT"_ns,
                       false);

  // Accumulate in histogram
  TelemetryHistogram::Accumulate(Telemetry::TELEMETRY_TEST_COUNT, samples);

  // Get a snapshot of all the histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry, "TELEMETRY_TEST_COUNT", &snapshot,
               false);

  // Get histogram from snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "TELEMETRY_TEST_COUNT", snapshot, &histogram);

  // Get "sum" from histogram
  JS::Rooted<JS::Value> sum(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "sum", histogram, &sum);

  // Check that sum matches with aValue
  uint32_t uSum = 0;
  JS::ToUint32(cx.GetJSContext(), sum, &uSum);
  ASSERT_EQ(uSum, kExpectedSum)
      << "This histogram is not returning expected value";
}

TEST_F(TelemetryTestFixture, AccumulateLinearHistogram_MultipleSamples) {
  nsTArray<uint32_t> samples({4, 4, 4});
  const uint32_t kExpectedCount = 3;

  AutoJSContextWithGlobal cx(mCleanGlobal);

  GetAndClearHistogram(cx.GetJSContext(), mTelemetry,
                       "TELEMETRY_TEST_LINEAR"_ns, false);

  // Accumulate in the histogram
  TelemetryHistogram::Accumulate(Telemetry::TELEMETRY_TEST_LINEAR, samples);

  // Get a snapshot of all the histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry, "TELEMETRY_TEST_LINEAR",
               &snapshot, false);

  // Get histogram from snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "TELEMETRY_TEST_LINEAR", snapshot, &histogram);

  // Get "values" object from histogram
  JS::Rooted<JS::Value> values(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "values", histogram, &values);

  // Index 0 is only for values less than 'low'. Values within range start at
  // index 1
  JS::Rooted<JS::Value> count(cx.GetJSContext());
  const uint32_t index = 1;
  GetElement(cx.GetJSContext(), index, values, &count);

  // Check that this count matches with nSamples
  uint32_t uCount = 0;
  JS::ToUint32(cx.GetJSContext(), count, &uCount);
  ASSERT_EQ(uCount, kExpectedCount)
      << "The histogram did not accumulate the correct number of values";
}

TEST_F(TelemetryTestFixture, AccumulateLinearHistogram_DifferentSamples) {
  nsTArray<uint32_t> samples(
      {4, 8, 2147483646, uint32_t(INT_MAX) + 1, UINT32_MAX});

  AutoJSContextWithGlobal cx(mCleanGlobal);

  mTelemetry->ClearScalars();
  GetAndClearHistogram(cx.GetJSContext(), mTelemetry,
                       "TELEMETRY_TEST_LINEAR"_ns, false);

  // Accumulate in histogram
  TelemetryHistogram::Accumulate(Telemetry::TELEMETRY_TEST_LINEAR, samples);

  // Get a snapshot of all histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry, "TELEMETRY_TEST_LINEAR",
               &snapshot, false);

  // Get histogram from snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "TELEMETRY_TEST_LINEAR", snapshot, &histogram);

  // Get values object from histogram
  JS::Rooted<JS::Value> values(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "values", histogram, &values);

  // Get values in first and last buckets
  JS::Rooted<JS::Value> countFirst(cx.GetJSContext());
  JS::Rooted<JS::Value> countLast(cx.GetJSContext());
  const uint32_t firstIndex = 1;
  // Buckets are indexed by their start value
  const uint32_t lastIndex = INT32_MAX - 1;
  GetElement(cx.GetJSContext(), firstIndex, values, &countFirst);
  GetElement(cx.GetJSContext(), lastIndex, values, &countLast);

  // Check that the values match
  uint32_t uCountFirst = 0;
  uint32_t uCountLast = 0;
  JS::ToUint32(cx.GetJSContext(), countFirst, &uCountFirst);
  JS::ToUint32(cx.GetJSContext(), countLast, &uCountLast);

  const uint32_t kExpectedCountFirst = 2;
  // We expect 2147483646 to be in the last bucket, as well the two samples
  // above 2^31 (prior to bug 1438335, values between INT_MAX and UINT32_MAX
  // would end up as 0s)
  const uint32_t kExpectedCountLast = 3;
  ASSERT_EQ(uCountFirst, kExpectedCountFirst)
      << "The first bucket did not accumulate the correct number of values";
  ASSERT_EQ(uCountLast, kExpectedCountLast)
      << "The last bucket did not accumulate the correct number of values";

  // We accumulated two values that had to be clamped. We expect the count in
  // 'telemetry.accumulate_clamped_values' to be 2 (only one storage).
  const uint32_t expectedAccumulateClampedCount = 2;
  JS::Rooted<JS::Value> scalarsSnapshot(cx.GetJSContext());
  GetScalarsSnapshot(true, cx.GetJSContext(), &scalarsSnapshot);
  CheckKeyedUintScalar("telemetry.accumulate_clamped_values",
                       "TELEMETRY_TEST_LINEAR", cx.GetJSContext(),
                       scalarsSnapshot, expectedAccumulateClampedCount);
}

TEST_F(TelemetryTestFixture, GIFFTLabeledCounterToBooleanHgram) {
  AutoJSContextWithGlobal cx(mCleanGlobal);

  GetAndClearHistogram(cx.GetJSContext(), mTelemetry,
                       "TELEMETRY_TEST_BOOLEAN"_ns, false);

  nsCString empty;
  ASSERT_EQ(NS_OK, mozilla::glean::impl::fog_test_reset(&empty, &empty));

  auto forTrue =
      mozilla::glean::test_only_ipc::a_labeled_counter_for_hgram.EnumGet(
          mozilla::glean::test_only_ipc::ALabeledCounterForHgramLabel::eTrue);
  forTrue.Add(1);
  forTrue.Add(1);
  mozilla::glean::test_only_ipc::a_labeled_counter_for_hgram.Get("false"_ns)
      .Add(1);

  // Get a snapshot for all the histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry, "", &snapshot, false);

  // Get the histogram from the snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "TELEMETRY_TEST_BOOLEAN", snapshot,
              &histogram);

  // Get the "values" array object from histogram.
  JS::Rooted<JS::Value> values(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "values", histogram, &values);

  // Get values in buckets 0,1,2
  const uint32_t falseIndex = 0;
  const uint32_t trueIndex = 1;
  const uint32_t otherIndex = 2;

  JS::Rooted<JS::Value> countFalse(cx.GetJSContext());
  JS::Rooted<JS::Value> countTrue(cx.GetJSContext());
  JS::Rooted<JS::Value> countOther(cx.GetJSContext());

  GetElement(cx.GetJSContext(), falseIndex, values, &countFalse);
  GetElement(cx.GetJSContext(), trueIndex, values, &countTrue);
  GetElement(cx.GetJSContext(), otherIndex, values, &countOther);

  uint32_t uCountFalse = 0;
  uint32_t uCountTrue = 0;
  uint32_t uCountOther = 0;
  JS::ToUint32(cx.GetJSContext(), countFalse, &uCountFalse);
  JS::ToUint32(cx.GetJSContext(), countTrue, &uCountTrue);
  JS::ToUint32(cx.GetJSContext(), countOther, &uCountOther);

  ASSERT_EQ(1u, uCountFalse);
  ASSERT_EQ(2u, uCountTrue);
  ASSERT_EQ(0u, uCountOther);
}

TEST_F(TelemetryTestFixture, GIFFTLabeledCounterToKeyedCountHgram) {
  AutoJSContextWithGlobal cx(mCleanGlobal);

  GetAndClearHistogram(cx.GetJSContext(), mTelemetry,
                       "TELEMETRY_TEST_KEYED_COUNT"_ns, true);

  nsCString empty;
  ASSERT_EQ(NS_OK, mozilla::glean::impl::fog_test_reset(&empty, &empty));

  mozilla::glean::test_only_ipc::a_labeled_counter_for_keyed_count_hgram
      .Get("aLabel"_ns)
      .Add(3);
  mozilla::glean::test_only_ipc::a_labeled_counter_for_keyed_count_hgram
      .Get("some other label"_ns)
      .Add(4);

  // Get a snapshot for all the keyed histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry, "", &snapshot, true);

  // Get the histogram from the snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "TELEMETRY_TEST_KEYED_COUNT", snapshot,
              &histogram);

  // Get "aLabel" subhistogram from keyed histogram
  JS::Rooted<JS::Value> expectedKeyData(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "aLabel", histogram, &expectedKeyData);

  // Get "sum" property from aLabel's subhistogram
  JS::Rooted<JS::Value> sum(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "sum", expectedKeyData, &sum);

  // Check that the sum stored in the histogram is correct.
  uint32_t uSum = 0;
  JS::ToUint32(cx.GetJSContext(), sum, &uSum);
  ASSERT_EQ(3u, uSum) << "The 'aLabel' hgram has incorrect sum.";

  // Get "some other label" subhistogram from keyed histogram
  GetProperty(cx.GetJSContext(), "some other label", histogram,
              &expectedKeyData);

  // Get "sum" property from aLabel's subhistogram
  GetProperty(cx.GetJSContext(), "sum", expectedKeyData, &sum);

  // Check that the sum stored in the histogram is correct.
  uSum = 0;
  JS::ToUint32(cx.GetJSContext(), sum, &uSum);
  ASSERT_EQ(4u, uSum) << "The 'some other label' hgram has incorrect sum.";
}

TEST_F(TelemetryTestFixture, GIFFTLabeledCounterToCategoricalHgram) {
  AutoJSContextWithGlobal cx(mCleanGlobal);

  GetAndClearHistogram(cx.GetJSContext(), mTelemetry,
                       "TELEMETRY_TEST_CATEGORICAL_OPTOUT"_ns, false);

  nsCString empty;
  ASSERT_EQ(NS_OK, mozilla::glean::impl::fog_test_reset(&empty, &empty));

  mozilla::glean::test_only_ipc::a_labeled_counter_for_categorical
      .EnumGet(mozilla::glean::test_only_ipc::
                   ALabeledCounterForCategoricalLabel::eCommonlabel)
      .Add(1);
  mozilla::glean::test_only_ipc::a_labeled_counter_for_categorical
      .Get("CommonLabel"_ns)
      .Add(1);
  mozilla::glean::test_only_ipc::a_labeled_counter_for_categorical
      .Get("Label5"_ns)
      .Add(1);

  // Get a snapshot for all the histograms
  JS::Rooted<JS::Value> snapshot(cx.GetJSContext());
  GetSnapshots(cx.GetJSContext(), mTelemetry, "", &snapshot, false);

  // Get the histogram from the snapshot
  JS::Rooted<JS::Value> histogram(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "TELEMETRY_TEST_CATEGORICAL_OPTOUT", snapshot,
              &histogram);

  // Get "values" array object from histogram.
  JS::Rooted<JS::Value> values(cx.GetJSContext());
  GetProperty(cx.GetJSContext(), "values", histogram, &values);

  // Get the value for the "CommonLabel" label.
  JS::Rooted<JS::Value> value(cx.GetJSContext());
  GetElement(
      cx.GetJSContext(),
      static_cast<uint32_t>(
          Telemetry::LABELS_TELEMETRY_TEST_CATEGORICAL_OPTOUT::CommonLabel),
      values, &value);

  // Check that the value stored in the histogram is correct.
  uint32_t uValue = 0;
  JS::ToUint32(cx.GetJSContext(), value, &uValue);
  ASSERT_EQ(2u, uValue) << "CommonLabel has incorrect value";

  // Now let's check "Label5".
  GetElement(cx.GetJSContext(),
             static_cast<uint32_t>(
                 Telemetry::LABELS_TELEMETRY_TEST_CATEGORICAL_OPTOUT::Label5),
             values, &value);

  // Check that the value stored in the histogram is correct.
  uValue = 0;
  JS::ToUint32(cx.GetJSContext(), value, &uValue);
  ASSERT_EQ(1u, uValue) << "Label5 has incorrect value";
}
