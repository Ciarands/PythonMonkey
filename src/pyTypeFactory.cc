/**
 * @file pyTypeFactory.cc
 * @author Caleb Aikens (caleb@distributive.network) and Philippe Laporte (philippe@distributive.network)
 * @brief Function for wrapping arbitrary PyObjects into the appropriate PyType class, and coercing JS types to python types
 * @date 2023-03-29
 *
 * @copyright 2023-2024 Distributive Corp.
 *
 */

#include "include/pyTypeFactory.hh"

#include "include/BoolType.hh"
#include "include/BufferType.hh"
#include "include/DateType.hh"
#include "include/DictType.hh"
#include "include/ExceptionType.hh"
#include "include/FloatType.hh"
#include "include/FuncType.hh"
#include "include/IntType.hh"
#include "include/jsTypeFactory.hh"
#include "include/ListType.hh"
#include "include/NoneType.hh"
#include "include/NullType.hh"
#include "include/PromiseType.hh"
#include "include/PyDictProxyHandler.hh"
#include "include/PyListProxyHandler.hh"
#include "include/PyObjectProxyHandler.hh"
#include "include/PyType.hh"
#include "include/setSpiderMonkeyException.hh"
#include "include/StrType.hh"
#include "include/TupleType.hh"
#include "include/modules/pythonmonkey/pythonmonkey.hh"

#include <jsapi.h>
#include <jsfriendapi.h>
#include <js/Object.h>
#include <js/ValueArray.h>

#include <Python.h>

PyType *pyTypeFactory(PyObject *object) {
  PyType *pyType;

  if (PyLong_Check(object)) {
    pyType = new IntType(object);
  }
  else if (PyUnicode_Check(object)) {
    pyType = new StrType(object);
  }
  else if (PyFunction_Check(object)) {
    pyType = new FuncType(object);
  }
  else if (PyDict_Check(object)) {
    pyType = new DictType(object);
  }
  else if (PyList_Check(object)) {
    pyType = new ListType(object);
  }
  else if (PyTuple_Check(object)) {
    pyType = new TupleType(object);
  }
  else {
    return nullptr;
  }

  return pyType;
}

PyType *pyTypeFactory(JSContext *cx, JS::Rooted<JSObject *> *thisObj, JS::HandleValue rval) {
  if (rval.isUndefined()) {
    return new NoneType();
  }
  else if (rval.isNull()) {
    return new NullType();
  }
  else if (rval.isBoolean()) {
    return new BoolType(rval.toBoolean());
  }
  else if (rval.isNumber()) {
    return new FloatType(rval.toNumber());
  }
  else if (rval.isString()) {
    StrType *s = new StrType(cx, rval.toString());
    return s;
  }
  else if (rval.isSymbol()) {
    printf("symbol type is not handled by PythonMonkey yet");
  }
  else if (rval.isBigInt()) {
    return new IntType(cx, rval.toBigInt());
  }
  else if (rval.isObject()) {
    JS::Rooted<JSObject *> obj(cx);
    JS_ValueToObject(cx, rval, &obj);
    if (JS::GetClass(obj)->isProxyObject()) {
      if (js::GetProxyHandler(obj)->family() == &PyDictProxyHandler::family) { // this is one of our proxies for python dicts
        return new DictType(((PyDictProxyHandler *)js::GetProxyHandler(obj))->pyObject);
      }
      if (js::GetProxyHandler(obj)->family() == &PyListProxyHandler::family) { // this is one of our proxies for python lists
        return new ListType(((PyListProxyHandler *)js::GetProxyHandler(obj))->pyObject);
      }
      if (js::GetProxyHandler(obj)->family() == &PyObjectProxyHandler::family) { // this is one of our proxies for python objects
        return new PyType(((PyObjectProxyHandler *)js::GetProxyHandler(obj))->pyObject);
      }
    }
    js::ESClass cls;
    JS::GetBuiltinClass(cx, obj, &cls);
    if (JS_ObjectIsBoundFunction(obj)) {
      cls = js::ESClass::Function; // In SpiderMonkey 115 ESR, bound function is no longer a JSFunction but a js::BoundFunctionObject.
                                   // js::ESClass::Function only assigns to JSFunction objects by JS::GetBuiltinClass.
    }
    switch (cls) {
    case js::ESClass::Boolean: {
        // TODO (Caleb Aikens): refactor out all `js::Unbox` calls
        // TODO (Caleb Aikens): refactor using recursive call to `pyTypeFactory`
        JS::RootedValue unboxed(cx);
        js::Unbox(cx, obj, &unboxed);
        return new BoolType(unboxed.toBoolean());
      }
    case js::ESClass::Date: {
        return new DateType(cx, obj);
      }
    case js::ESClass::Promise: {
        return new PromiseType(cx, obj);
      }
    case js::ESClass::Error: {
        return new ExceptionType(cx, obj);
      }
    case js::ESClass::Function: {
        PyObject *pyFunc;
        FuncType *f;
        if (JS_IsNativeFunction(obj, callPyFunc)) { // It's a wrapped python function by us
          // Get the underlying python function from the 0th reserved slot
          JS::Value pyFuncVal = js::GetFunctionNativeReserved(obj, 0);
          pyFunc = (PyObject *)(pyFuncVal.toPrivate());
          f = new FuncType(pyFunc);
        } else {
          f = new FuncType(cx, rval);
        }
        return f;
      }
    case js::ESClass::Number: {
        JS::RootedValue unboxed(cx);
        js::Unbox(cx, obj, &unboxed);
        return new FloatType(unboxed.toNumber());
      }
    case js::ESClass::BigInt: {
        JS::RootedValue unboxed(cx);
        js::Unbox(cx, obj, &unboxed);
        return new IntType(cx, unboxed.toBigInt());
      }
    case js::ESClass::String: {
        JS::RootedValue unboxed(cx);
        js::Unbox(cx, obj, &unboxed);
        StrType *s = new StrType(cx, unboxed.toString());
        return s;
      }
    case js::ESClass::Array: {
        return new ListType(cx, obj);
      }
    default: {
        if (BufferType::isSupportedJsTypes(obj)) { // TypedArray or ArrayBuffer
          // TODO (Tom Tang): ArrayBuffers have cls == js::ESClass::ArrayBuffer
          return new BufferType(cx, obj);
        }
      }
    }
    return new DictType(cx, rval);
  }
  else if (rval.isMagic()) {
    printf("magic type is not handled by PythonMonkey yet\n");
  }

  std::string errorString("pythonmonkey cannot yet convert Javascript value of: ");
  JS::RootedString str(cx, JS::ToString(cx, rval));
  errorString += JS_EncodeStringToUTF8(cx, str).get();
  PyErr_SetString(PyExc_TypeError, errorString.c_str());
  return NULL;
}

PyType *pyTypeFactorySafe(JSContext *cx, JS::Rooted<JSObject *> *thisObj, JS::HandleValue rval) {
  PyType *v = pyTypeFactory(cx, thisObj, rval);
  if (PyErr_Occurred()) {
    // Clear Python error
    PyErr_Clear();
    // Return `pythonmonkey.null` on error
    return new NullType();
  }
  return v;
}