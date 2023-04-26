#include "include/modules/pythonmonkey/pythonmonkey.hh"

#include "include/IntType.hh"

#include "include/PyType.hh"
#include "include/TypeEnum.hh"

#include <jsapi.h>
#include <js/BigInt.h>

#include <Python.h>

#include <iostream>
#include <bit>

#define SIGN_BIT_MASK 0b1000 // https://hg.mozilla.org/releases/mozilla-esr102/file/tip/js/src/vm/BigIntType.h#l40
#define CELL_HEADER_LENGTH 8 // https://hg.mozilla.org/releases/mozilla-esr102/file/tip/js/src/gc/Cell.h#l602

#define JS_DIGIT_BIT JS_BITS_PER_WORD
#define PY_DIGIT_BIT PYLONG_BITS_IN_DIGIT

#define js_digit_t uintptr_t // https://hg.mozilla.org/releases/mozilla-esr102/file/tip/js/src/vm/BigIntType.h#l36
#define JS_DIGIT_BYTE (sizeof(js_digit_t)/sizeof(uint8_t))

#define JS_INLINE_DIGIT_MAX_LEN 1 // https://hg.mozilla.org/releases/mozilla-esr102/file/tip/js/src/vm/BigIntType.h#l43

static const char HEX_CHAR_LOOKUP_TABLE[] = "0123456789ABCDEF";

IntType::IntType(PyObject *object) : PyType(object) {}

IntType::IntType(long n) : PyType(Py_BuildValue("i", n)) {}

IntType::IntType(JSContext *cx, JS::BigInt *bigint) {
  // Get the sign bit
  bool isNegative = BigIntIsNegative(bigint);

  // Read the digits count in this JS BigInt
  //    see https://hg.mozilla.org/releases/mozilla-esr102/file/tip/js/src/vm/BigIntType.h#l48
  //        https://hg.mozilla.org/releases/mozilla-esr102/file/tip/js/src/gc/Cell.h#l623
  uint32_t jsDigitCount = ((uint32_t *)bigint)[1];

  // Get all the 64-bit (assuming we compile on 64-bit OS) "digits" from JS BigInt
  js_digit_t *jsDigits = (js_digit_t *)(((char *)bigint) + CELL_HEADER_LENGTH);
  if (jsDigitCount > JS_INLINE_DIGIT_MAX_LEN) { // hasHeapDigits
    // We actually have a pointer to the digit storage if the number cannot fit in one uint64_t
    //    see https://hg.mozilla.org/releases/mozilla-esr102/file/tip/js/src/vm/BigIntType.h#l54
    jsDigits = *((js_digit_t **)jsDigits);
  }
  //
  // The digit storage starts with the least significant digit (little-endian digit order).
  // Byte order within a digit is native-endian.

  if constexpr (std::endian::native == std::endian::big) { // C++20
    // @TODO (Tom Tang): use C++23 std::byteswap?
    printf("big-endian cpu is not supported by PythonMonkey yet");
    return;
  }
  // If the native endianness is also little-endian,
  // we now have consecutive bytes of 8-bit "digits" in little-endian order
  const uint8_t *bytes = const_cast<const uint8_t *>((uint8_t *)jsDigits);
  if (jsDigitCount == 0) {
    // Create a new object instead of reusing the object for int 0
    //    see https://github.com/python/cpython/blob/3.9/Objects/longobject.c#L862
    //        https://github.com/python/cpython/blob/3.9/Objects/longobject.c#L310
    pyObject = (PyObject *)_PyLong_New(0);
  } else {
    pyObject = _PyLong_FromByteArray(bytes, jsDigitCount * JS_DIGIT_BYTE, true, false);
  }

  // Set the sign bit
  //    see https://github.com/python/cpython/blob/3.9/Objects/longobject.c#L956
  if (isNegative) {
    ssize_t pyDigitCount = Py_SIZE(pyObject);
    Py_SET_SIZE(pyObject, -pyDigitCount);
  }

  // Cast to a pythonmonkey.bigint to differentiate it from a normal Python int,
  //  allowing Py<->JS two-way BigInt conversion
  Py_SET_TYPE(pyObject, (PyTypeObject *)(PythonMonkey_BigInt));
}

JS::BigInt *IntType::toJsBigInt(JSContext *cx) {
  // Figure out how many 64-bit "digits" we would have for JS BigInt
  //    see https://github.com/python/cpython/blob/3.9/Modules/_randommodule.c#L306
  size_t bitCount = _PyLong_NumBits(pyObject);
  if (bitCount == (size_t)-1 && PyErr_Occurred())
    return nullptr;
  uint32_t jsDigitCount = bitCount == 0 ? 1 : (bitCount - 1) / JS_DIGIT_BIT + 1;
  // Get the sign bit
  //    see https://github.com/python/cpython/blob/3.9/Objects/longobject.c#L977
  ssize_t pyDigitCount = Py_SIZE(pyObject); // negative on negative numbers
  bool isNegative = pyDigitCount < 0;
  // Force to make the number positive otherwise _PyLong_AsByteArray would complain
  //    see https://github.com/python/cpython/blob/3.9/Objects/longobject.c#L980
  if (isNegative) {
    Py_SET_SIZE(pyObject, /*abs()*/ -pyDigitCount);
  }

  JS::BigInt *bigint = nullptr;
  if (jsDigitCount <= 1) {
    // Fast path for int fits in one js_digit_t (uint64 on 64-bit OS)
    bigint = JS::detail::BigIntFromUint64(cx, PyLong_AsUnsignedLongLong(pyObject));
  } else {
    // Convert to bytes of 8-bit "digits" in **big-endian** order
    size_t byteCount = (size_t)JS_DIGIT_BYTE * jsDigitCount;
    uint8_t *bytes = (uint8_t *)PyMem_Malloc(byteCount);
    _PyLong_AsByteArray((PyLongObject *)pyObject, bytes, byteCount, /*is_little_endian*/ false, false);

    // Convert pm.bigint to JS::BigInt through hex strings (no public API to convert directly through bytes)
    // TODO (Tom Tang): We could manually allocate the memory, https://hg.mozilla.org/releases/mozilla-esr102/file/tip/js/src/vm/BigIntType.cpp#l162, but still no public API
    // TODO (Tom Tang): Could we fill in an object with similar memory alignment (maybe by NewArrayBufferWithContents), and coerce it to BigInt?

    // Calculate the number of chars required to represent the bigint in hex string
    size_t charCount = byteCount * 2;
    // Convert bytes to hex string (big-endian)
    std::vector<char> chars = std::vector<char>(charCount); // can't be null-terminated, otherwise SimpleStringToBigInt would read the extra \0 character and then segfault
    for (size_t i = 0, j = 0; i < charCount; i += 2, j++) {
      chars[i] = HEX_CHAR_LOOKUP_TABLE[(bytes[j] >> 4)&0xf]; // high nibble
      chars[i+1] = HEX_CHAR_LOOKUP_TABLE[bytes[j]&0xf];      // low  nibble
    }
    PyMem_Free(bytes);

    // Convert hex string to JS::BigInt
    mozilla::Span<const char> strSpan = mozilla::Span<const char>(chars); // storing only a pointer to the underlying array and length
    bigint = JS::SimpleStringToBigInt(cx, strSpan, 16);
  }

  if (isNegative) {
    // Make negative number back negative
    // TODO (Tom Tang): use _PyLong_Copy to create a new object. Not thread-safe here
    Py_SET_SIZE(pyObject, pyDigitCount);

    // Set the sign bit
    // https://hg.mozilla.org/releases/mozilla-esr102/file/tip/js/src/vm/BigIntType.cpp#l1801
    /* flagsField */ ((uint32_t *)bigint)[0] |= SIGN_BIT_MASK;
  }

  return bigint;
}