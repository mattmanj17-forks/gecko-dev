// |reftest| shell-option(--setpref=atomics_wait_async) skip-if(!this.hasOwnProperty('Atomics')||!xulRuntime.shell) -- Atomics is not enabled unconditionally, requires shell-options
// Copyright (C) 2020 Rick Waldron. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-atomics.waitasync
description: Atomics.waitAsync is callable
features: [Atomics.waitAsync, Atomics]
---*/

assert.sameValue(typeof Atomics.waitAsync, 'function', 'The value of `typeof Atomics.waitAsync` is "function"');

reportCompare(0, 0);
