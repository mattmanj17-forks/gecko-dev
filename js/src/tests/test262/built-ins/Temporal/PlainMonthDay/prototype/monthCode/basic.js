// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-get-temporal.plainmonthday.prototype.monthcode
description: Basic tests for monthCode().
features: [Temporal]
---*/

const md = new Temporal.PlainMonthDay(1, 15);
assert.sameValue(md.monthCode, "M01");

reportCompare(0, 0);
