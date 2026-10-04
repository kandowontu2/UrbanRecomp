#ifndef SC_NATIVE_SPECIALIZE_H
#define SC_NATIVE_SPECIALIZE_H

/* Connected simulation bodies have fixed modes at their public entry points:
 * a bounded span, one atomic instruction, or stationary-clock development.
 * Make the body visible there so the compiler can remove unused mode branches
 * and optimize register/flag publications across connected edges. All live
 * stack, memory and deadline checks remain in the specialized body.
 * SC_NATIVE_KERNEL_GENERIC retains the shared body for build comparisons. */
#if defined(SC_NATIVE_KERNEL_GENERIC)
#define SC_NATIVE_SPECIALIZE static
#elif defined(_MSC_VER) && !defined(_DEBUG)
#define SC_NATIVE_SPECIALIZE static __forceinline
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__OPTIMIZE__)
#define SC_NATIVE_SPECIALIZE static inline __attribute__((always_inline))
#else
#define SC_NATIVE_SPECIALIZE static inline
#endif

#endif
