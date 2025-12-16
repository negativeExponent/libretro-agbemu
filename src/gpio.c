#include "gpio.h"

#include <time.h>

void rtc_init(RTC* rtc) {
    rtc->rbits = 0;
    rtc->num_rbits = 0;
    rtc->wbits = 0;
    rtc->num_wbits = 0;
    rtc->state = 0;
}

void gpio_init(GPIO *gpio) {
    gpio->data = 0;
    gpio->direction = 0;
    gpio->read_enabled = 0;

    rtc_init(&gpio->rtc);
}

byte bcd_to_decimal(byte x) {
    byte tens = x >> 4;
    byte ones = x & 0xf;
    return (tens * 10) + ones;
}

byte decimal_to_bcd(byte x) {
    byte tens = x / 10;
    byte ones = x % 10;
    return (tens << 4) | ones;
}

static void rtc_send(RTC* rtc, byte value) {
    for (int i = 0; i < 8; i++) {
        rtc->rbits <<= 1;
        rtc->rbits |= (value >> i) & 1;
    }
    rtc->num_rbits += 8;
}

static hword rtc_read_bit(RTC* rtc) {
    if (rtc->num_rbits > 0) {
        rtc->num_rbits--;
        return (rtc->rbits >> rtc->num_rbits) & 1;
    }
    return 0;
}

static void rtc_write_bit(RTC* rtc, hword value) {
    time_t rawtime;
    struct tm *timeinfo;

    rtc->wbits <<= 1;
    rtc->wbits |= value & 1;
    rtc->num_wbits++;

    if (rtc->state == 0) {  // Command received
        if (rtc->num_wbits < 8) return;
        rtc->state = (byte) rtc->wbits;
        rtc->rbits = 0;
        rtc->num_rbits = 0;
        rtc->wbits = 0;
        rtc->num_wbits = 0;

        switch (rtc->state) {
            case 0x60:  // Reset
            case 0x61:
                rtc->state = 0;
                break;

            case 0x62:  // Write status
                break;

            case 0x63:  // Read status
                rtc_send(rtc, STATUS_24HOUR);
                rtc->state = 0;
                break;

            case 0x64:  // Write date and time
                break;

            case 0x65:  // Read date and time
                time(&rawtime);
                timeinfo = localtime(&rawtime);
                rtc_send(rtc, decimal_to_bcd(timeinfo->tm_year % 100));
                rtc_send(rtc, decimal_to_bcd(timeinfo->tm_mon + 1));
                rtc_send(rtc, decimal_to_bcd(timeinfo->tm_mday));
                rtc_send(rtc, decimal_to_bcd(timeinfo->tm_wday));
                rtc_send(rtc, decimal_to_bcd(timeinfo->tm_hour));
                rtc_send(rtc, decimal_to_bcd(timeinfo->tm_min));
                rtc_send(rtc, decimal_to_bcd(timeinfo->tm_sec));
                rtc->state = 0;
                break;

            case 0x66:  // Write time
                break;

            case 0x67:  // Read time
                time(&rawtime);
                timeinfo = localtime(&rawtime);
                rtc_send(rtc, decimal_to_bcd(timeinfo->tm_hour));
                rtc_send(rtc, decimal_to_bcd(timeinfo->tm_min));
                rtc_send(rtc, decimal_to_bcd(timeinfo->tm_sec));
                rtc->state = 0;
                break;

            default:
                break;
        }
    } else {  // Data received
        switch (rtc->state) {
            case 0x62:  // Write status
                if (rtc->num_wbits < 8) return;
                // Do nothing
                rtc->state = 0;
                rtc->wbits = 0;
                rtc->num_wbits = 0;
                break;

            case 0x64:  // Write date and time
                if (rtc->num_wbits < 56) return;
                // Do nothing
                rtc->state = 0;
                rtc->wbits = 0;
                rtc->num_wbits = 0;
                break;

            case 0x66:  // Write time
                if (rtc->num_wbits < 24) return;
                // Do nothing
                rtc->state = 0;
                rtc->wbits = 0;
                rtc->num_wbits = 0;
                break;

            default:
                break;
        }
    }
}

hword gpio_read_halfword(GPIO* gpio, word addr) {
    if (gpio->read_enabled) {
        switch (addr) {
            case 0xc4: return gpio->data;
            case 0xc6: return gpio->direction;
            case 0xc8: return gpio->read_enabled;
            default: break;
        }
    }
    return 0;
}

void gpio_write_halfword(GPIO* gpio, word addr, hword value) {
    hword last_gpio_data;

    switch (addr) {
        case 0xc4:
            last_gpio_data = gpio->data;
            gpio->data = value & 0xf;
            if (/*gpio->has_rtc &&*/ (gpio->data & RTC_CS) && (gpio->data & RTC_SCK) && !(last_gpio_data & RTC_SCK)) {
                if (gpio->direction & RTC_SIO) {
                    if (gpio->data & RTC_SIO) {
                        rtc_write_bit(&gpio->rtc, 1);
                    } else {
                        rtc_write_bit(&gpio->rtc, 0);
                    }
                } else {
                    if (rtc_read_bit(&gpio->rtc)) {
                        gpio->data |= RTC_SIO;
                    } else {
                        gpio->data &= ~RTC_SIO;
                    }
                }
            }
            break;

        case 0xc6:
            gpio->direction = value & 0xf;
            break;

        case 0xc8:
            gpio->read_enabled = value & 1;
            break;

        default:
            break;
    }
}
