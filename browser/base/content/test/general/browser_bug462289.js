var tab1, tab2;

function focus_in_navbar() {
  var parent = document.activeElement.parentNode;
  while (parent && parent.id != "nav-bar") {
    parent = parent.parentNode;
  }

  return parent != null;
}

const SCOTCH_BONNET_PREF = "browser.urlbar.scotchBonnet.enableOverride";

function test() {
  Services.prefs.setBoolPref(SCOTCH_BONNET_PREF, false);
  // Put the home button in the pre-proton placement to test focus states.
  CustomizableUI.addWidgetToArea(
    "home-button",
    "nav-bar",
    CustomizableUI.getPlacementOfWidget("stop-reload-button").position + 1
  );
  registerCleanupFunction(async function resetToolbar() {
    Services.prefs.clearUserPref(SCOTCH_BONNET_PREF);
    await CustomizableUI.reset();
  });

  waitForExplicitFinish();

  tab1 = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });
  tab2 = BrowserTestUtils.addTab(gBrowser, "about:blank", {
    skipAnimation: true,
  });

  EventUtils.synthesizeMouseAtCenter(tab1, {});
  setTimeout(step2, 0);
}

function step2() {
  is(gBrowser.selectedTab, tab1, "1st click on tab1 selects tab");
  isnot(
    document.activeElement,
    tab1,
    "1st click on tab1 does not activate tab"
  );

  EventUtils.synthesizeMouseAtCenter(tab1, {});
  setTimeout(step3, 0);
}

async function step3() {
  is(
    gBrowser.selectedTab,
    tab1,
    "2nd click on selected tab1 keeps tab selected"
  );
  isnot(
    document.activeElement,
    tab1,
    "2nd click on selected tab1 does not activate tab"
  );

  info("focusing URLBar then sending 3 Shift+Tab.");
  gURLBar.focus();

  let focused = BrowserTestUtils.waitForEvent(
    document.getElementById("home-button"),
    "focus"
  );
  EventUtils.synthesizeKey("VK_TAB", { shiftKey: true });
  await focused;
  info("Focus is now on Home button");

  focused = BrowserTestUtils.waitForEvent(
    document.getElementById("tabs-newtab-button"),
    "focus"
  );
  EventUtils.synthesizeKey("VK_TAB", { shiftKey: true });
  await focused;
  info("Focus is now on the new tab button");

  focused = BrowserTestUtils.waitForEvent(tab1, "focus");
  EventUtils.synthesizeKey("VK_TAB", { shiftKey: true });
  await focused;
  is(gBrowser.selectedTab, tab1, "tab key to selected tab1 keeps tab selected");
  is(document.activeElement, tab1, "tab key to selected tab1 activates tab");

  EventUtils.synthesizeMouseAtCenter(tab1, {});
  setTimeout(step4, 0);
}

function step4() {
  is(
    gBrowser.selectedTab,
    tab1,
    "3rd click on activated tab1 keeps tab selected"
  );
  is(
    document.activeElement,
    tab1,
    "3rd click on activated tab1 keeps tab activated"
  );

  gBrowser.addEventListener("TabSwitchDone", step5);
  EventUtils.synthesizeMouseAtCenter(tab2, {});
}

function step5() {
  gBrowser.removeEventListener("TabSwitchDone", step5);

  // The tabbox selects a tab within a setTimeout in a bubbling mousedown event
  // listener, and focuses the current tab if another tab previously had focus.
  is(
    gBrowser.selectedTab,
    tab2,
    "click on tab2 while tab1 is activated selects tab"
  );
  is(
    document.activeElement,
    tab2,
    "click on tab2 while tab1 is activated activates tab"
  );

  info("focusing content then sending middle-button mousedown to tab2.");
  gBrowser.selectedBrowser.focus();

  EventUtils.synthesizeMouseAtCenter(tab2, { button: 1, type: "mousedown" });
  setTimeout(step6, 0);
}

function step6() {
  is(
    gBrowser.selectedTab,
    tab2,
    "middle-button mousedown on selected tab2 keeps tab selected"
  );
  isnot(
    document.activeElement,
    tab2,
    "middle-button mousedown on selected tab2 does not activate tab"
  );

  gBrowser.removeTab(tab2);
  gBrowser.removeTab(tab1);

  finish();
}
