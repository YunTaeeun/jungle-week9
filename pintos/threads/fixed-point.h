#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

// 17.14 고정소수점 형식
// p=17, q=14, f=2^14=16384
#define F (1 << 14)              // f = 2^14 = 16384

// 정수를 고정소수점으로 변환
#define INT_TO_FP(n) ((n) * F)

// 고정소수점을 정수로 변환 (0으로 반올림)
#define FP_TO_INT_ZERO(x) ((x) / F)

// 고정소수점을 정수로 변환 (가장 가까운 정수로 반올림)
#define FP_TO_INT_NEAR(x) ((x) >= 0 ? ((x) + F / 2) / F : ((x) - F / 2) / F)

// 고정소수점 덧셈
#define FP_ADD(x, y) ((x) + (y))

// 고정소수점 뺄셈
#define FP_SUB(x, y) ((x) - (y))

// 고정소수점 + 정수
#define FP_ADD_INT(x, n) ((x) + (n) * F)

// 고정소수점 - 정수
#define FP_SUB_INT(x, n) ((x) - (n) * F)

// 고정소수점 곱셈
#define FP_MULT(x, y) (((int64_t)(x)) * (y) / F)

// 고정소수점 * 정수
#define FP_MULT_INT(x, n) ((x) * (n))

// 고정소수점 나눗셈
#define FP_DIV(x, y) (((int64_t)(x)) * F / (y))

// 고정소수점 / 정수
#define FP_DIV_INT(x, n) ((x) / (n))

#endif /* threads/fixed-point.h */