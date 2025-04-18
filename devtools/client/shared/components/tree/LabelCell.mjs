/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Component } from "resource://devtools/client/shared/vendor/react.mjs";
import * as dom from "resource://devtools/client/shared/vendor/react-dom-factories.mjs";
import PropTypes from "resource://devtools/client/shared/vendor/react-prop-types.mjs";

/**
 * Render the default cell used for toggle buttons
 */
class LabelCell extends Component {
  // See the TreeView component for details related
  // to the 'member' object.
  static get propTypes() {
    return {
      title: PropTypes.string,
      member: PropTypes.object.isRequired,
      renderSuffix: PropTypes.func,
    };
  }

  render() {
    const title = this.props.title;
    const member = this.props.member;
    const level = member.level || 0;
    const renderSuffix = this.props.renderSuffix;

    const iconClassList = ["treeIcon"];
    if (member.hasChildren && member.loading) {
      iconClassList.push("devtools-throbber");
    } else if (member.hasChildren) {
      iconClassList.push("theme-twisty");
    }
    if (member.open) {
      iconClassList.push("open");
    }

    return dom.td(
      {
        className: "treeLabelCell",
        title,
        style: {
          // Compute indentation dynamically. The deeper the item is
          // inside the hierarchy, the bigger is the left padding.
          "--tree-label-cell-indent": `${level * 16}px`,
        },
        key: "default",
        role: "presentation",
      },
      dom.span({
        className: iconClassList.join(" "),
        role: "presentation",
      }),
      dom.span(
        {
          className: "treeLabel " + member.type + "Label",
          title,
          "data-level": level,
        },
        member.name
      ),
      renderSuffix && renderSuffix(member)
    );
  }
}

// Exports from this module
export default LabelCell;
