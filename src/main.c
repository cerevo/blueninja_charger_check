#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "TZ01_system.h"
#include "TZ01_console.h"

#include "utils.h"

#include "BQ24250.h"

extern ARM_DRIVER_I2C Driver_I2C2;

typedef enum _stat {
    ST_NONE,
    ST_WAIT_SEL_REG,
    ST_WAIT_VAL_U,
    ST_WAIT_VAL_L,
    ST_WAIT_CONFIRM,
}   ST_APP;

static ST_APP stat = ST_NONE;
static uint8_t reg_index, chr_reg_u, chr_reg_l;
static char msg[83], binval[9];

static uint8_t (*reg_get[])(void) = {
    NULL,
    BQ24250_drv_reg01_get,
    BQ24250_drv_reg02_get,
    BQ24250_drv_reg03_get,
    BQ24250_drv_reg04_get,
    BQ24250_drv_reg05_get,
    BQ24250_drv_reg06_get,
    BQ24250_drv_reg07_get,
};

static bool (*reg_set[])(uint8_t) = {
    NULL,
    BQ24250_drv_reg01_set,
    BQ24250_drv_reg02_set,
    BQ24250_drv_reg03_set,
    BQ24250_drv_reg04_set,
    BQ24250_drv_reg05_set,
    BQ24250_drv_reg06_set,
    BQ24250_drv_reg07_set,
};


static char* byte_to_binstr(uint8_t d, char *buf)
{
    for (int i = 0; i < 8; i++) {
        buf[i] = '0' + ((d >> (7 - i)) & 0x01);
    }
    buf[8] = '\0';
    return buf;
}

static bool is_hex_char(uint8_t c)
{
    int val;
    val = c - '0';
    if ((val >= 0) && (val <= 9)) {
        return true;
    }
    
    val = c - 'a';
    if ((val >= 0) && (val < 6)) {
        return true;
    }
    
    val = c - 'A';
    if ((val >= 0) && (val < 6)) {
        return true;
    }
    
    return false;
}

static int hexchar_to_int(uint8_t c)
{
    int val;

    val = c - '0';
    if ((val >= 0) && (val <= 9)) {
        return val;
    }
    
    val = c - 'a';
    if ((val >= 0) && (val < 6)) {
        return (val + 10);
    }
    
    val = c - 'A';
    if ((val >= 0) && (val < 6)) {
        return (val + 10);
    }
    
    return -1; 
}

static void show_menu(void)
{
    TZ01_console_puts("\r\n");
    TZ01_console_puts("* BQ24250 Register configure tool *\r\n");
    TZ01_console_puts("Select register 1 to 7: ");
}

static void state_func_none(uint8_t key)
{
    
}

static void state_func_wait_sel_reg(uint8_t key)
{
    uint8_t reg;
    switch (key) {
        case '\r':
            show_menu();
            break;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
            TZ01_console_putc(key);
            TZ01_console_puts("\r\n");
            reg_index = key - '0';
            reg = (reg_get[reg_index])();
            sprintf(msg, "REG%d: value=0x%02x [%s]\r\n> ", reg_index, reg, byte_to_binstr(reg, binval));
            TZ01_console_puts(msg);
            stat = ST_WAIT_VAL_U;
            break;
    }
}

static void state_func_wait_val_u(uint8_t key)
{
    switch (key) {
        case '\r':
            /* Return to Register select. */
            TZ01_console_puts("\r\n");
            show_menu();
            stat = ST_WAIT_SEL_REG;
            break;
        default:
            if (is_hex_char(key)) {
                TZ01_console_putc(key);
                chr_reg_u = key;
                stat = ST_WAIT_VAL_L;
            }
            break;
    }
}

static void state_func_wait_val_l(uint8_t key)
{
    switch (key) {
        case '\x08':
            TZ01_console_putc('\x08');
            TZ01_console_putc('\x20');
            TZ01_console_putc('\x08');

            stat = ST_WAIT_VAL_U;
            break;
        default:
            if (is_hex_char(key)) {
                TZ01_console_putc(key);
                chr_reg_l = key;
                stat = ST_WAIT_CONFIRM;
            }
            break;
    }    
}

static void state_func_wait_confirm(uint8_t key)
{
    int tmp;
    uint8_t val;
    
    switch (key) {
        case '\r':
            TZ01_console_puts("\r\n");
            /* Confirm */
            tmp = hexchar_to_int(chr_reg_l);
            if (tmp == -1) {
                /* failed */
                return;
            }
            val = tmp & 0x0f;
            
            tmp = hexchar_to_int(chr_reg_u);
            if (tmp == -1) {
                /* failed */
                return;
            }
            val |= (tmp & 0x0f) << 4;
            
            /**/
            if ((reg_set[reg_index])(val) == false) {
                /* failed */
                return;
            }
            TZ01_console_puts("Register set finished.\r\n");
            
            /* Dump reg */
            val = (reg_get[reg_index])();
            sprintf(msg, "REG%d: value=0x%02x [%s]\r\n", reg_index, val, byte_to_binstr(val, binval));
            TZ01_console_puts(msg);
            
            show_menu();
            stat = ST_WAIT_SEL_REG;
            break;
        case '\x08':
            /* Backspace */
            TZ01_console_putc('\x08');
            TZ01_console_putc('\x20');
            TZ01_console_putc('\x08');
            stat = ST_WAIT_VAL_L;
            break;
    }
}

static void (*state_func[])(uint8_t) = {
    state_func_none,
    state_func_wait_sel_reg,
    state_func_wait_val_u,
    state_func_wait_val_l,
    state_func_wait_confirm
};

static bool reg_edit_proc(void)
{
    uint8_t key;
    
    if (TZ01_console_getc(&key) == false) {
        return false;
    }
    
    (state_func[stat])(key);
    
    return true;
}

int main(void)
{
    /* Initialize */
    TZ01_system_init();
    TZ01_console_init();
 
    if (BQ24250_drv_init(&Driver_I2C2, true) == false) {
        goto terminate;
    }
    
    show_menu();
    stat = ST_WAIT_SEL_REG;
    
    for (;;) {
        if (TZ01_system_run() == RUNEVT_POWOFF) {
            /* Power off operation OR Low voltage detected */
            break;
        }
        
        reg_edit_proc();
    }
    
 terminate:
    TZ01_console_puts("Program terminated.\r\n");
    return 0;
}
