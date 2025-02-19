/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * This file tests urlbar telemetry for dynamic results.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  UrlbarProvidersManager: "resource:///modules/UrlbarProvidersManager.sys.mjs",
  UrlbarResult: "resource:///modules/UrlbarResult.sys.mjs",
  UrlbarTestUtils: "resource://testing-common/UrlbarTestUtils.sys.mjs",
  UrlbarView: "resource:///modules/UrlbarView.sys.mjs",
});

const DYNAMIC_TYPE_NAME = "test";

/**
 * A test URLBar provider.
 */
class TestProvider extends UrlbarTestUtils.TestProvider {
  constructor() {
    super({
      priority: Infinity,
      results: [
        Object.assign(
          new UrlbarResult(
            UrlbarUtils.RESULT_TYPE.DYNAMIC,
            UrlbarUtils.RESULT_SOURCE.OTHER_LOCAL,
            {
              dynamicType: DYNAMIC_TYPE_NAME,
            }
          ),
          { heuristic: true }
        ),
      ],
    });
  }

  getViewUpdate(_result, _idsByName) {
    return {
      title: {
        textContent: "This is a dynamic result.",
      },
      button: {
        textContent: "Click Me",
      },
    };
  }
}

add_task(async function test() {
  // Add a dynamic result type.
  UrlbarResult.addDynamicResultType(DYNAMIC_TYPE_NAME);
  UrlbarView.addDynamicViewTemplate(DYNAMIC_TYPE_NAME, {
    stylesheet:
      getRootDirectory(gTestPath) + "urlbarTelemetryUrlbarDynamic.css",
    children: [
      {
        name: "title",
        tag: "span",
      },
      {
        name: "buttonSpacer",
        tag: "span",
      },
      {
        name: "button",
        tag: "span",
        attributes: {
          role: "button",
        },
      },
    ],
  });
  registerCleanupFunction(() => {
    UrlbarView.removeDynamicViewTemplate(DYNAMIC_TYPE_NAME);
    UrlbarResult.removeDynamicResultType(DYNAMIC_TYPE_NAME);
  });

  // Register a provider that returns the dynamic result type.
  let provider = new TestProvider();
  UrlbarProvidersManager.registerProvider(provider);
  registerCleanupFunction(() => {
    UrlbarProvidersManager.unregisterProvider(provider);
  });

  const histograms = snapshotHistograms();

  // Do a search to show the dynamic result.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    waitForFocus,
    value: "test",
    fireInputEvent: true,
  });

  // Press enter on the result's button.  It will be preselected since the
  // result is the heuristic.
  await UrlbarTestUtils.promisePopupClose(window, () =>
    EventUtils.synthesizeKey("KEY_Enter")
  );

  TelemetryTestUtils.assertHistogram(
    histograms.resultMethodHist,
    UrlbarTestUtils.SELECTED_RESULT_METHODS.enter,
    1
  );

  // Clean up for subsequent tests.
  gURLBar.handleRevert();
});

function snapshotHistograms() {
  Services.telemetry.clearEvents();
  return {
    resultMethodHist: TelemetryTestUtils.getAndClearHistogram(
      "FX_URLBAR_SELECTED_RESULT_METHOD"
    ),
  };
}
