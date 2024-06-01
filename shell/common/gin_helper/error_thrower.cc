// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/gin_helper/error_thrower.h"

#include "gin/converter.h"
#include "shell/browser/javascript_environment.h"

namespace gin_helper {

ErrorThrower::ErrorThrower(v8::Isolate* isolate) : isolate_(isolate) {}

ErrorThrower::ErrorThrower()
    : isolate_(electron::JavascriptEnvironment::GetIsolate()) {}

ErrorThrower::~ErrorThrower() = default;

void ErrorThrower::ThrowError(base::StringPiece err_msg) {
  Throw(v8::Exception::Error, err_msg);
}

void ErrorThrower::ThrowTypeError(base::StringPiece err_msg) {
  Throw(v8::Exception::TypeError, err_msg);
}

void ErrorThrower::ThrowRangeError(base::StringPiece err_msg) {
  Throw(v8::Exception::RangeError, err_msg);
}

void ErrorThrower::ThrowReferenceError(base::StringPiece err_msg) {
  Throw(v8::Exception::ReferenceError, err_msg);
}

void ErrorThrower::ThrowSyntaxError(base::StringPiece err_msg) {
  Throw(v8::Exception::SyntaxError, err_msg);
}

void ErrorThrower::Throw(ErrorGenerator gen, base::StringPiece err_msg) {
  v8::Local<v8::Value> exception = gen(gin::StringToV8(isolate_, err_msg));
  if (!isolate_->IsExecutionTerminating())
    isolate_->ThrowException(exception);
}

}  // namespace gin_helper
