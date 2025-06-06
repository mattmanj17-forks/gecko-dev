/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createElement,
  createFactory,
  Fragment,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const {
  connect,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

const DevicePixelRatioMenu = createFactory(
  require("resource://devtools/client/responsive/components/DevicePixelRatioMenu.js")
);
const DeviceSelector = createFactory(
  require("resource://devtools/client/responsive/components/DeviceSelector.js")
);
const NetworkThrottlingMenu = createFactory(
  require("resource://devtools/client/shared/components/throttling/NetworkThrottlingMenu.js")
);
const SettingsMenu = createFactory(
  require("resource://devtools/client/responsive/components/SettingsMenu.js")
);
const ViewportDimension = createFactory(
  require("resource://devtools/client/responsive/components/ViewportDimension.js")
);

loader.lazyGetter(this, "UserAgentInput", function () {
  return createFactory(
    require("resource://devtools/client/responsive/components/UserAgentInput.js")
  );
});

const {
  getStr,
} = require("resource://devtools/client/responsive/utils/l10n.js");
const Types = require("resource://devtools/client/responsive/types.js");

class Toolbar extends PureComponent {
  static get propTypes() {
    return {
      devices: PropTypes.shape(Types.devices).isRequired,
      displayPixelRatio: PropTypes.number.isRequired,
      leftAlignmentEnabled: PropTypes.bool.isRequired,
      networkThrottling: PropTypes.shape(Types.networkThrottling).isRequired,
      onChangeDevice: PropTypes.func.isRequired,
      onChangeNetworkThrottling: PropTypes.func.isRequired,
      onChangePixelRatio: PropTypes.func.isRequired,
      onChangeTouchSimulation: PropTypes.func.isRequired,
      onChangeUserAgent: PropTypes.func.isRequired,
      onExit: PropTypes.func.isRequired,
      onRemoveDeviceAssociation: PropTypes.func.isRequired,
      doResizeViewport: PropTypes.func.isRequired,
      onRotateViewport: PropTypes.func.isRequired,
      onScreenshot: PropTypes.func.isRequired,
      onToggleLeftAlignment: PropTypes.func.isRequired,
      onToggleReloadOnTouchSimulation: PropTypes.func.isRequired,
      onToggleReloadOnUserAgent: PropTypes.func.isRequired,
      onToggleUserAgentInput: PropTypes.func.isRequired,
      onUpdateDeviceModal: PropTypes.func.isRequired,
      screenshot: PropTypes.shape(Types.screenshot).isRequired,
      selectedDevice: PropTypes.string.isRequired,
      selectedPixelRatio: PropTypes.number.isRequired,
      showUserAgentInput: PropTypes.bool.isRequired,
      touchSimulationEnabled: PropTypes.bool.isRequired,
      viewport: PropTypes.shape(Types.viewport).isRequired,
    };
  }

  renderUserAgent() {
    const { onChangeUserAgent, showUserAgentInput, viewport, devices } =
      this.props;

    if (!showUserAgentInput) {
      return null;
    }

    const selectedDeviceName = viewport.device;
    let selectedDeviceUserAgent = null;
    if (selectedDeviceName) {
      // Find the matching device by name
      for (const type of devices.types) {
        for (const device of devices[type]) {
          if (device.name == selectedDeviceName) {
            selectedDeviceUserAgent = device.userAgent;
            break;
          }
        }
        if (selectedDeviceUserAgent) {
          break;
        }
      }
    }

    return createElement(
      Fragment,
      null,
      UserAgentInput({
        onChangeUserAgent,
        selectedDeviceName,
        selectedDeviceUserAgent,
      }),
      dom.div({ className: "devtools-separator" })
    );
  }

  render() {
    const {
      devices,
      displayPixelRatio,
      leftAlignmentEnabled,
      networkThrottling,
      onChangeDevice,
      onChangeNetworkThrottling,
      onChangePixelRatio,
      onChangeTouchSimulation,
      onExit,
      onRemoveDeviceAssociation,
      doResizeViewport,
      onRotateViewport,
      onScreenshot,
      onToggleLeftAlignment,
      onToggleReloadOnTouchSimulation,
      onToggleReloadOnUserAgent,
      onToggleUserAgentInput,
      onUpdateDeviceModal,
      screenshot,
      selectedDevice,
      selectedPixelRatio,
      showUserAgentInput,
      touchSimulationEnabled,
      viewport,
    } = this.props;

    return dom.header(
      {
        id: "toolbar",
        className: [
          leftAlignmentEnabled ? "left-aligned" : "",
          showUserAgentInput ? "user-agent" : "",
        ]
          .join(" ")
          .trim(),
      },
      dom.div(
        { id: "toolbar-center-controls" },
        DeviceSelector({
          devices,
          onChangeDevice,
          onUpdateDeviceModal,
          selectedDevice,
          viewportId: viewport.id,
        }),
        dom.div({ className: "devtools-separator" }),
        ViewportDimension({
          onRemoveDeviceAssociation,
          doResizeViewport,
          viewport,
        }),
        dom.button({
          id: "rotate-button",
          className: `devtools-button viewport-orientation-${
            viewport.width > viewport.height ? "landscape" : "portrait"
          }`,
          onClick: () => onRotateViewport(viewport.id),
          title: getStr("responsive.rotate"),
        }),
        dom.div({ className: "devtools-separator" }),
        DevicePixelRatioMenu({
          devices,
          displayPixelRatio,
          onChangePixelRatio,
          selectedDevice,
          selectedPixelRatio,
        }),
        dom.div({ className: "devtools-separator" }),
        NetworkThrottlingMenu({
          // NetworkThrottlingMenu expects to display the Menu in the toolbox document
          // but for RDM we can just use window.document, same as for the device
          // selector MenuButton.
          toolboxDoc: window.document,
          networkThrottling,
          onChangeNetworkThrottling,
        }),
        dom.div({ className: "devtools-separator" }),
        this.renderUserAgent(),
        dom.button({
          id: "touch-simulation-button",
          className:
            "devtools-button" + (touchSimulationEnabled ? " checked" : ""),
          title: touchSimulationEnabled
            ? getStr("responsive.disableTouch")
            : getStr("responsive.enableTouch"),
          onClick: () => onChangeTouchSimulation(!touchSimulationEnabled),
        })
      ),
      dom.div(
        { id: "toolbar-end-controls" },
        dom.button({
          id: "screenshot-button",
          className: "devtools-button",
          title: getStr("responsive.screenshot"),
          onClick: onScreenshot,
          disabled: screenshot.isCapturing,
        }),
        SettingsMenu({
          onToggleLeftAlignment,
          onToggleReloadOnTouchSimulation,
          onToggleReloadOnUserAgent,
          onToggleUserAgentInput,
        }),
        dom.div({ className: "devtools-separator" }),
        dom.button({
          id: "exit-button",
          className: "devtools-button",
          title: getStr("responsive.exit"),
          onClick: onExit,
        })
      )
    );
  }
}

const mapStateToProps = state => {
  return {
    displayPixelRatio: state.ui.displayPixelRatio,
    leftAlignmentEnabled: state.ui.leftAlignmentEnabled,
    showUserAgentInput: state.ui.showUserAgentInput,
    touchSimulationEnabled: state.ui.touchSimulationEnabled,
  };
};

module.exports = connect(mapStateToProps)(Toolbar);
