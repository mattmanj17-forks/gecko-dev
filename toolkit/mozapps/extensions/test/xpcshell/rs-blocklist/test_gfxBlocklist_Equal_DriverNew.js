/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Test whether a machine which is newer than the equal
// blocklist entry is allowed.
// Uses test_gfxBlocklist.json

// Performs the initial setup
async function run_test() {
  var gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo);

  // We can't do anything if we can't spoof the stuff we need.
  if (!(gfxInfo instanceof Ci.nsIGfxInfoDebug)) {
    do_test_finished();
    return;
  }

  gfxInfo.QueryInterface(Ci.nsIGfxInfoDebug);

  // Set the vendor/device ID, etc, to match the test file.
  switch (Services.appinfo.OS) {
    case "WINNT":
      gfxInfo.spoofVendorID("0xdcdc");
      gfxInfo.spoofDeviceID("0x1234");
      // test_gfxBlocklist.json has several entries targeting "os": "All"
      // ("All" meaning "All Windows"), with several combinations of
      // "driverVersion" / "driverVersionMax" / "driverVersionComparator".
      gfxInfo.spoofDriverVersion("8.52.322.1112");
      // Windows 7
      gfxInfo.spoofOSVersion(0x60001);
      break;
    case "Linux":
      // We don't support driver versions on Linux.
      // XXX don't we? Seems like we do since bug 1294232 with the change in
      // https://hg.mozilla.org/mozilla-central/diff/8962b8d9b7a6/widget/GfxInfoBase.cpp
      // To update this test, we'd have to update test_gfxBlocklist.json in a
      // way similar to how bug 1714673 was resolved for Android.
      do_test_finished();
      return;
    case "Darwin":
      // We don't support driver versions on OS X.
      do_test_finished();
      return;
    case "Android":
      gfxInfo.spoofVendorID("dcdc");
      gfxInfo.spoofDeviceID("uiop");
      gfxInfo.spoofDriverVersion("6");
      break;
  }

  do_test_pending();

  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "15.0", "8");
  await promiseStartupManager();

  function checkBlocklist() {
    var status = gfxInfo.getFeatureStatusStr("DIRECT2D");
    Assert.equal(status, "STATUS_OK");

    // Make sure unrelated features aren't affected
    status = gfxInfo.getFeatureStatusStr("DIRECT3D_9_LAYERS");
    Assert.equal(status, "STATUS_OK");

    status = gfxInfo.getFeatureStatusStr("DIRECT3D_11_LAYERS");
    Assert.equal(status, "STATUS_OK");

    status = gfxInfo.getFeatureStatusStr("OPENGL_LAYERS");
    Assert.equal(status, "STATUS_OK");

    status = gfxInfo.getFeatureStatusStr("DIRECT3D_11_ANGLE");
    Assert.equal(status, "BLOCKED_DRIVER_VERSION");

    status = gfxInfo.getFeatureStatusStr("HARDWARE_VIDEO_DECODING");
    Assert.equal(status, "BLOCKED_DRIVER_VERSION");

    status = gfxInfo.getFeatureStatusStr("WEBRTC_HW_ACCELERATION_H264");
    Assert.equal(status, "BLOCKED_DRIVER_VERSION");

    status = gfxInfo.getFeatureStatusStr("WEBRTC_HW_ACCELERATION_DECODE");
    Assert.equal(status, "BLOCKED_DRIVER_VERSION");

    status = gfxInfo.getFeatureStatusStr("WEBRTC_HW_ACCELERATION_ENCODE");
    Assert.equal(status, "BLOCKED_DRIVER_VERSION");

    status = gfxInfo.getFeatureStatusStr("WEBGL_ANGLE");
    Assert.equal(status, "STATUS_OK");

    status = gfxInfo.getFeatureStatusStr("CANVAS2D_ACCELERATION");
    Assert.equal(status, "BLOCKED_DRIVER_VERSION");

    do_test_finished();
  }

  Services.obs.addObserver(function () {
    // If we wait until after we go through the event loop, gfxInfo is sure to
    // have processed the gfxItems event.
    executeSoon(checkBlocklist);
  }, "blocklist-data-gfxItems");

  mockGfxBlocklistItemsFromDisk("../data/test_gfxBlocklist.json");
}
