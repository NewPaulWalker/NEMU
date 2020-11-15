#include <device/map.h>
#include <device/alarm.h>
#include <monitor/monitor.h>
#include <sys/time.h>

#define RTC_PORT 0x48   // Note that this is not the standard
#define RTC_MMIO 0xa1000048

static uint32_t *rtc_port_base = NULL;

static void rtc_io_handler(uint32_t offset, int len, nemu_bool is_write) {
  assert(offset == 0 || offset == 8 || offset == 12);
  if (!is_write) {
    struct timeval now;
    gettimeofday(&now, NULL);
    uint32_t seconds = now.tv_sec;
    uint32_t useconds = now.tv_usec;
    rtc_port_base[0] = seconds * 1000 + (useconds + 500) / 1000;
    rtc_port_base[2] = useconds;
    rtc_port_base[3] = seconds;
  }
}

static void timer_intr() {
  if (nemu_state.state == NEMU_RUNNING) {
    extern void dev_raise_intr(void);
    dev_raise_intr();
  }
}

void init_timer() {
  rtc_port_base = (uint32_t *)new_space(16);
  add_pio_map("rtc", RTC_PORT, (uint8_t *)rtc_port_base, 16, rtc_io_handler);
  add_mmio_map("rtc", RTC_MMIO, (uint8_t *)rtc_port_base, 16, rtc_io_handler);
  add_alarm_handle((void *)timer_intr);
}
