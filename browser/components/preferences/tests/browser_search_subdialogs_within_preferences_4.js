/*
 * This file contains tests for the Preferences search bar.
 */

/**
 * Test for searching for the "Update History" subdialog.
 */
add_task(async function () {
  // The updates panel is disabled in MSIX builds.
  if (
    AppConstants.platform === "win" &&
    Services.sysinfo.getProperty("hasWinPackageId")
  ) {
    return;
  }
  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });
  await evaluateSearchResults("updates have been installed", "updateApp");
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

/**
 * Test for searching for the "Location Permissions" subdialog.
 */
add_task(async function () {
  await openPreferencesViaOpenPreferencesAPI("paneGeneral", {
    leaveOpen: true,
  });
  await evaluateSearchResults("location permissions", "permissionsGroup");
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
