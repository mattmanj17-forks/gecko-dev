/*
 *  Copyright 2014 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import <Foundation/Foundation.h>

#import "sdk/objc/base/RTCMacros.h"

@class RTC_OBJC_TYPE(RTCIceServer);

@protocol ARDTURNClient <NSObject>

// Returns TURN server urls if successful.
- (void)requestServersWithCompletionHandler:
    (void (^)(NSArray *turnServers, NSError *error))completionHandler;

@end
