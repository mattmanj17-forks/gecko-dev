// |reftest| shell-option(--enable-explicit-resource-management) skip-if(!(this.hasOwnProperty('getBuildConfiguration')&&getBuildConfiguration('explicit-resource-management'))||!xulRuntime.shell) -- explicit-resource-management is not enabled unconditionally, requires shell-options
// Copyright (C) 2023 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-%iteratorprototype%-@@dispose
description: Return value of @@dispose on %IteratorPrototype%
info: |
  %IteratorPrototype% [ @@dispose ] ( )

  1. Let O be the this value.
  2. Let return be ? GetMethod(O, "return").
  3. If return is not undefined, then
    a. Perform ? Call(return, O, « »).
  4. Return undefined.

features: [explicit-resource-management]
---*/
const IteratorPrototype = Object.getPrototypeOf(
  Object.getPrototypeOf([][Symbol.iterator]())
);

assert.sameValue(IteratorPrototype[Symbol.dispose](), undefined);

reportCompare(0, 0);
