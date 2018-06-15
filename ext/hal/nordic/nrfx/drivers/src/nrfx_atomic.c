/*$$$LICENCE_NORDIC_STANDARD<2018>$$$*/
#include "nrfx_atomic.h"

#ifndef NRFX_ATOMIC_USE_BUILD_IN
#if (defined(__GNUC__) && defined(WIN32))
    #define NRFX_ATOMIC_USE_BUILD_IN 1
#else
    #define NRFX_ATOMIC_USE_BUILD_IN 0
#endif
#endif // NRFX_ATOMIC_USE_BUILD_IN

#if ((__CORTEX_M >= 0x03U) || (__CORTEX_SC >= 300U))
#define STREX_LDREX_PRESENT
#endif


#if (NRFX_ATOMIC_USE_BUILD_IN == 0) && defined(STREX_LDREX_PRESENT)
#include "nrfx_atomic_internal.h"
#endif

uint32_t nrfx_atomic_u32_fetch_store(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_exchange_n(p_data, value, __ATOMIC_SEQ_CST);

#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(mov, old_val, new_val, p_data, value);
    return old_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    uint32_t old_val = *p_data;
    *p_data = value;
    NRFX_CRITICAL_SECTION_EXIT();
    return old_val;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_store(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    __atomic_store_n(p_data, value, __ATOMIC_SEQ_CST);
    return value;
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(mov, old_val, new_val, p_data, value);
    (void) old_val;
    return new_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    *p_data = value;
    NRFX_CRITICAL_SECTION_EXIT();
    return value;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_fetch_or(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_fetch_or(p_data, value, __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(orr, old_val, new_val, p_data, value);
    return old_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    uint32_t old_val = *p_data;
    *p_data |= value;
    NRFX_CRITICAL_SECTION_EXIT();
    return old_val;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_or(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_or_fetch(p_data, value, __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(orr, old_val, new_val, p_data, value);
    (void) old_val;
    return new_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    *p_data |= value;
    uint32_t new_value = *p_data;
    NRFX_CRITICAL_SECTION_EXIT();
    return new_value;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_fetch_and(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_fetch_and(p_data, value, __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(and, old_val, new_val, p_data, value);
    return old_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    uint32_t old_val = *p_data;
    *p_data &= value;
    NRFX_CRITICAL_SECTION_EXIT();
    return old_val;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_and(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_and_fetch(p_data, value, __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(and, old_val, new_val, p_data, value);
    (void) old_val;
    return new_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    *p_data &= value;
    uint32_t new_value = *p_data;
    NRFX_CRITICAL_SECTION_EXIT();
    return new_value;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_fetch_xor(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_fetch_xor(p_data, value, __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(eor, old_val, new_val, p_data, value);
    return old_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    uint32_t old_val = *p_data;
    *p_data ^= value;
    NRFX_CRITICAL_SECTION_EXIT();
    return old_val;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_xor(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_xor_fetch(p_data, value, __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(eor, old_val, new_val, p_data, value);
    (void) old_val;
    return new_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    *p_data ^= value;
    uint32_t new_value = *p_data;
    NRFX_CRITICAL_SECTION_EXIT();
    return new_value;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_fetch_add(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_fetch_add(p_data, value, __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(add, old_val, new_val, p_data, value);
    return old_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    uint32_t old_val = *p_data;
    *p_data += value;
    NRFX_CRITICAL_SECTION_EXIT();
    return old_val;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_add(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_add_fetch(p_data, value, __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(add, old_val, new_val, p_data, value);
    (void) old_val;
    return new_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    *p_data += value;
    uint32_t new_value = *p_data;
    NRFX_CRITICAL_SECTION_EXIT();
    return new_value;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_fetch_sub(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_fetch_sub(p_data, value, __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(sub, old_val, new_val, p_data, value);
    return old_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    uint32_t old_val = *p_data;
    *p_data -= value;
    NRFX_CRITICAL_SECTION_EXIT();
    return old_val;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_sub(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_sub_fetch(p_data, value, __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(sub, old_val, new_val, p_data, value);
    (void) old_val;
    return new_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    *p_data -= value;
    uint32_t new_value = *p_data;
    NRFX_CRITICAL_SECTION_EXIT();
    return new_value;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

bool nrfx_atomic_u32_cmp_exch(nrfx_atomic_u32_t * p_data,
                                           uint32_t *         p_expected,
                                           uint32_t           desired)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    return __atomic_compare_exchange(p_data,
                                     p_expected,
                                     &desired,
                                     1,
                                     __ATOMIC_SEQ_CST,
                                     __ATOMIC_SEQ_CST);
#elif defined(STREX_LDREX_PRESENT)
    return nrfx_atomic_internal_cmp_exch(p_data, p_expected, desired);
#else
    bool result;
    NRFX_CRITICAL_SECTION_ENTER();
    if(*p_data == *p_expected)
    {
        *p_data = desired;
        result = true;
    }
    else
    {
        *p_expected = *p_data;
        result = false;
    }
    NRFX_CRITICAL_SECTION_EXIT();
    return result;
#endif
}

uint32_t nrfx_atomic_u32_fetch_sub_hs(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    uint32_t expected = *p_data;
    uint32_t new_val;
    bool     success;

    do
    {
        if (expected >= value)
        {
            new_val = expected - value;
        }
        else
        {
            new_val = expected;
        }
        success = __atomic_compare_exchange(p_data,
                                            &expected,
                                            &new_val,
                                            1,
                                            __ATOMIC_SEQ_CST,
                                            __ATOMIC_SEQ_CST);
    } while(!success);
    return expected;
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(sub_hs, old_val, new_val, p_data, value);
    return old_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    uint32_t old_val = *p_data;
    *p_data -= value;
    NRFX_CRITICAL_SECTION_EXIT();
    return old_val;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_u32_sub_hs(nrfx_atomic_u32_t * p_data, uint32_t value)
{
#if NRFX_ATOMIC_USE_BUILD_IN
    uint32_t expected = *p_data;
    uint32_t new_val;
    bool     success;

    do
    {
        if (expected >= value)
        {
            new_val = expected - value;
        }
        else
        {
            new_val = expected;
        }
        success = __atomic_compare_exchange(p_data,
                                            &expected,
                                            &new_val,
                                            1,
                                            __ATOMIC_SEQ_CST,
                                            __ATOMIC_SEQ_CST);
    } while(!success);
    return new_val;
#elif defined(STREX_LDREX_PRESENT)
    uint32_t old_val;
    uint32_t new_val;

    NRFX_ATOMIC_OP(sub_hs, old_val, new_val, p_data, value);
    (void) old_val;
    return new_val;
#else
    NRFX_CRITICAL_SECTION_ENTER();
    *p_data -= value;
    uint32_t new_value = *p_data;
    NRFX_CRITICAL_SECTION_EXIT();
    return new_value;
#endif //NRFX_ATOMIC_USE_BUILD_IN
}

uint32_t nrfx_atomic_flag_set_fetch(nrfx_atomic_flag_t * p_data)
{
    return nrfx_atomic_u32_fetch_or(p_data, 1);
}

uint32_t nrfx_atomic_flag_set(nrfx_atomic_flag_t * p_data)
{
    return nrfx_atomic_u32_or(p_data, 1);
}

uint32_t nrfx_atomic_flag_clear_fetch(nrfx_atomic_flag_t * p_data)
{
    return nrfx_atomic_u32_fetch_and(p_data, 0);
}

uint32_t nrfx_atomic_flag_clear(nrfx_atomic_flag_t * p_data)
{
    return nrfx_atomic_u32_and(p_data, 0);
}
