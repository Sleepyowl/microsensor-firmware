#pragma once


/**
 * @brief Enter deep sleep mode (System OFF).
 *
 * Configures the RTC interrupt GPIO and button GPIO as wake-up sources,
 * then transitions the system into deep sleep using @ref sys_poweroff.
 *
 * The system will restart upon a wake-up event triggered by either
 * the RTC interrupt or the button.
 *
 * @note This function does not return under normal operation, as the system
 *       powers off. The infinite loop after @ref sys_poweroff is a safeguard.
 *
 * @retval 0        Unreachable in normal operation
 * @retval -ENODEV  One or more required GPIO devices are not ready
 */
int enter_deep_sleep(void);
