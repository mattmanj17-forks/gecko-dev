/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_PLATFORM_WMF_MFTENCODER_H
#define DOM_MEDIA_PLATFORM_WMF_MFTENCODER_H

#include <wrl.h>
#include <deque>
#include <functional>
#include <queue>

#include "EncoderConfig.h"
#include "WMF.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ResultVariant.h"
#include "nsDeque.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"

namespace mozilla {

class MFTEncoder final {
 public:
  struct InputSample {
    RefPtr<IMFSample> mSample{};
    bool mKeyFrameRequested = false;
  };
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MFTEncoder)

  enum class HWPreference {
    HardwareOnly,
    SoftwareOnly,
    PreferHardware,
    PreferSoftware
  };
  explicit MFTEncoder(const HWPreference aHWPreference)
      : mHWPreference(aHWPreference) {}

  HRESULT Create(const GUID& aSubtype, const gfx::IntSize& aFrameSize,
                 const EncoderConfig::CodecSpecific& aCodecSpecific);
  HRESULT Destroy();
  HRESULT SetMediaTypes(IMFMediaType* aInputType, IMFMediaType* aOutputType);
  HRESULT SetModes(const EncoderConfig& aConfig);
  HRESULT SetBitrate(UINT32 aBitsPerSec);

  HRESULT CreateInputSample(RefPtr<IMFSample>* aSample, size_t aSize);
  HRESULT PushInput(const InputSample& aInput);
  HRESULT TakeOutput(nsTArray<RefPtr<IMFSample>>& aOutput);
  HRESULT Drain(nsTArray<RefPtr<IMFSample>>& aOutput);

  HRESULT GetMPEGSequenceHeader(nsTArray<UINT8>& aHeader);

  static nsCString GetFriendlyName(const GUID& aSubtype);

  struct Info final {
    GUID mSubtype;
    nsCString mName;
  };
  struct Factory {
    MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING_AT_CLASS_SCOPE(
        Provider, (HW_AMD, HW_Intel, HW_NVIDIA, HW_Qualcomm, HW_Unknown, SW))

    Provider mProvider;
    Microsoft::WRL::ComPtr<IMFActivate> mActivate;
    nsCString mName;

    Factory(Provider aProvider,
            Microsoft::WRL::ComPtr<IMFActivate>&& aActivate);
    Factory(Factory&& aOther) = default;
    Factory(const Factory& aOther) = delete;
    ~Factory();

    explicit operator bool() const { return mActivate; }

    HRESULT Shutdown();
  };

 private:
  // Abstractions to support sync MFTs using the same logic for async MFTs.
  // When the MFT is async and a real event generator is available, simply
  // forward the calls. For sync MFTs, use the synchronous processing model
  // described in
  // https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model#process-data
  // to generate events of the asynchronous processing model.
  using Event = Result<MediaEventType, HRESULT>;
  using EventQueue = std::queue<MediaEventType>;
  class EventSource final {
   public:
    EventSource() : mImpl(Nothing{}) {}

    void SetAsyncEventGenerator(
        already_AddRefed<IMFMediaEventGenerator>&& aAsyncEventGenerator) {
      MOZ_ASSERT(mImpl.is<Nothing>());
      mImpl.emplace<RefPtr<IMFMediaEventGenerator>>(aAsyncEventGenerator);
    }

    void InitSyncMFTEventQueue() {
      MOZ_ASSERT(mImpl.is<Nothing>());
      mImpl.emplace<UniquePtr<EventQueue>>(MakeUnique<EventQueue>());
    }

    bool IsSync() const { return mImpl.is<UniquePtr<EventQueue>>(); }

    Event GetEvent();
    // Push an event when sync MFT is used.
    HRESULT QueueSyncMFTEvent(MediaEventType aEventType);

   private:
    // Pop an event from the queue when sync MFT is used.
    Event GetSyncMFTEvent();

    Variant<
        // Uninitialized.
        Nothing,
        // For async MFT events. See
        // https://docs.microsoft.com/en-us/windows/win32/medfound/asynchronous-mfts#events
        RefPtr<IMFMediaEventGenerator>,
        // Event queue for a sync MFT. Storing EventQueue directly breaks the
        // code so a pointer is introduced.
        UniquePtr<EventQueue>>
        mImpl;
#ifdef DEBUG
    bool IsOnCurrentThread();
    nsCOMPtr<nsISerialEventTarget> mThread;
#endif
  };

  ~MFTEncoder() { Destroy(); };

  static nsTArray<Info>& Infos();
  static nsTArray<Info> Enumerate();
  static Maybe<Info> GetInfo(const GUID& aSubtype);

  // Return true when successfully enabled, false for MFT that doesn't support
  // async processing model, and error otherwise.
  using AsyncMFTResult = Result<bool, HRESULT>;
  AsyncMFTResult AttemptEnableAsync();
  HRESULT GetStreamIDs();
  GUID MatchInputSubtype(IMFMediaType* aInputType);
  HRESULT SendMFTMessage(MFT_MESSAGE_TYPE aMsg, ULONG_PTR aData);

  HRESULT ProcessEvents();
  HRESULT ProcessInput();
  HRESULT ProcessOutput();

  MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING_AT_CLASS_SCOPE(DrainState,
                                                     (DRAINED, DRAINABLE,
                                                      DRAINING));
  void SetDrainState(DrainState aState);

  const HWPreference mHWPreference;
  RefPtr<IMFTransform> mEncoder;
  // For MFT object creation. See
  // https://docs.microsoft.com/en-us/windows/win32/medfound/activation-objects
  Maybe<Factory> mFactory;
  // For encoder configuration. See
  // https://docs.microsoft.com/en-us/windows/win32/directshow/encoder-api
  RefPtr<ICodecAPI> mConfig;

  DWORD mInputStreamID;
  DWORD mOutputStreamID;
  MFT_INPUT_STREAM_INFO mInputStreamInfo;
  MFT_OUTPUT_STREAM_INFO mOutputStreamInfo;
  bool mOutputStreamProvidesSample;

  size_t mNumNeedInput;
  DrainState mDrainState = DrainState::DRAINABLE;

  std::deque<InputSample> mPendingInputs;
  nsTArray<RefPtr<IMFSample>> mOutputs;

  EventSource mEventSource;
};

}  // namespace mozilla

#endif  // DOM_MEDIA_PLATFORM_WMF_MFTENCODER_H
