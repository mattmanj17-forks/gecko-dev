// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2024 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plainyearmonth.prototype.tolocalestring
description: toLocaleString returns a string.
features: [Temporal]
---*/

const pym = new Temporal.PlainYearMonth(1, 1);

assert.sameValue(
  typeof pym.toLocaleString(undefined, {calendar: "iso8601"}),
  "string"
);

reportCompare(0, 0);
