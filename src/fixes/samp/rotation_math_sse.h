#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <xmmintrin.h>

namespace RotationMathSSE {

inline constexpr float kRadians = std::bit_cast<float>(0x3C8EFA35u);

inline bool CompatibleState()
{
    unsigned short control;
    __asm fnstcw control
    unsigned precision = control & 0x300;
    unsigned mxcsr = _mm_getcsr();
    return (control & 0xC3F) == 0x3F && (precision == 0 || precision == 0x200) &&
           (mxcsr & 0x6040) == 0 && (mxcsr & 0x1F80) == 0x1F80;
}

inline bool InRange(float value)
{
    return (std::bit_cast<uint32_t>(value) & 0x7FFFFFFFu) <= 0x43B40000u;
}

inline __m128 SinFold(__m128 angle)
{
    __m128 sign = _mm_and_ps(angle, _mm_set1_ps(-0.0f));
    __m128 absolute = _mm_andnot_ps(_mm_set1_ps(-0.0f), angle);
    __m128 value = _mm_or_ps(_mm_min_ps(absolute, _mm_sub_ps(_mm_set1_ps(3.14159265358979323846f), absolute)), sign);
    __m128 square = _mm_mul_ps(value, value);
    __m128 polynomial = _mm_set1_ps(2.5904881178100494e-06f);
    polynomial = _mm_add_ps(_mm_set1_ps(-0.00019800897549276573f), _mm_mul_ps(square, polynomial));
    polynomial = _mm_add_ps(_mm_set1_ps(0.008332899819376866f), _mm_mul_ps(square, polynomial));
    polynomial = _mm_add_ps(_mm_set1_ps(-0.16666647634365314f), _mm_mul_ps(square, polynomial));
    polynomial = _mm_add_ps(_mm_set1_ps(0.9999999765893699f), _mm_mul_ps(square, polynomial));
    return _mm_mul_ps(value, polynomial);
}

inline void Put(unsigned char *object, int offset, float value)
{
    std::memcpy(object + offset, &value, sizeof(value));
}

inline void Write(unsigned char *object, float x, float y, float z, const float *s, const float *c)
{
    Put(object, 0xAD, x); Put(object, 0xB1, y); Put(object, 0xB5, z);
    float szsx = s[2] * s[0], czsx = c[2] * s[0];
    Put(object, 0xDB, c[2] * c[1] - szsx * s[1]);
    Put(object, 0xDF, czsx * s[1] + s[2] * c[1]);
    Put(object, 0xE3, -(s[1] * c[0]));
    Put(object, 0xEB, -(s[2] * c[0]));
    Put(object, 0xEF, c[2] * c[0]);
    Put(object, 0xF3, s[0]);
    Put(object, 0xFB, szsx * c[1] + c[2] * s[1]);
    Put(object, 0xFF, s[2] * s[1] - czsx * c[1]);
    Put(object, 0x103, c[1] * c[0]);
}

inline bool Try(unsigned char *object, float x, float y, float z)
{
    if (!InRange(x) || !InRange(y) || !InRange(z) || !CompatibleState()) return false;
    __m128 radians = _mm_mul_ps(_mm_setr_ps(x, y, z, 0), _mm_set1_ps(kRadians));
    __m128 periods = _mm_cvtepi32_ps(_mm_cvtps_epi32(_mm_mul_ps(radians, _mm_set1_ps(0.15915494309189533577f))));
    radians = _mm_sub_ps(radians, _mm_mul_ps(periods, _mm_set1_ps(6.28318530717958647692f)));
    __m128 cosineAngle = _mm_add_ps(radians, _mm_set1_ps(1.57079632679489661923f));
    cosineAngle = _mm_sub_ps(cosineAngle, _mm_and_ps(_mm_cmpgt_ps(cosineAngle, _mm_set1_ps(3.14159265358979323846f)), _mm_set1_ps(6.28318530717958647692f)));
    float s[4], c[4];
    _mm_storeu_ps(s, SinFold(radians)); _mm_storeu_ps(c, SinFold(cosineAngle));
    Write(object, x, y, z, s, c);
    return true;
}

}
