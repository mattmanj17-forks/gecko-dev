/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_gfx_layers_d3d11_DeviceAttachmentsD3D11_h
#define mozilla_gfx_layers_d3d11_DeviceAttachmentsD3D11_h

#include "mozilla/EnumeratedArray.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/SyncObject.h"
#include <d3d11.h>
#include <dxgi1_2.h>

namespace mozilla {
namespace layers {

struct ShaderBytes;

class DeviceAttachmentsD3D11 final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(DeviceAttachmentsD3D11);

 public:
  static RefPtr<DeviceAttachmentsD3D11> Create(ID3D11Device* aDevice);

  bool IsValid() const { return mInitialized; }
  const nsCString& GetFailureId() const {
    MOZ_ASSERT(!IsValid());
    return mInitFailureId;
  }

  typedef EnumeratedArray<ClipType, RefPtr<ID3D11VertexShader>,
                          size_t(ClipType::NumClipTypes)>
      VertexShaderArray;
  typedef EnumeratedArray<ClipType, RefPtr<ID3D11PixelShader>,
                          size_t(ClipType::NumClipTypes)>
      PixelShaderArray;

  RefPtr<ID3D11InputLayout> mInputLayout;

  RefPtr<ID3D11Buffer> mVertexBuffer;

  VertexShaderArray mVSQuadShader;

  RefPtr<ID3D11PixelShader> mSolidColorShader;
  PixelShaderArray mRGBAShader;
  PixelShaderArray mRGBShader;
  PixelShaderArray mYCbCrShader;
  PixelShaderArray mNV12Shader;
  RefPtr<ID3D11Buffer> mPSConstantBuffer;
  RefPtr<ID3D11Buffer> mVSConstantBuffer;
  RefPtr<ID3D11RasterizerState> mRasterizerState;
  RefPtr<ID3D11SamplerState> mLinearSamplerState;
  RefPtr<ID3D11SamplerState> mPointSamplerState;

  RefPtr<ID3D11BlendState> mPremulBlendState;
  RefPtr<ID3D11BlendState> mPremulCopyState;
  RefPtr<ID3D11BlendState> mNonPremulBlendState;
  RefPtr<ID3D11BlendState> mDisabledBlendState;

  RefPtr<SyncObjectHost> mSyncObject;

  void SetDeviceReset() { mDeviceReset = true; }
  bool IsDeviceReset() const { return mDeviceReset; }

 private:
  explicit DeviceAttachmentsD3D11(ID3D11Device* device);
  ~DeviceAttachmentsD3D11();

  bool Initialize();
  bool CreateShaders();
  bool InitSyncObject();

  void InitVertexShader(const ShaderBytes& aShader, VertexShaderArray& aArray,
                        ClipType aClipType) {
    InitVertexShader(aShader, getter_AddRefs(aArray[aClipType]));
  }
  void InitPixelShader(const ShaderBytes& aShader, PixelShaderArray& aArray,
                       ClipType aClipType) {
    InitPixelShader(aShader, getter_AddRefs(aArray[aClipType]));
  }

  void InitVertexShader(const ShaderBytes& aShader, ID3D11VertexShader** aOut);
  void InitPixelShader(const ShaderBytes& aShader, ID3D11PixelShader** aOut);

  bool Failed(HRESULT hr, const char* aContext);

 private:
  // Only used during initialization.
  RefPtr<ID3D11Device> mDevice;
  bool mContinueInit;
  bool mInitialized;
  bool mDeviceReset;
  nsCString mInitFailureId;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_gfx_layers_d3d11_DeviceAttachmentsD3D11_h
