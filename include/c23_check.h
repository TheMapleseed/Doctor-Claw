#ifndef DOCTORCLAW_C23_CHECK_H
#define DOCTORCLAW_C23_CHECK_H

/*
 * This project is built as ISO C23. Ensure the compiler is in C23 mode.
 * C23: __STDC_VERSION__ == 202311L (ISO/IEC 9899:2024)
 */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#error "Doctor Claw requires ISO C23. Build with -std=c23 (__STDC_VERSION__ >= 202311L)."
#endif

#endif /* DOCTORCLAW_C23_CHECK_H */
