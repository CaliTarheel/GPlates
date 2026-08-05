/*
 * Compatibility wrapper for including Python after CGAL CORE.
 *
 * CGAL's CORE/CoreAux.h can define LONG_BIT as `(sizeof(long) * 8)`. That is
 * valid in C++ expressions, but Python's pyport.h also uses LONG_BIT in a
 * preprocessor #if where sizeof is not accepted. Let pyport.h provide its own
 * preprocessor-safe definition before loading the real Python header.
 *
 * This wrapper is placed on the include path only for compilers that support
 * #include_next. MSVC uses the real Python header directly and receives a
 * preprocessor-safe LONG_BIT definition from CMake.
 */
#ifndef GPLATES_SYSTEM_FIXES_PYTHON_H
#define GPLATES_SYSTEM_FIXES_PYTHON_H

#ifdef LONG_BIT
#undef LONG_BIT
#endif

#include_next <Python.h>

#endif // GPLATES_SYSTEM_FIXES_PYTHON_H
