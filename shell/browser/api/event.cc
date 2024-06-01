// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/event.h"

#include <utility>

#include "gin/data_object_builder.h"
#include "gin/object_template_builder.h"
#include "shell/browser/javascript_environment.h"
#include "shell/common/gin_converters/blink_converter.h"
#include "shell/common/gin_converters/std_converter.h"

namespace gin_helper {

namespace {

bool InvokeCallbackAdapter(Event::InvokeCallback callback,
                           v8::Local<v8::Value> result) {
  v8::Isolate* isolate = electron::JavascriptEnvironment::GetIsolate();
  blink::CloneableMessage message;
  if (!gin::ConvertFromV8(isolate, result, &message)) {
    return false;
  }

  std::move(callback).Run(std::move(message));
  return true;
}

}  // namespace

gin::WrapperInfo Event::kWrapperInfo = {gin::kEmbedderNativeGin};

Event::Event() {}

Event::~Event() {
  if (callback_) {
    v8::Isolate* isolate = electron::JavascriptEnvironment::GetIsolate();
    v8::HandleScope scope(isolate);
    auto message = gin::DataObjectBuilder(isolate)
                       .Set("error", "reply was never sent")
                       .Build();
    SendReply(isolate, message);
  }
}

void Event::SetCallback(ValueCallback callback) {
  DCHECK(!callback_);
  callback_ = std::move(callback);
}

void Event::PreventDefault(v8::Isolate* isolate) {
  v8::Local<v8::Object> self = GetWrapper(isolate).ToLocalChecked();
  self->Set(isolate->GetCurrentContext(),
            gin::StringToV8(isolate, "defaultPrevented"), v8::True(isolate))
      .Check();
}

bool Event::SendReply(v8::Isolate* isolate, v8::Local<v8::Value> result) {
  if (!callback_)
    return false;

  return std::move(callback_).Run(result);
}

gin::ObjectTemplateBuilder Event::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<Event>::GetObjectTemplateBuilder(isolate)
      .SetMethod("preventDefault", &Event::PreventDefault)
      .SetMethod("sendReply", &Event::SendReply);
}

const char* Event::GetTypeName() {
  return "WebRequest";
}

// static
gin::Handle<Event> Event::Create(v8::Isolate* isolate) {
  return gin::CreateHandle(isolate, new Event());
}

Event::ValueCallback Event::AdaptInvokeCallbackToValueCallback(
    Event::InvokeCallback callback) {
  if (!callback)
    return ValueCallback();

  return base::BindOnce(InvokeCallbackAdapter, std::move(callback));
}

}  // namespace gin_helper
