/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */

// Generated by uniffi-bindgen-gecko-js.  DO NOT EDIT.

#include "nsString.h"
#include "nsPrintfCString.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ScaffoldingConverter.h"
#include "mozilla/dom/UniFFICall.h"
#include "mozilla/dom/UniFFICallbacks.h"
#include "mozilla/dom/UniFFIPointerType.h"
#include "mozilla/dom/UniFFIScaffolding.h"
#include "mozilla/dom/UniFFIRust.h"

namespace mozilla::uniffi {

using dom::ArrayBuffer;
using dom::AutoEntryScript;
using dom::GlobalObject;
using dom::RootedDictionary;
using dom::NullableRootedUnion;
using dom::Promise;
using dom::OwningUniFFIScaffoldingValue;
using dom::Sequence;
using dom::UniFFICallbackHandler;
using dom::UniFFIPointer;
using dom::UniFFIScaffoldingCallResult;

// Define scaffolding functions from UniFFI
extern "C" {
  {%- for (preprocessor_condition, ffi_definitions, preprocessor_condition_end) in all_ffi_definitions.iter() %}
{{ preprocessor_condition }}
  {%- for def in ffi_definitions %}
  {%- match def %}
  {%- when FfiDefinitionCpp::Function(func) %}
  {{ func.return_type }} {{ func.name }}({{ func.arg_types|join(", ") }});
  {%- when FfiDefinitionCpp::CallbackFunction(func) %}
  typedef {{ func.return_type }} (*{{ func.name }})({{ func.arg_types|join(", ") }});
  {%- when FfiDefinitionCpp::Struct(ffi_struct) %}
  struct {{ ffi_struct.name }} {
    {%- for field in ffi_struct.fields %}
    {{ field.type_ }} {{ field.name }};
    {%- endfor %}
  };
  {%- endmatch %}
  {%- endfor %}
{{ preprocessor_condition_end }}
  {%- endfor %}
}

// Define pointer types
{%- for (preprocessor_condition, pointer_types, preprocessor_condition_end) in all_pointer_types.iter() %}
{{ preprocessor_condition }}
{%- for pointer_type in pointer_types %}
const static mozilla::uniffi::UniFFIPointerType {{ pointer_type.name }} {
  "{{ pointer_type.label }}"_ns,
  {{ pointer_type.clone_fn }},
  {{ pointer_type.free_fn }},
};
{%- endfor %}
{{ preprocessor_condition_end }}
{%- endfor %}

// Callback interface method handlers, vtables, etc.
{%- for (preprocessor_condition, callback_interfaces, preprocessor_condition_end) in all_callback_interfaces.iter() %}
{{ preprocessor_condition }}

{%- for cbi in callback_interfaces %}
static StaticRefPtr<dom::UniFFICallbackHandler> {{ cbi.js_handler_var }};

{%- for handler in cbi.vtable.method_handlers %}
{%- let method_index = loop.index0 %}

class {{ handler.class_name }} : public UniffiCallbackMethodHandlerBase {
private:
  // Rust arguments, converted using ScaffoldingConverter::FromRust.
  {%- for a in handler.arguments %}
  typename {{ a.scaffolding_converter }}::IntermediateType {{ a.name }};
  {%- endfor %}

public:
  {{ handler.class_name }}(size_t aObjectHandle{%- for a in handler.arguments %}, {{ a.type_ }} {{ a.name }}{%- endfor %})
    : UniffiCallbackMethodHandlerBase("{{ cbi.name }}", aObjectHandle)
    {%- for a in handler.arguments %}, {{ a.name }}({{ a.scaffolding_converter }}::FromRust({{ a.name }})){% endfor %} {
  }

  MOZ_CAN_RUN_SCRIPT
  void MakeCall(JSContext* aCx, dom::UniFFICallbackHandler* aJsHandler, ErrorResult& aError) override {
    nsTArray<dom::OwningUniFFIScaffoldingValue> uniffiArgs;

    {%- if !handler.arguments.is_empty() %}
    // Setup
    if (!uniffiArgs.AppendElements({{ handler.arguments.len() }}, mozilla::fallible)) {
      aError.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }

    // Convert each argument
    {%- for a in handler.arguments %}
    {{ a.scaffolding_converter }}::IntoJs(
      aCx,
      std::move(this->{{ a.name }}),
      &uniffiArgs[{{ loop.index0 }}],
      aError);
    if (aError.Failed()) {
        return;
    }
    {%- endfor %}
    {%- endif %}

    // Stores the return value.  For now, we currently don't do anything with it, since we only support
    // fire-and-forget callbacks.
    NullableRootedUnion<dom::OwningUniFFIScaffoldingValue> returnValue(aCx);
    // Make the call
    aJsHandler->Call(mObjectHandle, {{ method_index }}, uniffiArgs, returnValue, aError);
  }
};

extern "C" void {{ handler.fn_name }}(
    uint64_t uniffiHandle,
    {% for a in handler.arguments %}{{ a.type_ }} {{ a.name }}, {% endfor %}
    void* uniffiOutReturn,
    RustCallStatus* uniffiCallStatus
) {
  UniquePtr<UniffiCallbackMethodHandlerBase> handler = MakeUnique<{{ handler.class_name }}>(uniffiHandle{% for a in handler.arguments %}, {{ a.name }}{%- endfor %});
  // Note: currently we only support queueing fire-and-forget async callbacks

  // For fire-and-forget callbacks, we don't know if the method succeeds or not
  // since it's called later. uniffiCallStatus is initialized to a successful
  // state by the Rust code, so there's no need to modify it.
  UniffiCallbackMethodHandlerBase::FireAndForget(std::move(handler), &{{ cbi.js_handler_var }});
}

{%- endfor %}

extern "C" void {{ cbi.free_fn }}(uint64_t uniffiHandle) {
  // Callback object handles are keys in a map stored in the JS handler. To
  // handle the free call, make a call into JS which will remove the key.
  // Fire-and-forget is perfect for this.
  UniffiCallbackMethodHandlerBase::FireAndForget(MakeUnique<UniffiCallbackFreeHandler>("{{ cbi.name }}", uniffiHandle), &{{ cbi.js_handler_var }});
}


static {{ cbi.vtable.type_ }} {{ cbi.vtable.var_name }} {
  {%- for handler in cbi.vtable.method_handlers %}
  {{ handler.fn_name }},
  {%- endfor %}
  {{ cbi.free_fn }}
};

{%- endfor %}
{{ preprocessor_condition_end }}
{%- endfor %}

void RegisterCallbackHandler(uint64_t aInterfaceId, UniFFICallbackHandler& aCallbackHandler, ErrorResult& aError) {
  switch (aInterfaceId) {
    {%- for (preprocessor_condition, callback_interfaces, preprocessor_condition_end) in all_callback_interfaces.iter() %}
    {{ preprocessor_condition }}

    {%- for cbi in callback_interfaces %}
    case {{ cbi.id }}: {
      if ({{ cbi.js_handler_var }}) {
        aError.ThrowUnknownError("[UniFFI] Callback handler already registered for {{ cbi.name }}"_ns);
        return;
      }

      {{ cbi.js_handler_var }} = &aCallbackHandler;
      {{ cbi.init_fn }}(&{{ cbi.vtable.var_name }});
      break;
    }


    {%- endfor %}
    {{ preprocessor_condition_end }}
    {%- endfor %}

    default:
      aError.ThrowUnknownError(nsPrintfCString("Unknown interface id: %" PRIu64, aInterfaceId));
      return;
  }
}

void DeregisterCallbackHandler(uint64_t aInterfaceId, ErrorResult& aError) {
  switch (aInterfaceId) {
    {%- for (preprocessor_condition, callback_interfaces, preprocessor_condition_end) in all_callback_interfaces.iter() %}
    {{ preprocessor_condition }}

    {%- for cbi in callback_interfaces %}
    case {{ cbi.id }}: {
      if (!{{ cbi.js_handler_var }}) {
        aError.ThrowUnknownError("[UniFFI] Callback handler not registered for {{ cbi.name }}"_ns);
        return;
      }

      {{ cbi.js_handler_var }} = nullptr;
      break;
    }


    {%- endfor %}
    {{ preprocessor_condition_end }}
    {%- endfor %}

    default:
      aError.ThrowUnknownError(nsPrintfCString("Unknown interface id: %" PRIu64, aInterfaceId));
      return;
  }
}


// Define scaffolding call classes for each combination of return/argument types
{%- for (preprocessor_condition, scaffolding_calls, preprocessor_condition_end) in all_scaffolding_calls.iter() %}
{{ preprocessor_condition }}
{%- for scaffolding_call in scaffolding_calls %}
{%- match scaffolding_call.async_info %}
{%- when None %}
class {{ scaffolding_call.handler_class_name }} : public UniffiSyncCallHandler {
private:
  // PrepareRustArgs stores the resulting arguments in these fields
  {%- for arg in scaffolding_call.arguments %}
  typename {{ arg.scaffolding_converter }}::IntermediateType {{ arg.var_name }};
  {%- endfor %}

  // MakeRustCall stores the result of the call in these fields
  {%- match scaffolding_call.return_type %}
  {%- when Some(return_type) %}
  typename {{ return_type.scaffolding_converter }}::IntermediateType mUniffiReturnValue;
  {%- else %}
  {%- endmatch %}

public:
  void PrepareRustArgs(const dom::Sequence<dom::OwningUniFFIScaffoldingValue>& aArgs, ErrorResult& aError) override {
    {%- for arg in scaffolding_call.arguments %}
    {{ arg.scaffolding_converter }}::FromJs(aArgs[{{ loop.index0 }}], &{{ arg.var_name }}, aError);
    if (aError.Failed()) {
      return;
    }
    {%- endfor %}
  }

  void MakeRustCall(RustCallStatus* aOutStatus) override {
    {%- match scaffolding_call.return_type %}
    {%- when Some(return_type) %}
    mUniffiReturnValue = {{ return_type.scaffolding_converter }}::FromRust(
      {{ scaffolding_call.ffi_func_name }}(
        {%- for arg in scaffolding_call.arguments %}
        {{ arg.scaffolding_converter }}::IntoRust(std::move({{ arg.var_name }})),
        {%- endfor %}
        aOutStatus
      )
    );
    {%- else %}
    {{ scaffolding_call.ffi_func_name }}(
      {%- for arg in scaffolding_call.arguments %}
      {{ arg.scaffolding_converter }}::IntoRust(std::move({{ arg.var_name }})),
      {%- endfor %}
      aOutStatus
    );
    {%- endmatch %}
  }

  virtual void ExtractSuccessfulCallResult(JSContext* aCx, dom::Optional<dom::OwningUniFFIScaffoldingValue>& aDest, ErrorResult& aError) override {
    {%- match scaffolding_call.return_type %}
    {%- when Some(return_type) %}
    {{ return_type.scaffolding_converter }}::IntoJs(
      aCx,
      std::move(mUniffiReturnValue),
      &aDest.Construct(),
      aError
    );
    {%- else %}
    {%- endmatch %}
  }
};
{%- when Some(async_info) %}
class {{ scaffolding_call.handler_class_name }} : public UniffiAsyncCallHandler {
public:
  {{ scaffolding_call.handler_class_name }}() : UniffiAsyncCallHandler({{ async_info.poll_fn }}, {{ async_info.free_fn }}) { }

private:
  // Complete stores the result of the call in mUniffiReturnValue
  {%- match scaffolding_call.return_type %}
  {%- when Some(return_type) %}
  typename {{ return_type.scaffolding_converter }}::IntermediateType mUniffiReturnValue;
  {%- else %}
  {%- endmatch %}

protected:
  // Convert a sequence of JS arguments and call the scaffolding function.
  // Always called on the main thread since async Rust calls don't block, they
  // return a future.
  void PrepareArgsAndMakeRustCall(const dom::Sequence<dom::OwningUniFFIScaffoldingValue>& aArgs, ErrorResult& aError) override {
    {%- for arg in scaffolding_call.arguments %}
    typename {{ arg.scaffolding_converter }}::IntermediateType {{ arg.var_name }};
    {{ arg.scaffolding_converter }}::FromJs(aArgs[{{ loop.index0 }}], &{{ arg.var_name }}, aError);
    if (aError.Failed()) {
      return;
    }
    {%- endfor %}

    mFutureHandle = {{ scaffolding_call.ffi_func_name }}(
      {%- for arg in scaffolding_call.arguments %}
      {{ arg.scaffolding_converter }}::IntoRust(std::move({{ arg.var_name }})){% if !loop.last %},{% endif %}
      {%- endfor %}
    );
  }

  void CallCompleteFn(RustCallStatus* aOutStatus) override {
    {%- match scaffolding_call.return_type %}
    {%- when Some(return_type) %}
    mUniffiReturnValue = {{ return_type.scaffolding_converter }}::FromRust(
      {{ async_info.complete_fn }}(mFutureHandle, aOutStatus));
    {%- else %}
    {{ async_info.complete_fn }}(mFutureHandle, aOutStatus);
    {%- endmatch %}
  }

public:
  void ExtractSuccessfulCallResult(JSContext* aCx, dom::Optional<dom::OwningUniFFIScaffoldingValue>& aDest, ErrorResult& aError) override {
    {%- match scaffolding_call.return_type %}
    {%- when Some(return_type) %}
    {{ return_type.scaffolding_converter }}::IntoJs(
      aCx,
      std::move(mUniffiReturnValue),
      &aDest.Construct(),
      aError
    );
    {%- else %}
    {%- endmatch %}
  }
};
{%- endmatch %}

{%- endfor %}
{{ preprocessor_condition_end }}
{%- endfor %}

UniquePtr<UniffiSyncCallHandler> GetSyncCallHandler(uint64_t aId) {
  switch (aId) {
    {%- for (preprocessor_condition, scaffolding_calls, preprocessor_condition_end) in all_scaffolding_calls.iter() %}
{{ preprocessor_condition }}
    {%- for call in scaffolding_calls %}
    {%- if !call.is_async() %}
    case {{ call.function_id }}: {
      return MakeUnique<{{ call.handler_class_name }}>();
    }
    {%- endif %}
    {%- endfor %}
{{ preprocessor_condition_end }}
    {%- endfor %}

    default:
      return nullptr;
  }
}

UniquePtr<UniffiAsyncCallHandler> GetAsyncCallHandler(uint64_t aId) {
  switch (aId) {
    {%- for (preprocessor_condition, scaffolding_calls, preprocessor_condition_end) in all_scaffolding_calls.iter() %}
{{ preprocessor_condition }}
    {%- for call in scaffolding_calls %}
    {%- if call.is_async() %}
    case {{ call.function_id }}: {
      return MakeUnique<{{ call.handler_class_name }}>();
    }
    {%- endif %}
    {%- endfor %}
{{ preprocessor_condition_end }}
    {%- endfor %}

    default:
      return nullptr;
  }
}


Maybe<already_AddRefed<UniFFIPointer>> ReadPointer(const GlobalObject& aGlobal, uint64_t aId, const ArrayBuffer& aArrayBuff, long aPosition, ErrorResult& aError) {
  const UniFFIPointerType* type;
  switch (aId) {
    {%- for (preprocessor_condition, pointer_types, preprocessor_condition_end) in all_pointer_types.iter() %}
{{ preprocessor_condition }}
    {%- for pointer_type in pointer_types %}
    case {{ pointer_type.object_id }}: {
      type = &{{ pointer_type.name }};
      break;
    }
    {%- endfor %}
{{ preprocessor_condition_end }}
    {%- endfor %}
    default:
      return Nothing();
  }
  return Some(UniFFIPointer::Read(aArrayBuff, aPosition, type, aError));
}

bool WritePointer(const GlobalObject& aGlobal, uint64_t aId, const UniFFIPointer& aPtr, const ArrayBuffer& aArrayBuff, long aPosition, ErrorResult& aError) {
  const UniFFIPointerType* type;
  switch (aId) {
    {%- for (preprocessor_condition, pointer_types, preprocessor_condition_end) in all_pointer_types.iter() %}
{{ preprocessor_condition }}
    {%- for pointer_type in pointer_types %}
    case {{ pointer_type.object_id }}: {
      type = &{{ pointer_type.name }};
      break;
    }
    {%- endfor %}
{{ preprocessor_condition_end }}
    {%- endfor %}
    default:
      return false;
  }
  aPtr.Write(aArrayBuff, aPosition, type, aError);
  return true;
}

}  // namespace mozilla::uniffi
