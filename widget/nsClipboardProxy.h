/* -*- Mode: c++; c-basic-offset: 2; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NS_CLIPBOARD_PROXY_H
#define NS_CLIPBOARD_PROXY_H

#include "mozilla/dom/PContent.h"
#include "nsIClipboard.h"

#define NS_CLIPBOARDPROXY_IID \
  {0xa64c82da, 0x7326, 0x4681, {0xa0, 0x95, 0x81, 0x2c, 0xc9, 0x86, 0xe6, 0xde}}

// Hack for ContentChild to be able to know that we're an nsClipboardProxy.
class nsIClipboardProxy : public nsIClipboard {
 protected:
  typedef mozilla::dom::ClipboardCapabilities ClipboardCapabilities;

 public:
  NS_INLINE_DECL_STATIC_IID(NS_CLIPBOARDPROXY_IID)

  virtual void SetCapabilities(const ClipboardCapabilities& aClipboardCaps) = 0;
};

class nsClipboardProxy final : public nsIClipboardProxy {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICLIPBOARD

  nsClipboardProxy();

  virtual void SetCapabilities(
      const ClipboardCapabilities& aClipboardCaps) override;

 private:
  ~nsClipboardProxy() = default;

  ClipboardCapabilities mClipboardCaps;
};

#endif
