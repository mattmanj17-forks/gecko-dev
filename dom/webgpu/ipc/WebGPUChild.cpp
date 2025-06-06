/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGPUChild.h"

#include "js/RootingAPI.h"
#include "js/String.h"
#include "js/TypeDecls.h"
#include "js/Value.h"
#include "js/Warnings.h"  // JS::WarnUTF8
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/EnumTypeTraits.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/dom/GPUUncapturedErrorEvent.h"
#include "mozilla/webgpu/ValidationError.h"
#include "mozilla/webgpu/WebGPUTypes.h"
#include "mozilla/webgpu/ffi/wgpu.h"
#include "Adapter.h"
#include "DeviceLostInfo.h"
#include "PipelineLayout.h"
#include "Sampler.h"
#include "CompilationInfo.h"
#include "Utility.h"

#include <utility>

namespace mozilla::webgpu {

NS_IMPL_CYCLE_COLLECTION(WebGPUChild)

void WebGPUChild::JsWarning(nsIGlobalObject* aGlobal,
                            const nsACString& aMessage) {
  const auto& flatString = PromiseFlatCString(aMessage);
  if (aGlobal) {
    dom::AutoJSAPI api;
    if (api.Init(aGlobal)) {
      JS::WarnUTF8(api.cx(), "Uncaptured WebGPU error: %s", flatString.get());
    }
  } else {
    printf_stderr("Uncaptured WebGPU error without device target: %s\n",
                  flatString.get());
  }
}

static UniquePtr<ffi::WGPUClient> initialize() {
  ffi::WGPUInfrastructure infra = ffi::wgpu_client_new();
  return UniquePtr<ffi::WGPUClient>{infra.client};
}

WebGPUChild::WebGPUChild() : mClient(initialize()) {}

WebGPUChild::~WebGPUChild() = default;

RefPtr<AdapterPromise> WebGPUChild::InstanceRequestAdapter(
    const dom::GPURequestAdapterOptions& aOptions) {
  RawId id = ffi::wgpu_client_make_adapter_id(mClient.get());
  auto* client = mClient.get();

  return SendInstanceRequestAdapter(aOptions, id)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [client, id](ipc::ByteBuf&& aInfoBuf) {
            // Ideally, we'd just send an empty ByteBuf, but the IPC code
            // complains if the capacity is zero...
            // So for the case where an adapter wasn't found, we just
            // transfer a single 0u64 in this buffer.
            if (aInfoBuf.mLen > sizeof(uint64_t)) {
              return AdapterPromise::CreateAndResolve(std::move(aInfoBuf),
                                                      __func__);
            } else {
              if (client) {
                ffi::wgpu_client_free_adapter_id(client, id);
              }
              return AdapterPromise::CreateAndReject(Nothing(), __func__);
            }
          },
          [client, id](const ipc::ResponseRejectReason& aReason) {
            if (client) {
              ffi::wgpu_client_free_adapter_id(client, id);
            }
            return AdapterPromise::CreateAndReject(Some(aReason), __func__);
          });
}

Maybe<DeviceRequest> WebGPUChild::AdapterRequestDevice(
    RawId aSelfId, const ffi::WGPUFfiDeviceDescriptor& aDesc) {
  ffi::WGPUDeviceQueueId ids =
      ffi::wgpu_client_make_device_queue_id(mClient.get());

  ByteBuf bb;
  ffi::wgpu_client_serialize_device_descriptor(&aDesc, ToFFI(&bb));

  DeviceRequest request;
  request.mDeviceId = ids.device;
  request.mQueueId = ids.queue;
  request.mPromise =
      SendAdapterRequestDevice(aSelfId, std::move(bb), ids.device, ids.queue);

  return Some(std::move(request));
}

RawId WebGPUChild::RenderBundleEncoderFinish(
    ffi::WGPURenderBundleEncoder& aEncoder, RawId aDeviceId,
    const dom::GPURenderBundleDescriptor& aDesc) {
  ffi::WGPURenderBundleDescriptor desc = {};

  webgpu::StringHelper label(aDesc.mLabel);
  desc.label = label.Get();

  ipc::ByteBuf bb;
  RawId id = ffi::wgpu_client_create_render_bundle(mClient.get(), &aEncoder,
                                                   &desc, ToFFI(&bb));

  SendDeviceAction(aDeviceId, std::move(bb));

  return id;
}

RawId WebGPUChild::RenderBundleEncoderFinishError(RawId aDeviceId,
                                                  const nsString& aLabel) {
  webgpu::StringHelper label(aLabel);

  ipc::ByteBuf bb;
  RawId id = ffi::wgpu_client_create_render_bundle_error(
      mClient.get(), label.Get(), ToFFI(&bb));

  SendDeviceAction(aDeviceId, std::move(bb));

  return id;
}

ipc::IPCResult WebGPUChild::RecvUncapturedError(const Maybe<RawId> aDeviceId,
                                                const nsACString& aMessage) {
  RefPtr<Device> device;
  if (aDeviceId) {
    const auto itr = mDeviceMap.find(*aDeviceId);
    if (itr != mDeviceMap.end()) {
      device = itr->second.get();
      MOZ_ASSERT(device);
    }
  }
  if (!device) {
    JsWarning(nullptr, aMessage);
  } else {
    // We don't want to spam the errors to the console indefinitely
    if (device->CheckNewWarning(aMessage)) {
      JsWarning(device->GetOwnerGlobal(), aMessage);

      dom::GPUUncapturedErrorEventInit init;
      init.mError = new ValidationError(device->GetParentObject(), aMessage);
      RefPtr<mozilla::dom::GPUUncapturedErrorEvent> event =
          dom::GPUUncapturedErrorEvent::Constructor(
              device, u"uncapturederror"_ns, init);
      device->DispatchEvent(*event);
    }
  }
  return IPC_OK();
}

bool WebGPUChild::ResolveLostForDeviceId(RawId aDeviceId,
                                         Maybe<uint8_t> aReason,
                                         const nsAString& aMessage) {
  RefPtr<Device> device;
  const auto itr = mDeviceMap.find(aDeviceId);
  if (itr != mDeviceMap.end()) {
    device = itr->second.get();
    MOZ_ASSERT(device);
  }
  if (!device) {
    // We must have unregistered the device already.
    return false;
  }

  if (aReason.isSome()) {
    dom::GPUDeviceLostReason reason =
        static_cast<dom::GPUDeviceLostReason>(*aReason);
    MOZ_ASSERT(reason == dom::GPUDeviceLostReason::Destroyed,
               "There is only one valid GPUDeviceLostReason value.");
    device->ResolveLost(Some(reason), aMessage);
  } else {
    device->ResolveLost(Nothing(), aMessage);
  }

  return true;
}

ipc::IPCResult WebGPUChild::RecvDeviceLost(RawId aDeviceId,
                                           Maybe<uint8_t> aReason,
                                           const nsACString& aMessage) {
  auto message = NS_ConvertUTF8toUTF16(aMessage);
  ResolveLostForDeviceId(aDeviceId, aReason, message);
  return IPC_OK();
}

void WebGPUChild::QueueOnSubmittedWorkDone(
    const RawId aSelfId, const RefPtr<dom::Promise>& aPromise) {
  SendQueueOnSubmittedWorkDone(aSelfId)->Then(
      GetCurrentSerialEventTarget(), __func__,
      [aPromise]() { aPromise->MaybeResolveWithUndefined(); },
      [aPromise](const ipc::ResponseRejectReason& aReason) {
        aPromise->MaybeRejectWithNotSupportedError("IPC error");
      });
}

void WebGPUChild::SwapChainPresent(RawId aTextureId,
                                   const RemoteTextureId& aRemoteTextureId,
                                   const RemoteTextureOwnerId& aOwnerId) {
  // Hack: the function expects `DeviceId`, but it only uses it for `backend()`
  // selection.
  // The parent side needs to create a command encoder which will be submitted
  // and dropped right away so we create and release an encoder ID here.
  RawId encoderId = ffi::wgpu_client_make_encoder_id(mClient.get());
  SendSwapChainPresent(aTextureId, encoderId, aRemoteTextureId, aOwnerId);
  ffi::wgpu_client_free_command_encoder_id(mClient.get(), encoderId);
}

void WebGPUChild::RegisterDevice(Device* const aDevice) {
  mDeviceMap.insert({aDevice->mId, aDevice});
}

void WebGPUChild::UnregisterDevice(RawId aDeviceId) {
  if (CanSend()) {
    SendDeviceDrop(aDeviceId);
  }
  mDeviceMap.erase(aDeviceId);
}

void WebGPUChild::FreeUnregisteredInParentDevice(RawId aId) {
  ffi::wgpu_client_kill_device_id(mClient.get(), aId);
  mDeviceMap.erase(aId);
}

void WebGPUChild::ActorDestroy(ActorDestroyReason) {
  // Resolving the promise could cause us to update the original map if the
  // callee frees the Device objects immediately. Since any remaining entries
  // in the map are no longer valid, we can just move the map onto the stack.
  const auto deviceMap = std::move(mDeviceMap);
  mDeviceMap.clear();

  for (const auto& targetIter : deviceMap) {
    RefPtr<Device> device = targetIter.second.get();
    MOZ_ASSERT(device);
    // It would be cleaner to call ResolveLostForDeviceId, but we
    // just cleared the device map, so we have to invoke ResolveLost
    // directly on the device.
    device->ResolveLost(Nothing(), u"WebGPUChild destroyed"_ns);
  }
}

void WebGPUChild::QueueSubmit(RawId aSelfId, RawId aDeviceId,
                              nsTArray<RawId>& aCommandBuffers) {
  SendQueueSubmit(aSelfId, aDeviceId, aCommandBuffers,
                  mSwapChainTexturesWaitingForSubmit);
  mSwapChainTexturesWaitingForSubmit.Clear();
}

void WebGPUChild::NotifyWaitForSubmit(RawId aTextureId) {
  mSwapChainTexturesWaitingForSubmit.AppendElement(aTextureId);
}

}  // namespace mozilla::webgpu
