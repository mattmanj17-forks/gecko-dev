/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * This file tests urlbar telemetry for tip results.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  UrlbarProvider: "resource:///modules/UrlbarUtils.sys.mjs",
  UrlbarProvidersManager: "resource:///modules/UrlbarProvidersManager.sys.mjs",
  UrlbarResult: "resource:///modules/UrlbarResult.sys.mjs",
  UrlbarTestUtils: "resource://testing-common/UrlbarTestUtils.sys.mjs",
});

function snapshotHistograms() {
  Services.telemetry.clearEvents();
  return {
    resultMethodHist: TelemetryTestUtils.getAndClearHistogram(
      "FX_URLBAR_SELECTED_RESULT_METHOD"
    ),
    search_hist: TelemetryTestUtils.getAndClearKeyedHistogram("SEARCH_COUNTS"),
  };
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Disable search suggestions in the urlbar.
      ["browser.urlbar.suggest.searches", false],
      // Turn autofill off.
      ["browser.urlbar.autoFill", false],
    ],
  });
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test() {
  // Add a restricting provider that returns a preselected heuristic tip result.
  let provider = new TipProvider([
    Object.assign(
      new UrlbarResult(
        UrlbarUtils.RESULT_TYPE.TIP,
        UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
        {
          helpUrl: "https://example.com/",
          type: "test",
          titleL10n: { id: "urlbar-search-tips-confirm" },
          buttons: [
            {
              url: "https://example.com/",
              l10n: { id: "urlbar-search-tips-confirm" },
            },
          ],
        }
      ),
      { heuristic: true }
    ),
  ]);
  UrlbarProvidersManager.registerProvider(provider);

  const histograms = snapshotHistograms();

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  // Show the view and press enter to select the tip.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    waitForFocus,
    value: "test",
    fireInputEvent: true,
  });
  EventUtils.synthesizeKey("KEY_Enter");

  TelemetryTestUtils.assertHistogram(
    histograms.resultMethodHist,
    UrlbarTestUtils.SELECTED_RESULT_METHODS.enter,
    1
  );

  UrlbarProvidersManager.unregisterProvider(provider);
  BrowserTestUtils.removeTab(tab);
});

/**
 * A test URLBar provider.
 */
class TipProvider extends UrlbarProvider {
  constructor(results) {
    super();
    this.results = results;
  }
  get name() {
    return "TestProviderTip";
  }
  get type() {
    return UrlbarUtils.PROVIDER_TYPE.PROFILE;
  }
  isActive(_context) {
    return true;
  }
  getPriority(_context) {
    return 1;
  }
  async startQuery(context, addCallback) {
    context.preselected = true;
    for (const result of this.results) {
      addCallback(this, result);
    }
  }
}
