#ifndef HORSE64_COMPILECONFIG_H_
#define HORSE64_COMPILECONFIG_H_


#define ATTR_UNUSED __attribute__((unused))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)


#endif  // HORSE64_COMPILECONFIG_H_
