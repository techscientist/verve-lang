#define CONCAT(__a, __b) CONCAT_(__a, __b)
#define CONCAT_(__a, __b) __a##__b

#define EVAL(...) EVAL1024(__VA_ARGS__)
#define EVAL1024(...) EVAL512(EVAL512(__VA_ARGS__))
#define EVAL512(...) EVAL256(EVAL256(__VA_ARGS__))
#define EVAL256(...) EVAL128(EVAL128(__VA_ARGS__))
#define EVAL128(...) EVAL64(EVAL64(__VA_ARGS__))
#define EVAL64(...) EVAL32(EVAL32(__VA_ARGS__))
#define EVAL32(...) EVAL16(EVAL16(__VA_ARGS__))
#define EVAL16(...) EVAL8(EVAL8(__VA_ARGS__))
#define EVAL8(...) EVAL4(EVAL4(__VA_ARGS__))
#define EVAL4(...) EVAL2(EVAL2(__VA_ARGS__))
#define EVAL2(...) EVAL1(EVAL1(__VA_ARGS__))
#define EVAL1(...) __VA_ARGS__

#define DEFER(__macro) __macro EMPTY()

#define MAP_1() EMPTY
#define MAP_0() MAP
#define MAP(...) MAP_(__VA_ARGS__)
#define MAP_(__macro, __first, ...) __macro(__first) DEFER(CONCAT(MAP_, IS_EMPTY(__VA_ARGS__)))()(__macro, __VA_ARGS__)

#define MAP_2_1() EMPTY
#define MAP_2_0() MAP_2
#define MAP_2(...) MAP_2_(__VA_ARGS__)
#define MAP_2_(__macro, __first, __second, ...) __macro(__first, __second) DEFER(CONCAT(MAP_2_, IS_EMPTY(__VA_ARGS__)))()(__macro, __VA_ARGS__)

#define IS_EMPTY(...) IS_EMPTY_(FIRST(__VA_ARGS__))
#define IS_EMPTY_(...) IS_EMPTY__(__VA_ARGS__)
#define IS_EMPTY__(...) IS_EMPTY___(_NOT_ ## __VA_ARGS__)
#define IS_EMPTY___(...) SECOND(__VA_ARGS__, 0)
#define _NOT_  ~, 1

#define BOOL_(__first, ...)

#define FIRST(__first, ...) __first
#define SECOND(_, __second, ...) __second

#define STR_WITH_COMMA(__a) #__a,

#define EMPTY(...)

#define ENUM_COMMON(__prefix, __name, __options...) \
  enum __prefix __name { \
    __options  \
  }; \
 \
  __unused static const char *typeName(__name t) { \
    return (const char *[]) { \
      EVAL(MAP(STR_WITH_COMMA, __options)) \
    }[(int)t]; \
  }

#define ENUM(...) ENUM_COMMON(, __VA_ARGS__)
#define ENUM_CLASS(...) ENUM_COMMON(class, __VA_ARGS__)

#define INDEX_OF(ARRAY, ITEM) (std::find(ARRAY.begin(), ARRAY.end(), ITEM) - ARRAY.begin())

#define ALWAYS_INLINE __attribute__((always_inline)) inline
