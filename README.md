nx_array — release package, 2026-11-12

Variants:
- nx_array_qt.h — for Qt; tested with Qt 5.14.2, MinGW 7.3, and C++11.
- nx_array_vc.h — a universal header for Visual C++ and other
C++11-compliant compilers; does not require Qt.
- nx_array_cb.h — for C++Builder 10 with the Clang compiler (Win32/Win64).
- nx_array_cb99.h — for C++Builder 6 (special abridged version of the C++98/03 standard)

Choose one variant: these headers define the same class, `nx_array`, and share
a common include guard. Do not include different variants in the same translation unit.
