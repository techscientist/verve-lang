#ifndef CEOS_VALUE_H
#define CEOS_VALUE_H

namespace ceos {
  class VM;
  struct Function;
  struct Lambda;

  struct Value {
    union {
      uintptr_t ptr;
      struct {
        int32_t i;
        uint16_t __;
        uint8_t _;
        uint8_t tag;
      } data;
    } value;

#define TAG(NAME, OFFSET) \
    static const uint8_t NAME##Tag = 1 << OFFSET; \
    bool is##NAME() { return value.data.tag == Value::NAME##Tag; }

    TAG(Int, 0);
    TAG(String, 1);
    TAG(Array, 2);
    TAG(Builtin, 3);
    TAG(Function, 4);
    TAG(Lambda, 5);

#undef TAG

    static uintptr_t unmask(uintptr_t ptr) {
      return 0xFFFFFFFFFFFFFF & ptr;
    }

    Value() {}

    Value(int v) {
      value.data.i = v;
      value.data.tag = Value::IntTag;
    }

    int asInt() { return value.data.i; }

#define POINTER_TYPE(TYPE, NAME) \
    Value(TYPE *ptr) { \
      value.ptr = reinterpret_cast<uintptr_t>(ptr); \
      value.data.tag = Value::NAME##Tag; \
    }\
    \
    TYPE *as##NAME() { \
      return reinterpret_cast<TYPE *>(unmask(value.ptr)); \
    }

    POINTER_TYPE(std::string, String)
    POINTER_TYPE(std::vector<Value>, Array)
    POINTER_TYPE(Function, Function)
    POINTER_TYPE(Lambda, Lambda)

#undef POINTER_TYPE

    typedef Value (*JSFunctionType)(VM &, unsigned);

    Value(JSFunctionType ptr) {
      value.ptr = reinterpret_cast<uintptr_t>(ptr);
      value.data.tag = Value::BuiltinTag;
    }

    JSFunctionType asBuiltin() {
      return reinterpret_cast<JSFunctionType>(unmask(value.ptr));
    }
  };

}

using JSFunctionType = ceos::Value::JSFunctionType;

#endif