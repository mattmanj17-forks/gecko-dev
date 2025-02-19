// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-temporal.now.zoneddatetimeiso
description: Temporal.Now.zonedDateTimeISO is extensible.
features: [Temporal]
---*/

assert(
  Object.isExtensible(Temporal.Now.zonedDateTimeISO),
  'Object.isExtensible(Temporal.Now.zonedDateTimeISO) must return true'
);

reportCompare(0, 0);
