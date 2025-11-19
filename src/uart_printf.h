#pragma once


/// @brief Fast non-blocking (20ms sleep inside) printf over UART
void uart_printf(const char *fmt, ...);