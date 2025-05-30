/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// ------------------------------------------------------------------------------
// Requirements
// ------------------------------------------------------------------------------

import os from "os";
import rule from "../lib/rules/valid-ci-uses.mjs";
import { RuleTester } from "eslint";

const ruleTester = new RuleTester();

// ------------------------------------------------------------------------------
// Tests
// ------------------------------------------------------------------------------

function invalidCode(code, messageId, data) {
  return { code, errors: [{ messageId, data }] };
}

process.env.MOZ_XPT_ARTIFACTS_DIR = `${import.meta.dirname}/xpidl`;

const tests = {
  valid: ["Ci.nsIURIFixup", "Ci.nsIURIFixup.FIXUP_FLAG_NONE"],
  invalid: [
    invalidCode("Ci.nsIURIFixup.UNKNOWN_CONSTANT", "unknownProperty", {
      interface: "nsIURIFixup",
      property: "UNKNOWN_CONSTANT",
    }),
    invalidCode("Ci.nsIFoo", "unknownInterface", {
      interface: "nsIFoo",
    }),
  ],
};

// For ESLint tests, we only have a couple of xpt examples in the xpidl directory.
// Therefore we can pretend that these interfaces no longer exist.
switch (os.platform()) {
  case "win32":
    tests.invalid.push(
      invalidCode("Ci.nsIJumpListShortcut", "missingInterface", {
        interface: "nsIJumpListShortcut",
      })
    );
    break;
  case "darwin":
    tests.invalid.push(
      invalidCode("Ci.nsIMacShellService", "missingInterface", {
        interface: "nsIMacShellService",
      })
    );
    break;
  case "linux":
    tests.invalid.push(
      invalidCode("Ci.mozISandboxReporter", "missingInterface", {
        interface: "mozISandboxReporter",
      })
    );
}

ruleTester.run("valid-ci-uses", rule, tests);
