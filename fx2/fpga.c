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

#include "fpga.h"

#include <delay.h>
#include <fx2macros.h>
#include <fx2regs.h>

#include <stdio.h>


#define SYNCDELAY SYNCDELAY4
#define DELAY_CONFIG  {}
#define DELAY_SPI     {NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;}

/* fpga configuration */

#define DONE      (1<<0)
#define DONE_BIT  PA0
#define DONE_OE   OEA
#define DONE_IO   IOA

#define INIT_B      (1<<1)
#define INIT_B_BIT  PA1
#define INIT_B_OE   OEA
#define INIT_B_IO   IOA

#define CCLK      (1<<2)
#define CCLK_BIT  PA2
#define CCLK_OE   OEA
#define CCLK_IO   IOA

#define DIN      (1<<3)
#define DIN_BIT  PA3
#define DIN_OE   OEA
#define DIN_IO   IOA

#define PROG_B  (1<<2)
/* PROG_B connected to CTL2 line of the GPIF */

/* fpga spi */

#define SS_N     (1<<4)
#define SS_N_OE  OEA
#define SS_N_IO  IOA

#define SCLK     (1<<5)
#define SCLK_OE  OEA
#define SCLK_IO  IOA

#define MOSI     (1<<6)
#define MOSI_OE  OEA
#define MOSI_IO  IOA

#define MISO     (1<<7)
#define MISO_OE  OEA
#define MISO_IO  IOA


static BOOL fpga_upload_done;


void
fpga_init()
{
    fpga_upload_done = FALSE;
    
    DONE_OE &= ~DONE;
    INIT_B_OE &= ~INIT_B;
    CCLK_OE |= CCLK;
    DIN_OE |= DIN;
    GPIFIDLECTL |= PROG_B;
    SYNCDELAY;
    
    SS_N_IO |= SS_N;
    SS_N_OE |= SS_N;
    SCLK_IO &= ~SCLK;
    SCLK_OE |= SCLK;
    MOSI_IO &= ~MOSI;
    MOSI_OE |= MOSI;
    MISO_OE &= ~MISO;
}


BOOL
fpga_upload_init()
{
    BYTE timeout = 100;

    CCLK_IO |= CCLK;
    DIN_IO &= ~DIN;

    /* pulse prog_b */
    GPIFIDLECTL &= ~PROG_B;
    delay(2);
    GPIFIDLECTL |= PROG_B;
    
    fpga_upload_done = FALSE;

    /* wait for init_b */
    while (--timeout > 0 && !(INIT_B_IO & INIT_B)) delay(1);
    if (timeout == 0)
        return FALSE;

    return TRUE;
}


static void
fpga_upload_data_fast(BYTE len)
{
    /* len in dpl */
    (void)len;
    __asm
    mov  r2, dpl
    mov  dptr, #_XAUTODAT1
    00001$:
        mov  r3, #8
        movx a, @dptr
    00002$:
        rlc  a
        __endasm;
        DIN_BIT = FALSE;
        __asm
        jnc  00003$
        __endasm;
        DIN_BIT = TRUE;
        __asm
    00003$:
    __endasm;
        DELAY_CONFIG;
        CCLK_BIT = TRUE;
        DELAY_CONFIG;
        CCLK_BIT = FALSE;
        //if (DONE_BIT || !INIT_B_BIT)
        //    return;
    __asm
        djnz r3, 00002$
        djnz r2, 00001$
    __endasm;
}



BOOL
fpga_upload_data(BYTE *data, BYTE len)
{
    const BOOL last = len < 62; // FIXME: find better way of detecting end of data

    if (fpga_upload_done || len <= 0)
        return TRUE;

    AUTOPTRSETUP = 0x02; /* inc ptr 1 */
    AUTOPTRH1 = MSB(data);
    AUTOPTRL1 = LSB(data);
    #if 1
    fpga_upload_data_fast(len);
    if (DONE_BIT)
    {
        fpga_upload_done = TRUE;
        return TRUE;
    }
    if (!INIT_B_BIT)
        return FALSE;
    #else
    while (len-- > 0)
    {
        //BYTE b = *data++;
        BYTE b = EXTAUTODAT1;
        BYTE bit = 8;
        if (DONE_BIT)
        {
            fpga_upload_done = TRUE;
            break;
        }
        if (!INIT_B_BIT)
            return FALSE;
        do
        {
            DIN_BIT = FALSE;
            if (b & (1<<7))
                DIN_BIT = TRUE;
            b = (b << 1) | (b >> 7);
            DELAY_CONFIG;
            CCLK_BIT = TRUE;
            DELAY_CONFIG;
            CCLK_BIT = FALSE;
        }
        while (--bit > 0);
    }
    #endif
    DIN_BIT = FALSE;
    
    if (last && !fpga_upload_done)
    {
        WORD timeout = 1000;
        BYTE b;
        while (--timeout > 0)
        {
            if (DONE_BIT)
            {
                fpga_upload_done = TRUE;
                break;
            }
            if (!INIT_B_BIT)
                return FALSE;

            b = 255;
            while (b-- > 0)
            {
                DELAY_CONFIG;
                CCLK_BIT = TRUE;
                DELAY_CONFIG;
                CCLK_BIT = FALSE;
            }
        }
        if (timeout == 0)
            return FALSE;
    }
    
    return TRUE;
}


static BYTE
spi_transfer(BYTE addr, BYTE val)
{
    BYTE i = 2, b = addr, ret = 0;
    SS_N_IO &= ~SS_N;
    DELAY_SPI;
    
    while (i-- > 0)
    {
        BYTE bit = 8;
        while (bit-- > 0)
        {
            if (b & (1<<7))
                MOSI_IO |= MOSI;
            else
                MOSI_IO &= ~MOSI;
            b <<= 1;
            DELAY_SPI;
            SCLK_IO |= SCLK;
            DELAY_SPI;
            SCLK_IO &= ~SCLK;
            ret <<= 1;
            if (MISO_IO & MISO)
                ret |= (1<<0);
        }
        b = val;
    }

    DELAY_SPI;
    SS_N_IO |= SS_N;
    
    return ret;
}


void
fpga_write_reg(BYTE addr, BYTE val)
{
    spi_transfer(addr & ~(1<<7), val);
}


BYTE
fpga_read_reg(BYTE addr)
{
    return spi_transfer(addr | (1<<7), 0);
}

