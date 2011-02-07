/*
 * Copyright (C) 2010-2011 Max Kellermann <max@duempel.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef XCSOAR_JAVA_OBJECT_HPP
#define XCSOAR_JAVA_OBJECT_HPP

#include "Java/Global.hpp"
#include "Java/Ref.hpp"

#include <jni.h>

namespace Java {
  class Object : public GlobalRef<jobject> {
  public:
    /**
     * Constructs an uninitialized object.  The method set() must be
     * called before it is destructed.
     */
    Object() {}

    Object(JNIEnv *env, jobject obj):GlobalRef<jobject>(env, obj) {}

    void call_void(JNIEnv *env, const char *name) {
      jclass cls = env->GetObjectClass(get());
      jmethodID mid = env->GetMethodID(cls, name, "()V");
      env->CallVoidMethod(get(), mid);
      env->DeleteLocalRef(cls);
    }

    void call_void(const char *name) {
      call_void(GetEnv(), name);
    }
  };
}

#endif
