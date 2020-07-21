/*
 * Copyright (c) 2014-2017 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FW_SRC_MGOS_UART_HAL_H_
#define CS_FW_SRC_MGOS_UART_HAL_H_

#include "mgos_system.h"
#include "mgos_uart.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct mgos_uart_state {
  int uart_no;
  struct mgos_uart_config cfg;
  struct mbuf rx_buf;
  struct mbuf tx_buf;
  bool rx_enabled;
  bool xoff_recd;
  bool xoff_sent;
  struct mgos_uart_stats stats;
  mgos_uart_dispatcher_t dispatcher_cb;
  void *dispatcher_data;
  void *dev_data;
  struct mgos_rlock_type *lock;
  int locked;
};

struct mgos_uart_state *mgos_uart_hal_get_state(int uart_no);

/* Returns number of bytes available in the RX buffer. */
size_t mgos_uart_rxb_free(const struct mgos_uart_state *us);

/*
 * Device-specific initialization. Note that at this point config is not yet
 * set,
 * The only field that is valis uart_no.
 */
bool mgos_uart_hal_init(struct mgos_uart_state *us);

/*
 * Configure UART.
 * Note that this can be called repeatedly after init when UART is already
 * running.
 */
bool mgos_uart_hal_configure(struct mgos_uart_state *us,
                             const struct mgos_uart_config *cfg);
/* Set device-specific config defaults. */
void mgos_uart_hal_config_set_defaults(int uart_no,
                                       struct mgos_uart_config *cfg);

/* Read any available chars into rx_buf. Ints should be kept disabled. */
void mgos_uart_hal_dispatch_rx_top(struct mgos_uart_state *us);
/* Push chars from tx_buf out. Ints should be kept disabled. */
void mgos_uart_hal_dispatch_tx_top(struct mgos_uart_state *us);
/*
 * Finish this dispatch. Set up interrupts depending on the state of rx/tx bufs:
 *  - If rx_buf has availabel space, RX ints should be enabled.
 *  - if there is data to send, TX empty ints should be enabled.
 */
void mgos_uart_hal_dispatch_bottom(struct mgos_uart_state *us);

/* Wait for the FIFO to drain */
void mgos_uart_hal_flush_fifo(struct mgos_uart_state *us);

void mgos_uart_hal_set_rx_enabled(struct mgos_uart_state *us, bool enabled);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CS_FW_SRC_MGOS_UART_HAL_H_ */
