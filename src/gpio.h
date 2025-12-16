#ifndef GPIO_H
#define GPIO_H

#include "types.h"

// Real-time clock (Seiko S-3511A)
enum {
    RTC_SCK = 1,
    RTC_SIO = 2,
    RTC_CS = 4
};

#define STATUS_INTFE  2     // Frequency interrupt enable
#define STATUS_INTME  8     // Per-minute interrupt enable
#define STATUS_INTAE  0x20  // Alarm interrupt enable
#define STATUS_24HOUR 0x40  // 0: 12-hour mode, 1: 24-hour mode
#define STATUS_POWER  0x80  // Power on or power failure occurred

#define TEST_MODE     0x80  // Flag in the "second" byte

#define ALARM_AM      0
#define ALARM_PM      0x80

typedef struct _RTC {
    dword rbits;
    word num_rbits;
    dword wbits;
    word num_wbits;
    word state;
} RTC;

typedef struct _GPIO {
    hword data;
    hword direction;
    hword read_enabled;

    RTC rtc;
} GPIO;

void gpio_init(GPIO* gpio);
hword gpio_read_halfword(GPIO* gpio, word addr);
void gpio_write_halfword(GPIO* gpio, word addr, hword value);

#endif
