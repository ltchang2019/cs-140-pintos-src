#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <debug.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

/* Fixed-point real arithmetic required for the MLFQS scheduling algorithm.
   
   Fixed-point arithmetic is implemented using signed 32-bit integers and
   17.14 fixed-point number representation, where there are 17 bits before
   the decimal, 14 bits after it, and one sign bit.
   
   Since the lowest 14 bits are designated fractional bits, an integer x
   as a fixed-point number represents the real number x/(2**14), where **
   represents exponentiation.
   
   In all function signatures below, x and y are assumed to be real numbers 
   stored in fixed-point format and n is assumed to be an integer number. */
typedef int32_t fixed32_t;

/* Constants. */
#define FIXED32_CONST (1 << 14)      /* 17.14 format conversion constant. */
#define FIXED32_MAX INT_MAX          /* Max real value in 17.14 format. */
#define FIXED32_MIN INT_MIN          /* Min real value in 17.14 format. */
#define ONE_HALF (FIXED32_CONST / 2) /* 0.5 in fixed-point format. */
#define HUNDRED 100                  /* For printing fixed-point numbers. */

/* Addition and subtraction operations. */
static inline void detect_add_overflow (int a, int b, int sum) {
  if (a > 0 && b > 0)
  {
    ASSERT (sum > a && sum > b);
  }
  if (a < 0 && b < 0)
  {
    ASSERT (sum < a && sum < b);
  }
}
static inline void detect_sub_overflow (int a, int b, int difference) {
  if (a > 0 && b < 0)
  {
    ASSERT (difference > a && difference > b);
  }
  if (a < 0 && b > 0)
  {
    ASSERT (difference < a && difference < b);
  }
}
static inline fixed32_t add_fixed_fixed (fixed32_t x, fixed32_t y) {
  detect_add_overflow (x, y, x + y);
  return x + y;
} 
static inline fixed32_t sub_fixed_fixed (fixed32_t x, fixed32_t y) {
  detect_sub_overflow (x, y, x - y);
  return x - y;
}
static inline fixed32_t add_fixed_int (fixed32_t x, int n) {
  detect_add_overflow (x, n * FIXED32_CONST, x + n * FIXED32_CONST);                     
  return x + n * FIXED32_CONST;
}
static inline fixed32_t sub_fixed_int (fixed32_t x, int n) {
  detect_sub_overflow (x, n * FIXED32_CONST, x - n * FIXED32_CONST);
  return x - n * FIXED32_CONST;
}

/* Multiplication and division operations. */
static inline void detect_mul_div_overflow (int a, int b, int result) {
  if ((a > 0 && b > 0) || (a < 0 && b < 0))
  {
    ASSERT (result >= 0);
  }
  if ((a > 0 && b < 0) || (a < 0 && b > 0)) 
  {
    ASSERT (result <= 0);
  }
}
static inline fixed32_t mul_fixed_fixed (fixed32_t x, fixed32_t y) {
  detect_mul_div_overflow (x, y, ((int64_t) x) * y / FIXED32_CONST);
  return ((int64_t) x) * y / FIXED32_CONST;
}
static inline fixed32_t mul_fixed_int (fixed32_t x, int n) {
  detect_mul_div_overflow (x, n, x * n);
  return x * n;
}
static inline fixed32_t div_fixed_fixed (fixed32_t x, fixed32_t y) {
  ASSERT (y != 0);
  detect_mul_div_overflow (x, y, ((int64_t) x) * FIXED32_CONST / y);
  return ((int64_t) x) * FIXED32_CONST / y;
}
static inline fixed32_t div_fixed_int (fixed32_t x, int n) {
  ASSERT (n != 0);
  return x / n;
}

/* Conversion operations between integers/fractions and real values
   in 17.14 fixed-point format. */
static inline fixed32_t int_to_fixed (int n) { 
  return mul_fixed_int (FIXED32_CONST, n);
}
static inline fixed32_t frac_to_fixed (int num, int denom) {
  return div_fixed_int (int_to_fixed (num), denom);
}
/* Convert x to integer (rounding toward zero). */
static inline int fixed_to_int_rzero (fixed32_t x) {
  return x / FIXED32_CONST;
}
/* Convert x to integer (rounding to nearest). Real values close to
   FIXED32_MAX or FIXED32_MIN require the decimal to be truncated
   first to avoid overflow. */
static inline int fixed_to_int_rnearest (fixed32_t x) {
  if (x > sub_fixed_fixed (FIXED32_MAX, ONE_HALF))
    return fixed_to_int_rzero (x) + 1;
  else if (x < add_fixed_fixed(FIXED32_MIN, ONE_HALF))
    return fixed_to_int_rzero (x) - 1;
  else if (x > 0)
    return fixed_to_int_rzero (add_fixed_fixed (x, ONE_HALF));
  else
    return fixed_to_int_rzero (sub_fixed_fixed (x, ONE_HALF));
}

/* Returns a fixed-point number as an integer value that can be used
   to display the real value of the fixed-point number in decimal
   format with two decimal places.

   To avoid overflow, the fractional bits of real values larger than
   100 * (FIXED32_MAX / FIXED32_CONST) (approx. 1310.72) or smaller
   than 100 * (FIXED32_MIN / FIXED32_CONST) (approx. -1310.72) are
   truncated to 0. */
static inline int fixed_to_two_decimal_format (fixed32_t x) {
  if (x > div_fixed_int (FIXED32_MAX, HUNDRED) ||
      x < div_fixed_int (FIXED32_MIN, HUNDRED))
    return fixed_to_int_rnearest (x) * HUNDRED;
  else
    return fixed_to_int_rnearest (mul_fixed_int (x, HUNDRED));
}

/* Prints a fixed-point number as a real value with two decimal places
   (for debugging). */
static inline void print_fixed_two_decimal_format (fixed32_t x) {
  int x_print = fixed_to_two_decimal_format  (x);
  printf ("%d.%02d\n", x_print / HUNDRED, x_print % HUNDRED);
}

#endif /* threads/fixed-point.h */