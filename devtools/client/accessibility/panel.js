/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const EventEmitter = require("resource://devtools/shared/event-emitter.js");

loader.lazyRequireGetter(
  this,
  "AccessibilityProxy",
  "resource://devtools/client/accessibility/accessibility-proxy.js",
  true
);
loader.lazyRequireGetter(
  this,
  "Picker",
  "resource://devtools/client/accessibility/picker.js",
  true
);

// The panel's window global is an EventEmitter firing the following events:
const EVENTS = {
  // When the accessibility inspector has a new accessible front selected.
  NEW_ACCESSIBLE_FRONT_SELECTED: "Accessibility:NewAccessibleFrontSelected",
  // When the accessibility inspector has a new accessible front highlighted.
  NEW_ACCESSIBLE_FRONT_HIGHLIGHTED:
    "Accessibility:NewAccessibleFrontHighlighted",
  // When the accessibility inspector has a new accessible front inspected.
  NEW_ACCESSIBLE_FRONT_INSPECTED: "Accessibility:NewAccessibleFrontInspected",
  // When the accessibility inspector is updated.
  ACCESSIBILITY_INSPECTOR_UPDATED:
    "Accessibility:AccessibilityInspectorUpdated",
  // When accessibility panel UI is initialized (rendered).
  INITIALIZED: "Accessibility:Initialized",
  // When accessibile object properties are updated in the panel sidebar for a
  // new accessible object.
  PROPERTIES_UPDATED: "Accessibility:PropertiesUpdated",
};

/**
 * This object represents Accessibility panel. It's responsibility is to
 * render Accessibility Tree of the current debugger target and the sidebar that
 * displays current relevant accessible details.
 */
function AccessibilityPanel(iframeWindow, toolbox, commands) {
  this.panelWin = iframeWindow;
  this._toolbox = toolbox;
  this._commands = commands;

  this.onPanelVisibilityChange = this.onPanelVisibilityChange.bind(this);
  this.onNewAccessibleFrontSelected =
    this.onNewAccessibleFrontSelected.bind(this);
  this.onAccessibilityInspectorUpdated =
    this.onAccessibilityInspectorUpdated.bind(this);
  this.updateA11YServiceDurationTimer =
    this.updateA11YServiceDurationTimer.bind(this);
  this.forceUpdatePickerButton = this.forceUpdatePickerButton.bind(this);
  this.onLifecycleEvent = this.onLifecycleEvent.bind(this);

  EventEmitter.decorate(this);
}

AccessibilityPanel.prototype = {
  /**
   * Open is effectively an asynchronous constructor.
   */
  async open() {
    if (this._opening) {
      await this._opening;
      return this._opening;
    }

    // This first promise includes initialization of proxy *and* the call to forceRefresh
    let resolver;
    this._opening = new Promise(resolve => {
      resolver = resolve;
    });

    // This second promise only include the initialization of proxy and few other things,
    // but not the call to forceRefresh.
    const { promise, resolve } = Promise.withResolvers();
    this.initializedPromise = promise;

    this._telemetry = this._toolbox.telemetry;
    this.panelWin.gTelemetry = this._telemetry;

    this._toolbox.on("select", this.onPanelVisibilityChange);

    this.panelWin.EVENTS = EVENTS;
    EventEmitter.decorate(this.panelWin);
    this.panelWin.on(
      EVENTS.NEW_ACCESSIBLE_FRONT_SELECTED,
      this.onNewAccessibleFrontSelected
    );
    this.panelWin.on(
      EVENTS.ACCESSIBILITY_INSPECTOR_UPDATED,
      this.onAccessibilityInspectorUpdated
    );

    this.picker = new Picker(this);
    this.fluentBundles = await this.createFluentBundles();

    this.accessibilityProxy = new AccessibilityProxy(this._commands, this);

    await this.accessibilityProxy.initialize();

    this.accessibilityProxy.startListeningForLifecycleEvents({
      init: this.onLifecycleEvent,
      shutdown: this.onLifecycleEvent,
    });

    // Start recording the duration where a11y service is enabled via the proxy.
    this.updateA11YServiceDurationTimer();

    // Resolve the `this.initializedPromise`
    resolve();

    // Force rendering the panel once everything is initialized
    await this.forceRefresh();

    resolver(this);
    return this._opening;
  },

  /**
   * Retrieve message contexts for the current locales, and return them as an
   * array of FluentBundles elements.
   */
  async createFluentBundles() {
    const locales = Services.locale.appLocalesAsBCP47;
    const generator = L10nRegistry.getInstance().generateBundles(locales, [
      "devtools/client/accessibility.ftl",
    ]);

    // Return value of generateBundles is a generator and should be converted to
    // a sync iterable before using it with React.
    const contexts = [];
    for await (const message of generator) {
      contexts.push(message);
    }

    return contexts;
  },

  onLifecycleEvent() {
    this.updateA11YServiceDurationTimer();
    this.forceUpdatePickerButton();
  },

  onNewAccessibleFrontSelected(selected) {
    this.emit("new-accessible-front-selected", selected);
  },

  onAccessibilityInspectorUpdated() {
    this.emit("accessibility-inspector-updated");
  },

  /**
   * Make sure the panel is refreshed when the page is reloaded. The panel is
   * refreshed immediatelly if it's currently selected or lazily when the user
   * actually selects it.
   */
  async forceRefresh() {
    this.shouldRefresh = true;

    // Wait for initialization to be done, in case this is called early on.
    await this.initializedPromise;
    const onUpdated = this.panelWin.once(EVENTS.INITIALIZED);
    this.refresh();
    await onUpdated;

    this.emit("reloaded");
  },

  /**
   * Make sure the panel is refreshed (if needed) when it's selected.
   */
  onPanelVisibilityChange() {
    this._opening.then(() => this.refresh());
  },

  refresh() {
    this.cancelPicker();

    if (!this.isVisible) {
      // Do not refresh if the panel isn't visible.
      return;
    }

    // Do not refresh if it isn't necessary.
    if (!this.shouldRefresh) {
      return;
    }
    // Alright reset the flag we are about to refresh the panel.
    this.shouldRefresh = false;
    const {
      supports,
      getAccessibilityTreeRoot,
      startListeningForAccessibilityEvents,
      stopListeningForAccessibilityEvents,
      audit,
      simulate,
      toggleDisplayTabbingOrder,
      enableAccessibility,
      resetAccessiblity,
      startListeningForLifecycleEvents,
      stopListeningForLifecycleEvents,
      startListeningForParentLifecycleEvents,
      stopListeningForParentLifecycleEvents,
      highlightAccessible,
      unhighlightAccessible,
    } = this.accessibilityProxy;
    this.postContentMessage("initialize", {
      fluentBundles: this.fluentBundles,
      toolbox: this._toolbox,
      supports,
      getAccessibilityTreeRoot,
      startListeningForAccessibilityEvents,
      stopListeningForAccessibilityEvents,
      audit,
      simulate,
      toggleDisplayTabbingOrder,
      enableAccessibility,
      resetAccessiblity,
      startListeningForLifecycleEvents,
      stopListeningForLifecycleEvents,
      startListeningForParentLifecycleEvents,
      stopListeningForParentLifecycleEvents,
      highlightAccessible,
      unhighlightAccessible,
    });
  },

  updateA11YServiceDurationTimer() {
    if (this.accessibilityProxy.enabled) {
      this._timerID = Glean.devtools.accessibilityServiceTimeActive.start();
    } else if (this._timerID) {
      Glean.devtools.accessibilityServiceTimeActive.stopAndAccumulate(
        this._timerID
      );
      this._timerID = null;
    }
  },

  selectAccessible(accessibleFront) {
    this.postContentMessage("selectAccessible", accessibleFront);
  },

  selectAccessibleForNode(nodeFront, reason) {
    if (reason) {
      Glean.devtoolsAccessibility.selectAccessibleForNode[reason].add(1);
    }

    this.postContentMessage("selectNodeAccessible", nodeFront);
  },

  highlightAccessible(accessibleFront) {
    this.postContentMessage("highlightAccessible", accessibleFront);
  },

  postContentMessage(type, ...args) {
    const event = new this.panelWin.MessageEvent("devtools/chrome/message", {
      bubbles: true,
      cancelable: true,
      data: { type, args },
    });

    this.panelWin.dispatchEvent(event);
  },

  updatePickerButton() {
    this.picker && this.picker.updateButton();
  },

  forceUpdatePickerButton() {
    // Only update picker button when the panel is selected.
    if (!this.isVisible) {
      return;
    }

    this.updatePickerButton();
    // Calling setToolboxButtons to make sure toolbar is forced to re-render.
    this._toolbox.component.setToolboxButtons(this._toolbox.toolbarButtons);
  },

  togglePicker() {
    this.picker && this.picker.toggle();
  },

  cancelPicker() {
    this.picker && this.picker.cancel();
  },

  stopPicker() {
    this.picker && this.picker.stop();
  },

  /**
   * Return true if the Accessibility panel is currently selected.
   */
  get isVisible() {
    return this._toolbox.currentToolId === "accessibility";
  },

  destroy() {
    if (this._destroyed) {
      return;
    }
    this._destroyed = true;

    this.postContentMessage("destroy");

    if (this.accessibilityProxy) {
      this.accessibilityProxy.stopListeningForLifecycleEvents({
        init: this.onLifecycleEvent,
        shutdown: this.onLifecycleEvent,
      });
      this.accessibilityProxy.destroy();
      this.accessibilityProxy = null;
      this.initializedPromise = null;
    }

    this._toolbox.off("select", this.onPanelVisibilityChange);

    this.panelWin.off(
      EVENTS.NEW_ACCESSIBLE_FRONT_SELECTED,
      this.onNewAccessibleFrontSelected
    );
    this.panelWin.off(
      EVENTS.ACCESSIBILITY_INSPECTOR_UPDATED,
      this.onAccessibilityInspectorUpdated
    );

    // Older versions of devtools server do not support picker functionality.
    if (this.picker) {
      this.picker.release();
      this.picker = null;
    }

    this._telemetry = null;
    this.panelWin.gTelemetry = null;

    this.emit("destroyed");
  },
};

// Exports from this module
exports.AccessibilityPanel = AccessibilityPanel;
