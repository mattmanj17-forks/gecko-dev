const { TelemetryController } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetryController.sys.mjs"
);
const { TelemetrySession } = ChromeUtils.importESModule(
  "resource://gre/modules/TelemetrySession.sys.mjs"
);
const { ContentTaskUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ContentTaskUtils.sys.mjs"
);

const MESSAGE_CHILD_TEST_DONE = "ChildTest:Done";

function run_child_test() {
  // Setup histograms with some fixed values.
  let countHist = Telemetry.getHistogramById("TELEMETRY_TEST_COUNT");
  countHist.add();
  countHist.add();
  let categHist = Telemetry.getHistogramById("TELEMETRY_TEST_CATEGORICAL");
  categHist.add("Label2");
  categHist.add("Label3");

  let countKeyed = Telemetry.getKeyedHistogramById(
    "TELEMETRY_TEST_KEYED_COUNT"
  );
  countKeyed.add("a");
  countKeyed.add("b");
  countKeyed.add("b");

  // Test record_in_processes
  let contentLinear = Telemetry.getHistogramById(
    "TELEMETRY_TEST_CONTENT_PROCESS"
  );
  contentLinear.add(10);
  let contentKeyed = Telemetry.getKeyedHistogramById(
    "TELEMETRY_TEST_KEYED_CONTENT_PROCESS"
  );
  contentKeyed.add("content", 1);
  let allLinear = Telemetry.getHistogramById("TELEMETRY_TEST_ALL_PROCESSES");
  allLinear.add(10);
  let allChildLinear = Telemetry.getHistogramById(
    "TELEMETRY_TEST_ALL_CHILD_PROCESSES"
  );
  allChildLinear.add(10);

  // Test snapshot APIs.
  // Should be forbidden in content processes.
  Assert.throws(
    () => Telemetry.getHistogramById("TELEMETRY_TEST_COUNT").snapshot(),
    /Histograms can only be snapshotted in the parent process/,
    "Snapshotting should be forbidden in the content process"
  );

  Assert.throws(
    () =>
      Telemetry.getKeyedHistogramById("TELEMETRY_TEST_KEYED_COUNT").snapshot(),
    /Keyed histograms can only be snapshotted in the parent process/,
    "Snapshotting should be forbidden in the content process"
  );

  Assert.throws(
    () => Telemetry.getHistogramById("TELEMETRY_TEST_COUNT").clear(),
    /Histograms can only be cleared in the parent process/,
    "Clearing should be forbidden in the content process"
  );

  Assert.throws(
    () => Telemetry.getKeyedHistogramById("TELEMETRY_TEST_KEYED_COUNT").clear(),
    /Keyed histograms can only be cleared in the parent process/,
    "Clearing should be forbidden in the content process"
  );

  Assert.throws(
    () => Telemetry.getSnapshotForHistograms(),
    /NS_ERROR_FAILURE/,
    "Snapshotting should be forbidden in the content process"
  );

  Assert.throws(
    () => Telemetry.getSnapshotForKeyedHistograms(),
    /NS_ERROR_FAILURE/,
    "Snapshotting should be forbidden in the content process"
  );
}

function check_histogram_values(payload) {
  const hs = payload.histograms;
  Assert.ok("TELEMETRY_TEST_COUNT" in hs, "Should have count test histogram.");
  Assert.ok(
    "TELEMETRY_TEST_CATEGORICAL" in hs,
    "Should have categorical test histogram."
  );
  Assert.equal(
    hs.TELEMETRY_TEST_COUNT.sum,
    2,
    "Count test histogram should have the right value."
  );
  Assert.equal(
    hs.TELEMETRY_TEST_CATEGORICAL.sum,
    3,
    "Categorical test histogram should have the right sum."
  );

  const kh = payload.keyedHistograms;
  Assert.ok(
    "TELEMETRY_TEST_KEYED_COUNT" in kh,
    "Should have keyed count test histogram."
  );
  Assert.equal(
    kh.TELEMETRY_TEST_KEYED_COUNT.a.sum,
    1,
    "Keyed count test histogram should have the right value."
  );
  Assert.equal(
    kh.TELEMETRY_TEST_KEYED_COUNT.b.sum,
    2,
    "Keyed count test histogram should have the right value."
  );
}

add_task(async function () {
  if (!runningInParent) {
    TelemetryController.testSetupContent();
    run_child_test();
    dump("... done with child test\n");
    do_send_remote_message(MESSAGE_CHILD_TEST_DONE);
    return;
  }

  // Setup.
  do_get_profile(true);
  await loadAddonManager(APP_ID, APP_NAME, APP_VERSION, PLATFORM_VERSION);
  finishAddonManagerStartup();
  fakeIntlReady();
  await TelemetryController.testSetup();
  if (runningInParent) {
    // Make sure we don't generate unexpected pings due to pref changes.
    await setEmptyPrefWatchlist();
  }

  // Run test in child, don't wait for it to finish.
  run_test_in_child("test_ChildHistograms.js");
  await do_await_remote_message(MESSAGE_CHILD_TEST_DONE);

  await ContentTaskUtils.waitForCondition(() => {
    let payload = TelemetrySession.getPayload("test-ping");
    return (
      payload &&
      "processes" in payload &&
      "content" in payload.processes &&
      "histograms" in payload.processes.content &&
      "TELEMETRY_TEST_COUNT" in payload.processes.content.histograms
    );
  });

  // Test record_in_processes in main process, too
  let contentLinear = Telemetry.getHistogramById(
    "TELEMETRY_TEST_CONTENT_PROCESS"
  );
  contentLinear.add(20);
  let contentKeyed = Telemetry.getKeyedHistogramById(
    "TELEMETRY_TEST_KEYED_CONTENT_PROCESS"
  );
  contentKeyed.add("parent", 1);
  let allLinear = Telemetry.getHistogramById("TELEMETRY_TEST_ALL_PROCESSES");
  allLinear.add(20);
  let allChildLinear = Telemetry.getHistogramById(
    "TELEMETRY_TEST_ALL_CHILD_PROCESSES"
  );
  allChildLinear.add(20);
  let countKeyed = Telemetry.getKeyedHistogramById(
    "TELEMETRY_TEST_KEYED_COUNT"
  );
  countKeyed.add("a");

  const payload = TelemetrySession.getPayload("test-ping");
  Assert.ok("processes" in payload, "Should have processes section");
  Assert.ok(
    "content" in payload.processes,
    "Should have child process section"
  );
  Assert.ok(
    "histograms" in payload.processes.content,
    "Child process section should have histograms."
  );
  Assert.ok(
    "keyedHistograms" in payload.processes.content,
    "Child process section should have keyed histograms."
  );
  check_histogram_values(payload.processes.content);

  // Check record_in_processes
  // Content Process
  let hs = payload.processes.content.histograms;
  let khs = payload.processes.content.keyedHistograms;
  Assert.ok(
    "TELEMETRY_TEST_CONTENT_PROCESS" in hs,
    "Should have content process histogram"
  );
  Assert.equal(
    hs.TELEMETRY_TEST_CONTENT_PROCESS.sum,
    10,
    "Should have correct value"
  );
  Assert.ok(
    "TELEMETRY_TEST_KEYED_CONTENT_PROCESS" in khs,
    "Should have keyed content process histogram"
  );
  Assert.equal(
    khs.TELEMETRY_TEST_KEYED_CONTENT_PROCESS.content.sum,
    1,
    "Should have correct value"
  );
  Assert.ok(
    "TELEMETRY_TEST_ALL_PROCESSES" in hs,
    "Should have content process histogram"
  );
  Assert.equal(
    hs.TELEMETRY_TEST_ALL_PROCESSES.sum,
    10,
    "Should have correct value"
  );
  Assert.ok(
    "TELEMETRY_TEST_ALL_CHILD_PROCESSES" in hs,
    "Should have content process histogram"
  );
  Assert.equal(
    hs.TELEMETRY_TEST_ALL_CHILD_PROCESSES.sum,
    10,
    "Should have correct value"
  );

  // Main Process
  let mainHs = payload.histograms;
  let mainKhs = payload.keyedHistograms;
  Assert.ok(
    !("TELEMETRY_TEST_CONTENT_PROCESS" in mainHs),
    "Should not have content process histogram in main process payload"
  );
  Assert.ok(
    !("TELEMETRY_TEST_KEYED_CONTENT_PROCESS" in mainKhs),
    "Should not have keyed content process histogram in main process payload"
  );
  Assert.ok(
    "TELEMETRY_TEST_ALL_PROCESSES" in mainHs,
    "Should have all-process histogram in main process payload"
  );
  Assert.equal(
    mainHs.TELEMETRY_TEST_ALL_PROCESSES.sum,
    20,
    "Should have correct value"
  );
  Assert.ok(
    !("TELEMETRY_TEST_ALL_CHILD_PROCESSES" in mainHs),
    "Should not have all-child process histogram in main process payload"
  );
  Assert.equal(
    mainKhs.TELEMETRY_TEST_KEYED_COUNT.a.sum,
    1,
    "Should have correct value in parent"
  );

  do_test_finished();
});
