/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mongoose.h"

#include "esp_missing_includes.h"

#ifdef RTOS_SDK
#include <esp_common.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#else
#include <user_interface.h>
#endif

#include "common/cs_dbg.h"
#include "mgos_app.h"
#include "mgos_core_dump.h"
#include "mgos_debug_internal.h"
#include "mgos_hal.h"
#include "mgos_init_internal.h"
#include "mgos_mongoose_internal.h"
#ifdef MGOS_HAVE_OTA_COMMON
#include "mgos_ota.h"
#endif
#include "mgos_uart_internal.h"

#ifdef MGOS_HAVE_ADC
#include "esp_adc.h"
#endif
#include "esp_coredump.h"
#include "esp_exc.h"
#include "esp_features.h"
#include "esp_fs.h"
#include "esp_hw.h"
#include "esp_hw_wdt.h"
#include "esp_periph.h"
#include "esp_rboot.h"
#include "esp_umm_malloc.h"
#include "esp_vfs_dev_sysflash.h"

#ifdef RTOS_SDK

#ifndef MGOS_TASK_STACK_SIZE
#define MGOS_TASK_STACK_SIZE 8192 /* in bytes */
#endif

#ifndef MGOS_TASK_PRIORITY
#define MGOS_TASK_PRIORITY 5
#endif

#else

#ifndef MGOS_TASK_PRIORITY
#define MGOS_TASK_PRIORITY 1
#endif

#endif /* RTOS_SDK */

#ifndef MGOS_TASK_QUEUE_LENGTH
#define MGOS_TASK_QUEUE_LENGTH 32
#endif

#ifndef MGOS_MONGOOSE_MAX_POLL_SLEEP_MS
#define MGOS_MONGOOSE_MAX_POLL_SLEEP_MS 1000
#endif

extern const char *build_version, *build_id;
extern const char *mg_build_version, *mg_build_id;

bool uart_initialized = false;

static os_timer_t s_mg_poll_tmr;

static uint32_t s_mg_polls_in_flight = 0;

static IRAM void mgos_mg_poll_cb(void *arg) {
  mgos_ints_disable();
  s_mg_polls_in_flight--;
  mgos_ints_enable();
  int timeout_ms = 0;
  if (mongoose_poll(0) == 0) {
    /* Nothing is happening now, see when next timer is due. */
    double min_timer = mg_mgr_min_timer(mgos_get_mgr());
    if (min_timer > 0) {
      /* Note: timeout_ms can get negative if a timer is past due. That's ok. */
      timeout_ms = (int) ((min_timer - mg_time()) * 1000.0);
      if (timeout_ms < 0) {
        timeout_ms = 0; /* Now */
      } else if (timeout_ms > MGOS_MONGOOSE_MAX_POLL_SLEEP_MS) {
        timeout_ms = MGOS_MONGOOSE_MAX_POLL_SLEEP_MS;
      }
    } else {
      timeout_ms = MGOS_MONGOOSE_MAX_POLL_SLEEP_MS;
    }
  } else {
    /* Things are happening, we need another poll ASAP. */
  }
  if (timeout_ms == 0) {
    mongoose_schedule_poll(false /* from_isr */);
  } else {
    os_timer_disarm(&s_mg_poll_tmr);
    /* We set repeat = true in case things get stuck for any reason. */
    os_timer_arm(&s_mg_poll_tmr, timeout_ms, 1 /* repeat */);
  }
  (void) arg;
}

IRAM void mongoose_schedule_poll(bool from_isr) {
  mgos_ints_disable();
  if (s_mg_polls_in_flight < 2) {
    s_mg_polls_in_flight++;
    mgos_ints_enable();
    if (mgos_invoke_cb(mgos_mg_poll_cb, NULL, from_isr)) {
      return;
    } else {
      /* Ok, that didn't work, roll back our counter change. */
      mgos_ints_disable();
      s_mg_polls_in_flight--;
      /*
       * Not much else we can do here, the queue is full.
       * Background poll timer will eventually restart polling.
       */
    }
  } else {
    /* There are at least two pending callbacks, don't bother. */
  }
  mgos_ints_enable();
}

void mg_lwip_mgr_schedule_poll(struct mg_mgr *mgr) {
  (void) mgr;
  mongoose_schedule_poll(false /* from_isr */);
}

IRAM void sdk_putc(char c) {
  if (mgos_debug_uart_is_suspended()) return;
  esp_exc_putc(c);
}

enum mgos_init_result esp_mgos_init2(void) {
#ifdef CS_MMAP
  mgos_vfs_mmap_init();
#endif
  enum mgos_init_result ir = mgos_debug_uart_init();
  if (ir != MGOS_INIT_OK) return ir;
  uart_initialized = true;
  cs_log_set_level(MGOS_EARLY_DEBUG_LEVEL);
  setvbuf(stdout, NULL, _IOLBF, 256);
  setvbuf(stderr, NULL, _IOLBF, 256);
  /* Note: putc can be invoked from int handlers. */
  os_install_putc1(sdk_putc);
  fputc('\n', stderr);

  if (strcmp(MGOS_APP, "mongoose-os") != 0) {
    LOG(LL_INFO, ("%s %s (%s)", MGOS_APP, build_version, build_id));
  }
  LOG(LL_INFO, ("Mongoose OS %s (%s)", mg_build_version, mg_build_id));
  LOG(LL_INFO, ("CPU: %s, %d MHz, RAM: %u total, %u free",
                esp_chip_type_str(esp_get_chip_type()),
                (int) (mgos_get_cpu_freq() / 1000000), mgos_get_heap_size(),
                mgos_get_free_heap_size()));
  LOG(LL_INFO, ("SDK %s; flash: %uM", system_get_sdk_version(),
                esp_vfs_dev_sysflash_get_size(NULL) / 1048576));
  esp_print_reset_info();

  system_soft_wdt_stop();
  ir = mgos_init();
  if (ir != MGOS_INIT_OK) {
    LOG(LL_ERROR, ("%s init error: %d", "MG", ir));
    return ir;
  }

  return MGOS_INIT_OK;
}

static void esp_mgos_init(void) {
  enum mgos_init_result result = esp_mgos_init2();
  bool success = (result == MGOS_INIT_OK);
#ifdef MGOS_HAVE_OTA_COMMON
  mgos_ota_boot_finish(success, mgos_ota_is_first_boot());
#endif
  if (!success) {
    LOG(LL_ERROR, ("Init failed: %d", result));
    /* Arbitrary delay to make potential reboot loop less tight. */
    mgos_usleep(500000);
    mgos_system_restart();
  }
}

#ifdef RTOS_SDK
static xQueueHandle s_main_queue;

struct mgos_event {
  mgos_cb_t cb;
  void *arg;
};

xSemaphoreHandle s_mtx;

IRAM bool mgos_invoke_cb(mgos_cb_t cb, void *arg, bool from_isr) {
  struct mgos_event e = {.cb = cb, .arg = arg};
  if (from_isr) {
    long int should_yield = false;
    if (!xQueueSendToBackFromISR(s_main_queue, &e, &should_yield)) {
      return false;
    }
    if (should_yield) {
      /* Hm? */
    }
  } else {
    return xQueueSendToBack(s_main_queue, &e, 10);
  }
  return true;
}

static void mgos_task(void *arg) {
  struct mgos_event e;
  s_main_queue = xQueueCreate(MGOS_TASK_QUEUE_LENGTH, sizeof(e));

  esp_mgos_init();

  mongoose_schedule_poll(false /* from_isr */);

  while (true) {
    /* Keep soft WDT disabled. */
    system_soft_wdt_stop();
    if (xQueueReceive(s_main_queue, &e, 10 /* tick */)) {
      e.cb(e.arg);
    }
    taskYIELD();
  }
  (void) arg;
}

#else /* !RTOS_SDK */

static os_event_t s_main_queue[MGOS_TASK_QUEUE_LENGTH];

IRAM bool mgos_invoke_cb(mgos_cb_t cb, void *arg, bool from_isr) {
  if (!system_os_post(MGOS_TASK_PRIORITY, (uint32_t) cb, (uint32_t) arg)) {
    return false;
  }
  (void) from_isr;
  return true;
}

static void mgos_lwip_task(os_event_t *e) {
  mgos_cb_t cb = (mgos_cb_t)(e->sig);
  cb((void *) e->par);
  /* Keep soft WDT disabled. */
  system_soft_wdt_stop();
}

void sdk_init_done_cb(void) {
  system_os_task(mgos_lwip_task, MGOS_TASK_PRIORITY, s_main_queue,
                 MGOS_TASK_QUEUE_LENGTH);
  esp_core_dump_init();
  esp_mgos_init();
  mongoose_schedule_poll(false);
}

#endif

extern void __libc_init_array(void);

void _init(void) {
  // Called by __libc_init_array after global ctors. No further action required.
}

void user_init(void) {
  uart_div_modify(0, UART_CLK_FREQ / MGOS_DEBUG_UART_BAUD_RATE);
#ifdef MGOS_HAVE_ADC
  /* Note: it's critical to call this early to record value at boot ASAP. */
  esp_adc_init();
#endif
  mgos_uart_init();
  mgos_debug_init();
  srand(system_get_time() ^ system_get_rtc_time());
  os_timer_disarm(&s_mg_poll_tmr);
  os_timer_setfn(&s_mg_poll_tmr, (void (*)(void *)) mongoose_schedule_poll,
                 /* RTOS callbacks are executed in ISR context; for non-OS it
                    doesn't matter. */
                 (void *) true);
  esp_hw_wdt_setup(ESP_HW_WDT_26_8_SEC, ESP_HW_WDT_26_8_SEC);
  /* Soft WDT feeds HW WDT, we don't want this. */
  system_soft_wdt_stop();
#ifdef RTOS_SDK
  s_mtx = xSemaphoreCreateRecursiveMutex();
  xTaskCreate(mgos_task, (const signed char *) "mgos",
              MGOS_TASK_STACK_SIZE / 4, /* specified in 32-bit words */
              NULL, MGOS_TASK_PRIORITY, NULL);
#else
  esp_exception_handler_init();
  __libc_init_array(); /* C++ global contructors. */
  system_init_done_cb(sdk_init_done_cb);
#endif
}

void user_rf_pre_init() {
  /* Early init app hook. */
  system_update_cpu_freq(SYS_CPU_160MHZ);
  uart_div_modify(0, UART_CLK_FREQ / MGOS_DEBUG_UART_BAUD_RATE);
  mgos_app_preinit();
}

#ifndef FW_RF_CAL_DATA_ADDR
#error FW_RF_CAL_DATA_ADDR is not defined
#endif
uint32_t user_rf_cal_sector_set(void) {
  /* Defined externally. */
  return FW_RF_CAL_DATA_ADDR / 4096;
}
