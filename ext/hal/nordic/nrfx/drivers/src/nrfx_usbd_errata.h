/*$$$LICENCE_NORDIC_STANDARD<2017>$$$*/

#ifndef NRFX_USBD_ERRATA_H__
#define NRFX_USBD_ERRATA_H__

#include <stdbool.h>
/**
 * @defgroup nrfx_usbd_errata Functions to check if selected PAN is present in current chip
 * @{
 * @ingroup nrfx_usbd
 *
 * Functions here are checking the presence of an error in current chip.
 * The checking is done at runtime based on the microcontroller version.
 * This file is subject to removal when nRF51840 prototype support is removed.
 */

#ifndef NRFX_USBD_ERRATA_ENABLE
/**
 * @brief The constant that informs if errata should be enabled at all.
 *
 * If this constant is set to 0, all the Errata bug fixes will be automatically disabled.
 */
#define NRFX_USBD_ERRATA_ENABLE 1
#endif

/**
 * @brief Internal auxiliary function to check if the program is running on NRF52840 chip.
 * @retval true  It is NRF52480 chip.
 * @retval false It is other chip.
 */
static inline bool nrfx_usbd_errata_type_52840(void)
{
    return ((((*(uint32_t *)0xF0000FE0) & 0xFF) == 0x08) &&
        (((*(uint32_t *)0xF0000FE4) & 0x0F) == 0x0));
}

/**
 * @brief Internal auxiliary function to check if the program is running on first sample of
 *        NRF52840 chip.
 * @retval true  It is NRF52480 chip and it is first sample version.
 * @retval false It is other chip.
 */
static inline bool nrfx_usbd_errata_type_52840_proto1(void)
{
    return ( nrfx_usbd_errata_type_52840() &&
               ( ((*(uint32_t *)0xF0000FE8) & 0xF0) == 0x00 ) &&
               ( ((*(uint32_t *)0xF0000FEC) & 0xF0) == 0x00 ) );
}

/**
 * @brief Internal auxiliary function to check if the program is running on first final product of
 *        NRF52840 chip.
 * @retval true  It is NRF52480 chip and it is first final product.
 * @retval false It is other chip.
 */
static inline bool nrfx_usbd_errata_type_52840_fp1(void)
{
    return ( nrfx_usbd_errata_type_52840() &&
               ( ((*(uint32_t *)0xF0000FE8) & 0xF0) == 0x10 ) &&
               ( ((*(uint32_t *)0xF0000FEC) & 0xF0) == 0x00 ) );
}

/**
 * @brief Function to check if chip requires errata 104.
 *
 * Errata: USBD: EPDATA event is not always generated.
 *
 * @retval true  Errata should be implemented.
 * @retval false Errata should not be implemented.
 */
static inline bool nrfx_usbd_errata_104(void)
{
    return NRFX_USBD_ERRATA_ENABLE && nrfx_usbd_errata_type_52840_proto1();
}

/**
 * @brief Function to check if chip requires errata 154.
 *
 * Errata: During setup read/write transfer USBD acknowledges setup stage without SETUP task.
 *
 * @retval true  Errata should be implemented.
 * @retval false Errata should not be implemented.
 */
static inline bool nrfx_usbd_errata_154(void)
{
    return NRFX_USBD_ERRATA_ENABLE && nrfx_usbd_errata_type_52840_proto1();
}

/**
 * @brief Function to check if chip requires errata 166.
 *
 * Errata: ISO double buffering not functional.
 *
 * @retval true  Errata should be implemented.
 * @retval false Errata should not be implemented.
 */
static inline bool nrfx_usbd_errata_166(void)
{
    return NRFX_USBD_ERRATA_ENABLE && true;
}

/**
 * @brief Function to check if chip requires errata 171.
 *
 * Errata: USBD might not reach its active state.
 *
 * @retval true  Errata should be implemented.
 * @retval false Errata should not be implemented.
 */
static inline bool nrfx_usbd_errata_171(void)
{
    return NRFX_USBD_ERRATA_ENABLE && true;
}

/**
 * @brief Function to check if chip requires errata 187.
 *
 * Errata: USB cannot be enabled.
 *
 * @retval true  Errata should be implemented.
 * @retval false Errata should not be implemented.
 */
static inline bool nrfx_usbd_errata_187(void)
{
    return NRFX_USBD_ERRATA_ENABLE && nrfx_usbd_errata_type_52840_fp1();
}

/**
 * @brief Function to check if chip requires errata 200.
 *
 * Errata: SIZE.EPOUT not writable.
 *
 * @retval true  Errata should be implemented.
 * @retval false Errata should not be implemented.
 */
static inline bool nrfx_usbd_errata_sizeepout_rw(void)
{
    return NRFX_USBD_ERRATA_ENABLE && nrfx_usbd_errata_type_52840_proto1();
}

/**
 * @brief Function to check if chip requires errata 199.
 *
 * Errata: USBD cannot receive tasks during DMA.
 *
 * @retval true  Errata should be implemented.
 * @retval false Errata should not be implemented.
 */
static inline bool nrfx_usbd_errata_199(void)
{
    return NRFX_USBD_ERRATA_ENABLE && true;
}

/** @} */
#endif /* NRFX_USBD_ERRATA_H__ */
