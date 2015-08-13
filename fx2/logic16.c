/*
 * This file is part of the la16fw project.
 *
 * Copyright (C) 2014-2015 Gregor Anich
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <autovector.h>
#include <delay.h>
#include <eputils.h>
#include <fx2ints.h>
#include <fx2macros.h>
#include <i2c.h>

#include "fpga.h"
#include "gpif_stuff.h"

#include "debug.h"

#define  SYNCDELAY SYNCDELAY4

/* logic16 ep1 commands */

#define CMD_START_ACQUISITION        0x01
#define CMD_ABORT_ACQUISITION_ASYNC  0x02
#define CMD_WRITE_EEPROM             0x06
#define CMD_READ_EEPROM              0x07
#define CMD_WRITE_LED_TABLE          0x7a
#define CMD_SET_LED_MODE             0x7b
#define CMD_RETURN_TO_BOOTLOADER     0x7c
#define CMD_ABORT_ACQUISITION_SYNC   0x7d
#define CMD_FPGA_UPLOAD_INIT         0x7e
#define CMD_FPGA_UPLOAD_DATA         0x7f
#define CMD_FPGA_WRITE_REGISTER      0x80
#define CMD_FPGA_READ_REGISTER       0x81
#define CMD_GET_REVID                0x82

#define I2C_EEPROM_ADDRESS  0x50


static __xdata BYTE led_table[64] = {0};
static BOOL led_run = FALSE;
static BOOL led_repeat = FALSE;
static BYTE led_div = 0;


/* logic16 specific ep1 encode/decode functions */

static void
ep1_encrypt(BYTE *dst, BYTE *src, BYTE count)
{
    BYTE st[2] = {0x9b, 0x54};
    while (count-- > 0)
    {
        BYTE s, x;
        s = *src++;
        x = (((s ^ st[1] ^ 0x2b) - 0x05) ^ 0x35) - 0x39;
        x = (((x ^ st[0] ^ 0x5a) - 0xb0) ^ 0x38) - 0x45;
        *dst++ = x;
        st[0] = s;
        st[1] = x;
    }
}

static void
ep1_decrypt(BYTE *dst, BYTE *src, BYTE count)
{
    BYTE st[2] = {0x9b, 0x54};
    while (count-- > 0)
    {
        BYTE s, x;
        s = *src++;
        x = (((s + 0x45) ^ 0x38) + 0xb0) ^ 0x5a ^ st[0];
        x = (((x + 0x39) ^ 0x35) + 0x05) ^ 0x2b ^ st[1];
        *dst++ = x;
        st[0] = x;
        st[1] = s;
    }
}

/* what? */

BOOL
handle_get_descriptor()
{
    return FALSE;
}

/* configuration */

BOOL
handle_get_interface(BYTE interface, BYTE *alt_interface)
{
    if (interface != 0)
        return FALSE;
    *alt_interface = 0;
    return TRUE;
}


BOOL
handle_set_interface(BYTE interface, BYTE alt_interface)
{
    if (interface != 0 || alt_interface != 0)
        return FALSE;
    
    RESETTOGGLE(0x01); /* ep1 out */
    RESETTOGGLE(0x81); /* ep1 in */
    RESETTOGGLE(0x82); /* ep2 in */
    RESETTOGGLE(0x06); /* ep6 out */
    
    RESETFIFO(2);
    
    return TRUE;
}


BYTE
handle_get_configuration()
{
    return 1;
}


BOOL
handle_set_configuration(BYTE config)
{
    if (config != 1)
        return FALSE;
    return TRUE;
}

/* vendor commands */

BOOL
handle_vendorcommand(BYTE command)
{
    (void) command;
    return FALSE;
}

/* initialization */

static void
ep_init()
{
    printf("ep_init\r\n");

    /* setup ep1 in/out */
    EP1INCFG = (1<<7) | /* valid */
               (1<<5) | (0<<4); /* bulk */
    SYNCDELAY;
    EP1OUTCFG = (1<<7) | /* valid */
                (1<<5) | (0<<4); /* bulk */
    SYNCDELAY;
    OUTPKTEND = 0x81;
    SYNCDELAY;
    OUTPKTEND = 0x81;
    SYNCDELAY;

    /* disable other endpoints */
    EP2CFG = 0;
    SYNCDELAY;
    EP4CFG = 0;
    SYNCDELAY;
    EP6CFG = 0;
    SYNCDELAY;
    EP8CFG = 0;
    SYNCDELAY;
    
    /* prepare ep1 out */
    EP1OUTCS &= ~bmEPSTALL;
    EP1OUTBC = 0xff;
    SYNCDELAY;
}


void
main_init()
{
    REVCTL = bmNOAUTOARM | bmSKIPCOMMIT;
    SYNCDELAY;
    SETCPUFREQ(CLK_48M);
    SYNCDELAY;
    
    printf("main_init\r\n");

    ep_init();
    gpif_stuff_init();
    fpga_init();
    
    /* timer2 */
    CKCON &= ~(1<<5);
    T2CON = (1<<2); /* run timer 2 in auto reload mode */
}

/* called periodically by the main loop (unless device is suspended) */

BOOL first = TRUE;

void
main_loop()
{
    if (first)
    {
        first = FALSE;
        printf("main_loop CPUCS = 0x%x\r\n", CPUCS);
    }


    /* check for ep1 out data */
    if(!(EP1OUTCS & bmEPBUSY))
    {
        BYTE *buf_out = EP1OUTBUF;
        BYTE *buf_in = EP1INBUF;
        BYTE len_out = EP1OUTBC;
        BYTE len_in = 0;
        BOOL ok = FALSE;
        //printf("ep1 out\r\n");
        
        /* decrypt data */
        ep1_decrypt(buf_out, buf_out, len_out);
        
        /* handle command */
        //if (buf_out[0] != CMD_FPGA_UPLOAD_DATA)
        //    printf("cmd 0x%x len %d\r\n", buf_out[0], len_out);
        switch (buf_out[0])
        {
        case CMD_WRITE_EEPROM:
            if (len_out > 5 && buf_out[1] == 0x42 && buf_out[2] == 0x55 && (buf_out[4] + 5) == len_out &&
                eeprom_write(I2C_EEPROM_ADDRESS, buf_out[3], buf_out[4], buf_out + 5))
            {
                ok = TRUE;
            }
            break;
        case CMD_READ_EEPROM:
            if (len_out == 5 && buf_out[1] == 0x33 && buf_out[2] == 0x81)
            {
                /* wait for ep1in ready */
                while (EP1INCS & bmEPBUSY);
                if (eeprom_read(I2C_EEPROM_ADDRESS, buf_out[3], buf_out[4], buf_in))
                {
                    len_in = buf_out[4];
                    ok = TRUE;
                }
            }
            break;
            
        case CMD_WRITE_LED_TABLE:
            if (len_out > 3 && (buf_out[2] + 3) == len_out && buf_out[1] < sizeof (led_table))
            {
                BYTE *dst = led_table + buf_out[1];
                BYTE *src = buf_out + 3;
                BYTE len = buf_out[2];
                while (len-- > 0)
                {
                    *dst++ = *src++;
                    if (dst == led_table + sizeof (led_table))
                        dst = led_table;
                }
                ok = TRUE;
            }
            break;
        case CMD_SET_LED_MODE:
            if (len_out == 6)
            {
                led_run = buf_out[1];
                RCAP2L = buf_out[2];
                RCAP2H = buf_out[3];
                led_div = buf_out[4];
                led_repeat = buf_out[5];
                ok = TRUE;
            }
            break;
            
        case CMD_FPGA_UPLOAD_INIT:
            ok = fpga_upload_init();
            break;
        case CMD_FPGA_UPLOAD_DATA:
            if (len_out > 2 && (buf_out[1] + 2) == len_out)
            {
                ok = fpga_upload_data(buf_out + 2, buf_out[1]);
            }
            break;
        case CMD_FPGA_WRITE_REGISTER:
            if (len_out > 2 && (2*buf_out[1] + 2) == len_out)
            {
                BYTE i;
                for (i = 0; i < buf_out[1]; i++)
                    fpga_write_reg(buf_out[2 + 2*i], buf_out[2 + 2*i + 1]);
                ok = TRUE;
            }
            break;
        case CMD_FPGA_READ_REGISTER:
            if (len_out > 2 && (buf_out[1] + 2) == len_out)
            {
                BYTE i;
                /* wait for ep1in ready */
                while (EP1INCS & bmEPBUSY);
                for (i = 0; i < buf_out[1]; i++)
                    buf_in[i] = fpga_read_reg(buf_out[2 + i]);
                len_in = buf_out[1];
                ok = TRUE;
            }
            break;


        case CMD_START_ACQUISITION:
            if (len_out == 1)
            {
                gpif_stuff_start();
                ok = TRUE;
            }
            break;
        case CMD_ABORT_ACQUISITION_ASYNC:
            if (len_out == 1)
            {
                gpif_stuff_abort();
                ok = TRUE;
            }
            break;
        case CMD_ABORT_ACQUISITION_SYNC:
            if (len_out == 2)
            {
                gpif_stuff_abort();
                /* wait for ep1in ready */
                while (EP1INCS & bmEPBUSY);
                buf_in[0] = buf_out[1] ^ 0xff;
                len_in = 1;
                ok = TRUE;
            }
            break;
            
// CMD_RETURN_TO_BOOTLOADER     0x7c
// CMD_GET_REVID                0x82
        }
        
        if (!ok)
        {
            /* stall ep1 */
            EP1OUTCS |= bmEPSTALL; /* FIXME: dont stall? */
            printf("STALL\r\n");
        }
        else if (len_in > 0)
        {
            /* send reply */
            ep1_encrypt(buf_in, buf_in, len_in);
            SYNCDELAY;
            EP1INBC = len_in;
        }
        
        /* prepare ep1 out for next packet */
        EP1OUTBC = 0xff;
        SYNCDELAY;
    }
    
    /* led stuff */
    if (TF2)
    {
        static BYTE count = 0, index = 0;
        CLEAR_TIMER2();
        if (led_run)
        {
            if (count == 0)
            {
                count = led_div;
                if (index < sizeof (led_table))
                    fpga_write_reg(5, led_table[index++]);
                if (index == sizeof (led_table) && led_repeat)
                    index = 0;
            }
            else
            {
                count -= 1;
            }
        }
    }
}
