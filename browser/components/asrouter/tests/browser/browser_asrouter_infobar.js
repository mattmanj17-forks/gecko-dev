/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { InfoBar } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/InfoBar.sys.mjs"
);
const { CFRMessageProvider } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/CFRMessageProvider.sys.mjs"
);
const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);

add_task(async function show_and_send_telemetry() {
  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );

  Assert.ok(message.id, "Found the message");

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    {
      ...message,
      content: {
        priority: window.gNotificationBox.PRIORITY_WARNING_HIGH,
        ...message.content,
      },
    },
    dispatchStub
  );

  Assert.equal(dispatchStub.callCount, 2, "Called twice with IMPRESSION");
  // This is the call to increment impressions for frequency capping
  Assert.equal(dispatchStub.firstCall.args[0].type, "IMPRESSION");
  Assert.equal(dispatchStub.firstCall.args[0].data.id, message.id);
  // This is the telemetry ping
  Assert.equal(dispatchStub.secondCall.args[0].data.event, "IMPRESSION");
  Assert.equal(dispatchStub.secondCall.args[0].data.message_id, message.id);
  Assert.equal(
    infobar.notification.priority,
    window.gNotificationBox.PRIORITY_WARNING_HIGH,
    "Has the priority level set in the message definition"
  );

  let primaryBtn = infobar.notification.buttonContainer.querySelector(
    ".notification-button.primary"
  );

  Assert.ok(primaryBtn, "Has a primary button");
  primaryBtn.click();

  Assert.equal(dispatchStub.callCount, 4, "Called again with CLICK + removed");
  Assert.equal(dispatchStub.thirdCall.args[0].type, "USER_ACTION");
  Assert.equal(
    dispatchStub.lastCall.args[0].data.event,
    "CLICK_PRIMARY_BUTTON"
  );

  await BrowserTestUtils.waitForCondition(
    () => !InfoBar._activeInfobar,
    "Wait for notification to be dismissed by primary btn click."
  );
});

add_task(async function dismiss_telemetry() {
  let message = {
    ...(await CFRMessageProvider.getMessages()).find(
      m => m.id === "INFOBAR_ACTION_86"
    ),
  };
  message.content.type = "tab";

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  // Remove any IMPRESSION pings
  dispatchStub.reset();

  infobar.notification.closeButton.click();

  await BrowserTestUtils.waitForCondition(
    () => infobar.notification === null,
    "Set to null by `removed` event"
  );

  Assert.equal(dispatchStub.callCount, 1, "Only called once");
  Assert.equal(
    dispatchStub.firstCall.args[0].data.event,
    "DISMISSED",
    "Called with dismissed"
  );

  // Remove DISMISSED ping
  dispatchStub.reset();

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  await BrowserTestUtils.waitForCondition(
    () => dispatchStub.callCount > 0,
    "Wait for impression ping"
  );

  // Remove IMPRESSION ping
  dispatchStub.reset();
  BrowserTestUtils.removeTab(tab);

  await BrowserTestUtils.waitForCondition(
    () => infobar.notification === null,
    "Set to null by `disconnect` event"
  );

  // Called by closing the tab and triggering "disconnect"
  Assert.equal(dispatchStub.callCount, 1, "Only called once");
  Assert.equal(
    dispatchStub.firstCall.args[0].data.event,
    "DISMISSED",
    "Called with dismissed"
  );
});

add_task(async function prevent_multiple_messages() {
  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );

  Assert.ok(message.id, "Found the message");

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.equal(dispatchStub.callCount, 2, "Called twice with IMPRESSION");

  // Try to stack 2 notifications
  await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.equal(dispatchStub.callCount, 2, "Impression count did not increase");

  // Dismiss the first notification
  infobar.notification.closeButton.click();
  Assert.equal(InfoBar._activeInfobar, null, "Cleared the active notification");

  // Reset impressions count
  dispatchStub.reset();
  // Try show the message again
  infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );
  Assert.ok(InfoBar._activeInfobar, "activeInfobar is set");
  Assert.equal(dispatchStub.callCount, 2, "Called twice with IMPRESSION");
  // Dismiss the notification again
  infobar.notification.closeButton.click();
  Assert.equal(InfoBar._activeInfobar, null, "Cleared the active notification");
});

add_task(async function default_dismissable_button_shows() {
  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );
  Assert.ok(message, "Found the message");

  // Use the base message which has no dismissable property by default.
  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.ok(
    infobar.notification.closeButton,
    "Default message should display a close button"
  );

  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(
    () => infobar.notification === null,
    "Wait for default message notification to be dismissed."
  );
});

add_task(
  async function non_dismissable_notification_does_not_show_close_button() {
    let baseMessage = (await CFRMessageProvider.getMessages()).find(
      m => m.id === "INFOBAR_ACTION_86"
    );
    Assert.ok(baseMessage, "Found the base message");

    let message = {
      ...baseMessage,
      content: {
        ...baseMessage.content,
        dismissable: false,
      },
    };

    // Add a footer button we can close the infobar with
    message.content.buttons.push({
      label: "Cancel",
      action: {
        type: "CANCEL",
      },
    });

    let dispatchStub = sinon.stub();
    let infobar = await InfoBar.showInfoBarMessage(
      BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
      message,
      dispatchStub
    );

    Assert.ok(
      !infobar.notification.closeButton,
      "Non-dismissable message should not display a close button"
    );

    let cancelButton = infobar.notification.querySelector(
      ".footer-button:not(.primary)"
    );

    Assert.ok(cancelButton, "Non-primary footer button exists");

    cancelButton.click();
    await BrowserTestUtils.waitForCondition(
      () => infobar.notification === null,
      "Wait for default message notification to close."
    );
  }
);
