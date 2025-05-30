/**
 * @file Enforce the standard object name for
 * ChromeUtils.defineESModuleGetters
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

function isIdentifier(node, id) {
  return node.type === "Identifier" && node.name === id;
}

export default {
  meta: {
    docs: {
      url: "https://firefox-source-docs.mozilla.org/code-quality/lint/linters/eslint-plugin-mozilla/rules/lazy-getter-object-name.html",
    },
    messages: {
      mustUseLazy:
        "The variable name of the object passed to ChromeUtils.defineESModuleGetters must be `lazy`",
    },
    schema: [],
    type: "problem",
  },

  create(context) {
    return {
      CallExpression(node) {
        let { callee } = node;
        if (
          callee.type === "MemberExpression" &&
          isIdentifier(callee.object, "ChromeUtils") &&
          isIdentifier(callee.property, "defineESModuleGetters") &&
          node.arguments.length >= 1 &&
          !isIdentifier(node.arguments[0], "lazy")
        ) {
          context.report({
            node,
            messageId: "mustUseLazy",
          });
        }
      },
    };
  },
};
