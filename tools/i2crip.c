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
#include <stdarg.h>
#include <time.h>
#include "i2cbusses.h"
#include "util.h"
#include "../version.h"
#include "i2crip.h"

/////////////////////// MACROS ////////////////////////

#define IS_LOG_ENABLED ((g_logToTerm || g_logToFile) && !g_quietMode)

/////////////////////// Global Vars ////////////////////////

static __u8 g_logToTerm = 1;	// Default on
static __u8 g_logToFile = 0;
static __u8 g_logFileOpen = 0;
static FILE* g_logFile = NULL;
static __u8 g_simulate = 0;
static __u8 g_debug = 0;
static __u8 g_supressErrors = 0;
static __u8 g_quietMode = 0;
static int g_i2cRipCmdListLength = 0;
static i2cRipCmdStruct_t* g_i2cRipCmdList = NULL;
static int g_cmdToLineNumberSize = 0;
static int* g_cmdToLineNumber = NULL;
static i2cBusConnection_t g_i2cBusFiles[I2C_MAX_BUSSES];

/////////////////// FUNCTIONS //////////////////

// Frees all allocated memory and exits program
static void i2cRipExit(int val){
	if(g_logFileOpen){
		fclose(g_logFile);
	}
	if(!g_simulate){
		for(int i = 0; i < I2C_MAX_BUSSES; i++){
			if(g_i2cBusFiles[i].m_isConnected){
				close(g_i2cBusFiles[i].m_file);
			}
			g_i2cBusFiles[i].m_isConnected = 0;
		}
	}
	if(g_i2cRipCmdListLength > 0){
		free(g_i2cRipCmdList);
	}
	if(g_cmdToLineNumberSize > 0){
		free(g_cmdToLineNumber);
	}
	exit(val);
}

// Logs errors messages
static void printToTerm(const char* fmt, ...){
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}


// Logs errors messages
static void logErrors(const char* fmt, ...){
	if(!g_quietMode){
		va_list args;
		va_start(args, fmt);
		
		// File logging is optional
		if(g_logToFile && g_logFileOpen){
			vfprintf(g_logFile, fmt, args);
		}

		vfprintf(stderr, fmt, args);
		va_end(args);
	}
}

// Logs normal messages
static void logMsg(const char* fmt, ...){
	if(IS_LOG_ENABLED){
		va_list args;
		va_start(args, fmt);
		if(g_logToFile && g_logFileOpen){
			vfprintf(g_logFile, fmt, args);
		}
		if(g_logToTerm){
			vfprintf(stdout, fmt, args);
		}
		va_end(args);
	}
}

// Help function returns message on how to use i2cRip
static void help(void){
	printToTerm(
		"Usage: i2crip [ACTION] FILELOCATION\n"
		"  ACTION is a flag to indicate read, write, or verify.\n"
		"    -y (Yes))\n"
		"    -s (Simulate)\n"
		"    -q (Quiet)\n"
		"    -h (Help)\n"
		"  FILELOCATION is the path to the intput file\n");
	EXIT(1);
}

// User Comfirmation
static int confirm(void){
	printToTerm("WARNING! This program will consume your I2C bus. May cause data loss and worse!\n");
	printToTerm("Continue? [y/N] ");
	fflush(stderr);
	if (!user_ack(0)) {
		printToTerm( "Aborting on user request.\n");
		return 0;
	}

	return 1;
}

// Parses one line
// InputFileParser Calls this function
static int parseLine(char* buffer, int size, i2cRipCmdStruct_t *i2cRipData){
		int start = 0;
		int argNum = 0;
		int numArgReq = 0;
		int endOfLine = 0;
		char subString[20];
		const int subStringSize = 20;
		i2cRipData->m_cmd = I2C_RIP_INVALID;
		i2cRipData->m_isValid = 0;
		for(int i = 0; i < size; i++){
			// Successful parse
			if(endOfLine){
				break;
			}

			// If found argument
			if((buffer[i] == ' ') || (buffer[i] == '\0') || (buffer[i] == '\t')){
				if(buffer[i] == '\0'){
					endOfLine = 1;
				}
				//Empty White space
				if(start == i){
					start++;
					continue;
				}

				// Find Argument
				if((i - start) >= subStringSize){
					logErrors("Error: Argument too long\n");
					return 0;
				}

				strncpy(subString, &buffer[start], i - start);
				subString[i - start] = '\0';
				start = i + 1;

				// If cmd not filled
				if(i2cRipData->m_cmd == I2C_RIP_INVALID){
					// Find Command
					for(int j = 0; j < I2C_RIP_LOOKUP_TABLE_SIZE; j++){
						if(strcmp(g_cmdLookUpTable[j].m_string, subString) == 0){
							i2cRipData->m_cmd = g_cmdLookUpTable[j].m_cmd;
							numArgReq = g_cmdLookUpTable[j].m_numArgs;
							break;
						}
					}
					// If unable to find command
					if(i2cRipData->m_cmd == I2C_RIP_INVALID){
						logErrors("Error: Invalid Cmd: %s\n", subString);
						return 0;
					}
				}
				else{
					long num = 0;
					char *endptr;
					int strLength = strlen(subString);
					if(strLength > 2){
						// Hex numbers
						if(subString[0] == '0' && subString[1] == 'x'){
							num = strtol(&subString[2], &endptr, 16);
						}
						else{
							num = strtol(subString, &endptr, 10);
						}
					}
					else{
						// Decimal conversion
						num = strtol(subString, &endptr, 10);
					}
					// Checks conversions
					if (*endptr != '\0')
					{
						logErrors("Error: Invalid Arg: %s\n", subString);
						return 0;
					}

					switch(argNum){
						// First Argument
						case 0:
							switch(i2cRipData->m_cmd){
								case I2C_RIP_SET_BUS:
								case I2C_RIP_SET_ID:
								case I2C_RIP_DELAY:
								case I2C_RIP_ERROR_DETECT:
								case I2C_RIP_LOG_TO_FILE:
								case I2C_RIP_LOG_TO_TERM:
									i2cRipData->m_data.m_single = (int)num;
									break;

								case I2C_RIP_8_WRITE_BYTE:
								case I2C_RIP_8_READ_BYTE:
								case I2C_RIP_8_VERIFY_BYTE:
									i2cRipData->m_data.m_8_8.m_addr = (__u8)num;
									break;

								case I2C_RIP_8_WRITE_WORD:
								case I2C_RIP_8_READ_WORD:
								case I2C_RIP_8_VERIFY_WORD:
									i2cRipData->m_data.m_8_16.m_addr = (__u16)num;
									break;

								case I2C_RIP_16_WRITE_BYTE:
								case I2C_RIP_16_READ_BYTE:
								case I2C_RIP_16_VERIFY_BYTE:
									i2cRipData->m_data.m_16_8.m_addr = (__u8)num;
									break;

								case I2C_RIP_16_WRITE_WORD:
								case I2C_RIP_16_READ_WORD:
								case I2C_RIP_16_VERIFY_WORD:
									i2cRipData->m_data.m_16_16.m_addr = (__u16)num;
									break;

								default:
									logErrors("Error: Invalid arguemts %s\n", subString);
									return 0;
							}
							break;

						// Second Argument
						case 1:
							switch(i2cRipData->m_cmd){
								case I2C_RIP_8_WRITE_BYTE:
								case I2C_RIP_8_READ_BYTE:
								case I2C_RIP_8_VERIFY_BYTE:
									i2cRipData->m_data.m_8_8.m_data = (__u8)num;
									break;

								case I2C_RIP_8_WRITE_WORD:
								case I2C_RIP_8_READ_WORD:
								case I2C_RIP_8_VERIFY_WORD:
									i2cRipData->m_data.m_8_16.m_data = (__u16)num;
									break;

								case I2C_RIP_16_WRITE_BYTE:
								case I2C_RIP_16_READ_BYTE:
								case I2C_RIP_16_VERIFY_BYTE:
									i2cRipData->m_data.m_16_8.m_data = (__u8)num;
									break;

								case I2C_RIP_16_WRITE_WORD:
								case I2C_RIP_16_READ_WORD:
								case I2C_RIP_16_VERIFY_WORD:
									i2cRipData->m_data.m_16_16.m_data = (__u16)num;
									break;

								default:
									logErrors("Error: Invalid arguemts %s\n", subString);
									return 0;
							}
							break;

						default:
							logErrors("Error: Invalid arguemts %s\n", subString);
							return 0;
					}
					argNum++;
				}
			}
		}
		if(argNum != numArgReq){
			logErrors("Error: Invalid number of arguments got %d: needed %d\n", argNum, numArgReq);
			return 0;
		}

		// Empty lines parse successfully
		// But command will not be valid
		if(i2cRipData->m_cmd != I2C_RIP_INVALID){
			i2cRipData->m_isValid = 1;
		}
		return 1;
}

// Gets one line of input file
// InputFileParser Calls this function
static int getLine(FILE* file, char* buffer, int size, int* endOfFile){
	int ch;
	*endOfFile = 0;

	for(int i = 0; i < size; i++){
		ch = fgetc(file);
		
		// Check for new line or EOF
		if(ch == '\n' || ch == EOF){
			if(ch == EOF){
				*endOfFile = 1;
			}
			buffer[i] = '\0';
			return 1;
		}

		// Ignore
		if (ch == '\r'){
			i--;
			continue;
		}

		// put on buffer
		buffer[i] = (char) ch;
	}

	// Out of buffer
	return 0;
}

// Parsing input file
static int inputFileParser(const char* filename){
	FILE *file = fopen(filename, "r");
	int failed = 0;
	int lines = 0;
	int numOfCmds = 0;
    char buffer[100];
	int endOfFile = 0;

    if (file == NULL) {
		logErrors("File: %s could not be opened\n", filename);
        return 0;
    }	
	for(lines = 0; !endOfFile ; lines++){
		if(!getLine(file, buffer, 100, &endOfFile)){
			logErrors("Error: Buffer too small, line number: %d\n", lines);
			failed = 1;
			break;
		}
		
		i2cRipCmdStruct_t i2cRipData;
		if(!parseLine(buffer, 100, &i2cRipData)){
			logErrors("Error: Failed to parse line: %d: %s\n", lines + 1, buffer);
			failed = 1;
			break;
		}
		if (i2cRipData.m_isValid){
			numOfCmds++;
		}

		if(lines >= 100000){
			logErrors("Error: File too large or in recursive loop\n");
			failed = 1;
			break;
		}
	}
    fclose(file);

	if(failed){
		return 0;
	}

	if(lines <= 0){
		logErrors("Empty File\n");
        return 0;
	}

	logMsg("Number of commands: %d\n", numOfCmds);

	// Allocate memory for commands
	g_i2cRipCmdList = (i2cRipCmdStruct_t *)malloc(sizeof(i2cRipCmdStruct_t) * numOfCmds);
	if (g_i2cRipCmdList == NULL) {
        logErrors("Error: Memory allocation failed\n");
        return 0;
    }
	g_i2cRipCmdListLength = numOfCmds;

	// Allocate memory for Debugging
	if(g_debug){
		g_cmdToLineNumber = (int *)malloc(sizeof(int) * numOfCmds);
		if (g_i2cRipCmdList == NULL) {
			logErrors("Error: Memory allocation failed\n");
			return 0;
		}
		g_cmdToLineNumberSize = numOfCmds;
	}


	// INIT
	for(int i = 0; i < g_i2cRipCmdListLength; i++){
		g_i2cRipCmdList[i].m_cmd = I2C_RIP_INVALID;
		g_i2cRipCmdList[i].m_isValid = 0;
	}

	// Open file again
	file = fopen(filename, "r");
	if (file == NULL) {
        logErrors("File could not be opened\n");
        return 0;
    }

	numOfCmds = 0;
	endOfFile = 0;
	for(int i = 0; !endOfFile && (numOfCmds < g_i2cRipCmdListLength); i++){
		if(!getLine(file, buffer, 100, &endOfFile)){
			logErrors("Error: Buffer too small, line number: %d\n", i + 1);
			failed = 1;
			break;
		}
		
		if(!parseLine(buffer, 100, &g_i2cRipCmdList[numOfCmds])){
			logErrors("Error: Failed to parse line: %d: %s\n", lines + 1, buffer);
			failed = 1;
			break;
		}
		if (g_i2cRipCmdList[numOfCmds].m_isValid){
			if(numOfCmds < g_cmdToLineNumberSize){
				g_cmdToLineNumber[numOfCmds] = i + 1;
			}
			numOfCmds++;
		}
	}

	fclose(file);
	return !failed;
}

// Checks IOCtrl For correct functions
static int check_funcs(int file){
	unsigned long funcs;

	if(g_simulate){
		return 0;
	}

	/* check adapter functionality */
	if (ioctl(file, I2C_FUNCS, &funcs) < 0) {
		logErrors("Error: Could not get the adapter "
			"functionality matrix: %s\n", strerror(errno));
		return -1;
	}

	if (!(funcs & I2C_FUNC_I2C)) {
		logErrors("Error: I2cDevice missing I2C transfers functions\n");
		return -1;
	}

	return 0;
}

// RDWR Interface
// Allows us to inject simulation to fake ioCtl Calls
static int ioCtlRdwrIf(int file, struct i2c_rdwr_ioctl_data *rdwr){
	if(g_simulate){
		return rdwr->nmsgs;
	}
	return ioctl(file, I2C_RDWR, rdwr);
}

// i2cRead function to be called by main program
static int i2cRead(int file, int reg, int dreg, int dregSize, __u8 *data, int dataSize){

	struct i2c_msg msgs[2];
	__u8 regBuff[2];

	if( 0 > reg || reg > 0xff){
		logErrors("Error: Read Failed, Register size invalid: %d", dregSize);
		return 0;
	}

	// Add device ID register
	msgs[0].addr = reg;
	msgs[1].addr = reg;

	// First Message write, Second read
	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;

	// Point to respected buffers
	msgs[0].buf = regBuff;
	msgs[1].buf = data;

	// Message length
	msgs[0].len = dregSize;
	msgs[1].len = dataSize;

	switch(dregSize){
		case 2:
			regBuff[0] = (__u8)(dreg & 0xFF00) >> 8;
			regBuff[1] = (__u8)(dreg & 0xFF);
			break;
		case 1:
			regBuff[0] = (__u8)(dreg & 0xFF);
			break;
		default:
			logErrors("Error: Read Failed, Register size invalid: %d", dregSize);
			return 0;
	}

	if(0 > dataSize || dataSize > 2){
		logErrors("Error: Read Failed, Data size invalid: %d", dataSize);
		return 0;
	}

	struct i2c_rdwr_ioctl_data rdwr;
	rdwr.msgs = msgs;
	rdwr.nmsgs = 2;

	int nmsgs_sent = ioCtlRdwrIf(file, &rdwr);

	if (nmsgs_sent < 0) {
		logErrors("Error: Sending messages failed: %s\n", strerror(errno));
		return 0;
	} else if (nmsgs_sent < (int)rdwr.nmsgs) {
		logErrors("Error: only %d/%s messages were sent\n", nmsgs_sent, rdwr.nmsgs);
		return 0;
	}

	return 1;
}

// i2cWrite function to be called by main program
static int i2cWrite(int file, int reg, int dreg, int dregSize, __u8 *data, int dataSize){

	struct i2c_msg msgs;
	__u8 buff[4];

	if( 0 > reg || reg > 0xff){
		logErrors("Error: Write Failed, Register size invalid: %d", dregSize);
		return 0;
	}

	// Add device ID register
	msgs.addr = reg;

	// First Message write, Second read
	msgs.flags = 0;

	// Point to respected buffers
	msgs.buf = buff;

	int index = 0;
	switch(dregSize){
		case 2:
			buff[index++] = (__u8)(dreg & 0xFF00) >> 8;
			buff[index++] = (__u8)(dreg & 0xFF);
			break;
		case 1:
			buff[index++] = (__u8)(dreg & 0xFF);
			break;
		default:
			logErrors("Error: Read Failed, Register size invalid: %d", dregSize);
			return 0;
	}

	if(0 > dataSize || dataSize > 2){
		logErrors("Error: Read Failed, Data size invalid: %d", dataSize);
		return 0;
	}
	for(int i = 0; i < dataSize; i++){
		buff[index++] = data[i];
	}

	// Message length
	msgs.len = index;

	struct i2c_rdwr_ioctl_data rdwr;
	rdwr.msgs = &msgs;
	rdwr.nmsgs = 1;

	int nmsgs_sent = ioCtlRdwrIf(file, &rdwr);
	
	if (nmsgs_sent < 0) {
		logErrors("Error: Sending messages failed: %s\n", strerror(errno));
		return 0;
	} else if (nmsgs_sent < (int)rdwr.nmsgs) {
		logErrors("Error: only %d/%d messages were sent\n", nmsgs_sent, rdwr.nmsgs);
		return 0;
	}

	return 1;
}

// Opens i2c interface for ioCtl
static int open_i2c_dev_If(int i2cBus, char *filename, int  size){
	if(g_simulate){
		snprintf(filename, size, "Sim_I2cDev_%d", i2cBus);
		return 0;
	}
	return open_i2c_dev(i2cBus, filename, size, 0);
}

// Sets slave address on i2cBus
static int set_slave_addr_If(int file, int address){
	const int force = 1;
	if(g_simulate){
		return 0;
	}
	return set_slave_addr(file, address, force);
}

// Entry point to function
int main(int argc, char *argv[]){
	int yes = 0;
	char filename[20];
	char logFileName[20] = "i2cRip.log";
	char *inputFile = NULL;
	int opt;	

	/* handle (optional) flags first */
	while ((opt = getopt(argc, argv, "ysdqh:")) != -1) {
		switch (opt) {
			case 'y': yes = 1; break;
			case 's': g_simulate = 1; break;
			case 'q': g_quietMode = 1; break;
			case 'd': g_debug = 1; break;
			case 'h':
			case '?':
				help();
				EXIT(opt == '?');
		}
	}

	if (argc == optind + 1){
		inputFile = argv[optind];
		if (access(argv[optind], F_OK) == 0) {
			logErrors("Error: Using %s\n", inputFile);
		} else {
			logErrors("Error: Cannot find file %s\n", inputFile);
			help();
		}
	}
	else{
		logErrors("Error: Invalid number of argument.%d : %d\n", argc, optind + 2);
		help();
	}

	if(!inputFileParser(inputFile)){
		printToTerm("Failed parsing input file %s\n", inputFile);
		EXIT(0);
	}

	if(g_simulate){
		logMsg("Simulating I2cDevice\n");
	}
	
	if (!yes && !confirm()){
		EXIT(0);
	}

	for(int i = 0; i < I2C_MAX_BUSSES; i++){
		g_i2cBusFiles[i].m_isConnected = 0;
		g_i2cBusFiles[i].m_slaveAddress = I2C_INVALID_SLAVE_ADDRESS;
	}

	int error = 0;
	int address;
	int i2cBus;
	int activeBus = I2C_NO_BUS_SELECTED;

	__u16 dAddress = 0;
	__u8 * pData = NULL;
	int dAddrSize = 0;
	int dataSize = 0;
	__u8 readWriteDate[MAX_READ_WRITE_SIZE];
	__u8 varData[MAX_READ_WRITE_SIZE];

	for(int i = 0; i < g_i2cRipCmdListLength; i++){
		i2cRipCmds_t cmd = g_i2cRipCmdList[i].m_cmd;
    	i2cRipCmdData_t* data = &g_i2cRipCmdList[i].m_data;
		char lineNumStr[15] = "";

		if(i < g_cmdToLineNumberSize){
			snprintf(lineNumStr, 15, "Line %d:", g_cmdToLineNumber[i]);
		}

		switch(cmd){
			case I2C_RIP_SET_BUS:
				i2cBus = data->m_single;
				if ((i2cBus < 0) || (i2cBus >= I2C_MAX_BUSSES)){
					// failed
					logErrors("%sError: Invalid Bus selection: %d\n", lineNumStr, i2cBus);
					error = 1;
					break;
				}

				// If bus open
				if (g_i2cBusFiles[i2cBus].m_isConnected == 0){
					// Open i2c port
					g_i2cBusFiles[i2cBus].m_file = open_i2c_dev_If(i2cBus, filename, sizeof(filename));
					if (g_i2cBusFiles[i2cBus].m_file < 0){
						logErrors("%sError: Unable to open bus %d\n", lineNumStr, i2cBus);
						error = 1;
						break;	
					}
					if(check_funcs(g_i2cBusFiles[i2cBus].m_file)){
						logErrors("%sError: Unable to find RDWD Function %d\n", lineNumStr, i2cBus);
						error = 1;
						break;	
					}
					g_i2cBusFiles[i2cBus].m_isConnected = 1;
					g_i2cBusFiles[i2cBus].m_slaveAddress = I2C_INVALID_SLAVE_ADDRESS;
				}
				activeBus = i2cBus;
				logMsg("%sChanged I2cBus %d\n", lineNumStr, activeBus);
				break;

			case I2C_RIP_SET_ID:
				address = data->m_single;
				if ((activeBus < 0) || (activeBus >= I2C_MAX_BUSSES)){
					logErrors("%sError: Invalid Active Bus: Out of range %d\n", lineNumStr, activeBus);
					error = 1;
					break;
				}
				if(!g_i2cBusFiles[activeBus].m_isConnected){
					logErrors("%sError: Invalid Active Bus: Not Connected %d\n", lineNumStr, activeBus);
					error = 1;
					break;
				}
				if(set_slave_addr_If(g_i2cBusFiles[activeBus].m_file, address)){
					logErrors("%sError: Unable to set slave address %d to bus %d\n", lineNumStr, address, activeBus);
					g_i2cBusFiles[activeBus].m_slaveAddress = I2C_INVALID_SLAVE_ADDRESS;
					error = 1;
					break;
				}
				g_i2cBusFiles[activeBus].m_slaveAddress = address;
				logMsg("%sChanged Slave addess %x on bus %d\n", lineNumStr, g_i2cBusFiles[activeBus].m_slaveAddress, activeBus);
				break;

			case I2C_RIP_DELAY:
				if(data->m_single <= 0){
					logErrors("%sError: Invalid Delay time%d.\n");
					g_supressErrors = 1;
					break;
				}
				logMsg("%sDelay of %dms\n", lineNumStr, (int)data->m_single);
				usleep((int)data->m_single * 1000);
				break;

			case I2C_RIP_ERROR_DETECT:
				if(!data->m_single){
					g_supressErrors = 1;
					break;
				}
				g_supressErrors = 0;
				logMsg("%sSupress Errors: %s\n", lineNumStr, (g_supressErrors) ? "Enabled" : "Disabled");
				break;


			case I2C_RIP_LOG_TO_FILE:
				g_logToFile = 0;
				if(data->m_single){
					if(g_logFileOpen == 0){
						g_logFile = fopen(logFileName, "w");
						if (g_logFile == NULL) {
							logErrors("%sError: LogFile: %s could not be opened\n", lineNumStr, logFileName);
							break;
						}	
					}
					g_logToFile = 1;
					break;
				}
				logMsg("%sLogging to file %s: %s\n",lineNumStr, logFileName, (g_logToFile) ? "Enabled" : "Disabled");
				break;

			case I2C_RIP_LOG_TO_TERM:
				g_logToTerm = 0;
				if(data->m_single){
					g_logToTerm = 1;
					break;
				}
				logMsg("%sLogging to Term: %s\n", lineNumStr, (g_logToTerm) ? "Enabled" : "Disabled");
				break;

			case I2C_RIP_8_WRITE_BYTE:
			case I2C_RIP_8_READ_BYTE:
			case I2C_RIP_8_VERIFY_BYTE:
			case I2C_RIP_8_WRITE_WORD:
			case I2C_RIP_8_READ_WORD:
			case I2C_RIP_8_VERIFY_WORD:
			case I2C_RIP_16_WRITE_BYTE:
			case I2C_RIP_16_READ_BYTE:
			case I2C_RIP_16_VERIFY_BYTE:
			case I2C_RIP_16_WRITE_WORD:
			case I2C_RIP_16_READ_WORD:
			case I2C_RIP_16_VERIFY_WORD:
				dAddrSize = 0;
				dataSize = 0;
				dAddress = 0;
				pData = NULL;

				if ((activeBus < 0) || (activeBus >= I2C_MAX_BUSSES)){
					logErrors("%sError: Invalid Active Bus: Out of range %#x\n",  lineNumStr, activeBus);
					error = 1;
					break;
				}
				if(!g_i2cBusFiles[activeBus].m_isConnected){
					logErrors("%sError: Invalid Active Bus: Not Connected %#x\n", lineNumStr, activeBus);
					error = 1;
					break;
				}
				if(g_i2cBusFiles[activeBus].m_slaveAddress == I2C_INVALID_SLAVE_ADDRESS){
					logErrors("%sError: Invalid slave address %#x\n", lineNumStr, g_i2cBusFiles[activeBus].m_slaveAddress);
					error = 1;
					break;
				}

				switch(cmd){
					case I2C_RIP_8_VERIFY_BYTE:
					case I2C_RIP_8_WRITE_BYTE:
					case I2C_RIP_8_READ_BYTE:
						dAddrSize = 1;
						dataSize = 1;
						dAddress = (__u16)data->m_8_8.m_addr;
						pData = (__u8*)&data->m_8_8.m_data;
						break;


					case I2C_RIP_8_WRITE_WORD:
					case I2C_RIP_8_READ_WORD:
					case I2C_RIP_8_VERIFY_WORD:
						dAddress = (__u16)data->m_8_16.m_addr;
						pData = (__u8*)&data->m_8_16.m_data;
						dAddrSize = 1;
						dataSize = 2;
						break;

					case I2C_RIP_16_WRITE_BYTE:
					case I2C_RIP_16_READ_BYTE:
					case I2C_RIP_16_VERIFY_BYTE:
						dAddress = (__u16)data->m_16_8.m_addr;
						pData = (__u8*)&data->m_16_8.m_data;
						dAddrSize = 2;
						dataSize = 1;
						break;

					case I2C_RIP_16_WRITE_WORD:
					case I2C_RIP_16_READ_WORD:
					case I2C_RIP_16_VERIFY_WORD:
						dAddress = (__u16)data->m_16_16.m_addr;
						pData = (__u8*)&data->m_16_16.m_data;
						dAddrSize = 2;
						dataSize = 2;
						break;

					default:
						logErrors("%sError: Invalid Write/Read/Verify command\n", lineNumStr);
						error = 1;
						break;
				}

				if( dataSize >=  MAX_READ_WRITE_SIZE ){
					logErrors("%sError: Write/Read/Verify size too large %d\n", lineNumStr, dataSize);
					error = 1;
					break;
				}

				// Memcpy would work if we could guarintee endieness
				for(int j = 0; j < dataSize; j++){
					readWriteDate[j] = *pData;
					varData[j] = *pData;
					pData++;
				}

				switch(cmd){
					case I2C_RIP_8_WRITE_BYTE:
					case I2C_RIP_8_WRITE_WORD:
					case I2C_RIP_16_WRITE_BYTE:
					case I2C_RIP_16_WRITE_WORD:
						if(!i2cWrite(g_i2cBusFiles[activeBus].m_file, g_i2cBusFiles[activeBus].m_slaveAddress, dAddress, dAddrSize, readWriteDate, dataSize)){
							logErrors("%sError: Failed to Write. Bus: %d, Address: %#x\n", lineNumStr, activeBus, g_i2cBusFiles[activeBus].m_slaveAddress);
							error = 1;
							break;
						}
						if(IS_LOG_ENABLED){
							logMsg("%sWriting %d byte(s). Reg %d byte(s) long. %#x\n\tDATA:", lineNumStr, dataSize, dAddrSize, dAddress);
							for (int j = 0; j < dataSize; j++){
								logMsg("%#04x,", readWriteDate[j]);
							}
							logMsg("\n");
						}
						break;

					case I2C_RIP_8_READ_BYTE:
					case I2C_RIP_8_READ_WORD:
					case I2C_RIP_16_READ_BYTE:
					case I2C_RIP_16_READ_WORD:
						if(!i2cRead(g_i2cBusFiles[activeBus].m_file, g_i2cBusFiles[activeBus].m_slaveAddress, dAddress, dAddrSize, readWriteDate, dataSize)){
							logErrors("%sError: Failed to Read. Bus: %d, Address: %#x\n", lineNumStr, activeBus, g_i2cBusFiles[activeBus].m_slaveAddress);
							error = 1;
							break;
						}
						if(IS_LOG_ENABLED){
							logMsg("%sReading %d byte(s). Reg %d byte(s) long. %#x\n\tDATA:", lineNumStr, dataSize, dAddrSize, dAddress);
							for (int j = 0; j < dataSize; j++){
								logMsg("%#04x,", readWriteDate[j]);
							}
							logMsg("\n");
						}
						break;

					case I2C_RIP_8_VERIFY_BYTE:
					case I2C_RIP_8_VERIFY_WORD:
					case I2C_RIP_16_VERIFY_BYTE:
					case I2C_RIP_16_VERIFY_WORD:
						if(!i2cRead(g_i2cBusFiles[activeBus].m_file, g_i2cBusFiles[activeBus].m_slaveAddress, dAddress, dAddrSize, readWriteDate, dataSize)){
							logErrors("%sError: Failed to Verify. Bus: %d, Address: %#x\n", lineNumStr, activeBus, g_i2cBusFiles[activeBus].m_slaveAddress);
							error = 1;
							break;
						}
						
						if(memcmp(readWriteDate, varData, dataSize) != 0){
							error = 1;
						}
						if(IS_LOG_ENABLED){
							logMsg("%sVerifing %d byte(s). Reg %d byte(s) long. %#x\n\tDATA:", lineNumStr, dataSize, dAddrSize, dAddress);
							for (int j = 0; j < dataSize; j++){
								logMsg("%#04x,", readWriteDate[j]);
							}
							logMsg("\n\tCTRL:");
							for (int j = 0; j < dataSize; j++){
								logMsg("%#04x,", varData[j]);
							}
							logMsg("\n");
						}
						break;

					default:
						logErrors("%sError: Invalid Write/Read/Verify command\n", lineNumStr);
						error = 1;
						break;
				}
				break;				

			default:
				logErrors("%sError: Invalid Write/Read/Verify command\n", lineNumStr);
				error = 1;
				break;
		}

		if(error){
			if(!g_supressErrors){
				break;
			}
		}
		error = 0;
	}
	printToTerm("Exiting: I2cRip %s\n", (error) ? "FAILED" : "was SUCCESSFUL");

	EXIT(0);
}