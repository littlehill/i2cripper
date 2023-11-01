/*
    i2crip.c - A user-space program to write an I2C register.
    Copyright (C) 2001-2003  Frodo Looijaard <frodol@dds.nl>, and
                             Mark D. Studebaker <mdsxyz123@yahoo.com>
    Copyright (C) 2004-2022  Jean Delvare <jdelvare@suse.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
    MA 02110-1301 USA.
*/

#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
#include "i2cbusses.h"
#include "util.h"
#include "../version.h"

#define MAX_READ_WRITE_SIZE 64
#define MAX_DREG_SIZE 2

#define I2C_RIP_MAX_ARGUMENTS 50
#define I2C_RIP_LOOKUP_TABLE_SIZE 18

#define I2C_NO_BUS_SELECTED -1
#define I2C_INVALID_SLAVE_ADDRESS 0xFF
#define I2C_MAX_BUSSES 64

#define EXIT(N) i2cRipExit(N)

typedef struct i2cBusConnection {
	int m_file;
	int m_isConnected;
	__u8 m_slaveAddress;
}i2cBusConnection_t;

typedef enum i2cRipCmds {
	I2C_RIP_INVALID = -1,
	I2C_RIP_SET_BUS = 0,
	I2C_RIP_SET_ID,
	I2C_RIP_DELAY,
	I2C_RIP_SUPRESS_ERRORS,
	I2C_RIP_LOG_TO_FILE,
	I2C_RIP_LOG_TO_TERM,
	I2C_RIP_8_WRITE_BYTE,
	I2C_RIP_16_WRITE_BYTE,
	I2C_RIP_8_WRITE_WORD,
	I2C_RIP_16_WRITE_WORD,
	I2C_RIP_8_READ_BYTE,
	I2C_RIP_16_READ_BYTE,
	I2C_RIP_8_READ_WORD,
	I2C_RIP_16_READ_WORD,
	I2C_RIP_8_VERIFY_BYTE,
	I2C_RIP_16_VERIFY_BYTE,
	I2C_RIP_8_VERIFY_WORD,
	I2C_RIP_16_VERIFY_WORD,
} i2cRipCmds_t;

typedef struct ripCmd8_8{
    __u8 m_addr;
    __u8 m_data;
}ripCmd8_8_t;

typedef struct ripCmd8_16{
    __u8 m_addr;
    __u16 m_data;
}ripCmd8_16_t;

typedef struct ripCmd16_8{
    __u16 m_addr;
    __u8 m_data;
}ripCmd16_8_t;

typedef struct ripCmd16_16{
    __u16 m_addr;
    __u16 m_data;
}ripCmd16_16_t;

typedef union i2cRipCmdData{
    ripCmd8_8_t m_8_8;
    ripCmd8_16_t m_8_16;
    ripCmd16_8_t m_16_8;
    ripCmd16_16_t m_16_16;
    int m_single;
} i2cRipCmdData_t;

typedef struct i2cRipCmdsLookUp{
	i2cRipCmds_t m_cmd;
	int m_numArgs;
	char m_string[20];
} i2cRipCmdsLookUp_t ;

i2cRipCmdsLookUp_t g_cmdLookUpTable[I2C_RIP_LOOKUP_TABLE_SIZE] = {
	{I2C_RIP_SET_BUS, 1, "SET-BUS"},
	{I2C_RIP_SET_ID, 1, "SET-ID"},
	{I2C_RIP_DELAY, 1, "DELAY"},
	{I2C_RIP_SUPRESS_ERRORS, 1, "SUPRESS-ERRORS"},
	{I2C_RIP_LOG_TO_FILE, 1, "LOG-FILE"},
	{I2C_RIP_LOG_TO_TERM, 1, "LOG-TERM"},
	{I2C_RIP_8_WRITE_BYTE, 2, "WB-8"},
	{I2C_RIP_16_WRITE_BYTE, 2, "WB-16"},
	{I2C_RIP_8_WRITE_WORD, 2, "WW-8"},
	{I2C_RIP_16_WRITE_WORD, 2, "WW-16"},
	{I2C_RIP_8_READ_BYTE, 1, "RB-8"},
	{I2C_RIP_16_READ_BYTE, 1, "RB-16"},
	{I2C_RIP_8_READ_WORD, 1, "RW-8"},	
	{I2C_RIP_16_READ_WORD, 1, "RW-16"},	
	{I2C_RIP_8_VERIFY_BYTE, 2, "VB-8"},
	{I2C_RIP_16_VERIFY_BYTE, 2, "VB-16"},
	{I2C_RIP_8_VERIFY_WORD, 2, "VW-8"},	
	{I2C_RIP_16_VERIFY_WORD, 2, "VW-16"}
};

typedef struct i2cRipCmdStruct {
	i2cRipCmds_t m_cmd;
    i2cRipCmdData_t m_data;
	__u8 m_isValid;
} i2cRipCmdStruct_t;
