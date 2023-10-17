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
#include "i2crip.h"

i2cRipCmdStruct_t * g_i2cRipCmdList = NULL;
int g_i2cRipCmdListLength = 0;

#define EXIT(N) i2cRipExit(N)

static int getLine(FILE* file, char* buffer, int size, int* endOfFile);

static int parseLine(char* buffer, int size, i2cRipCmdStruct_t *i2cRipData);

int inputFileParser(const char* filename);

int ioCtlIf(int file, struct i2c_rdwr_ioctl_data *rdwr, int simulate);

static void i2cRipExit(int val)  __attribute__ ((noreturn));

static void help(void) __attribute__ ((noreturn));

static void help(void)
{
	fprintf(stderr,
		"Usage: i2crip [ACTION] I2CBUS CHIP-ADDRESS DATA-ADDRESS [VALUE]\n"
		"  ACTION is a flag to indicate read, write, or verify.\n"
		"    -r (Read)\n"
		"    -w (Write)\n"
		"    -v (Verify)\n"
		"  I2CBUS is an integer or an I2C bus name\n"
		"  ADDRESS is an integer (0x08 - 0x77, or 0x00 - 0x7f if -a is given)\n"
		"  VALUE is data to be written / verified\n");
	EXIT(1);
}

static int check_funcs(int file, int simulate)
{
	unsigned long funcs;

	if(simulate){
		return 0;
	}

	/* check adapter functionality */
	if (ioctl(file, I2C_FUNCS, &funcs) < 0) {
		fprintf(stderr, "Error: Could not get the adapter "
			"functionality matrix: %s\n", strerror(errno));
		return -1;
	}

	if (!(funcs & I2C_FUNC_I2C)) {
		fprintf(stderr, MISSING_FUNC_FMT, "I2C transfers");
		return -1;
	}

	return 0;
}

static int confirm(void)
{
	fprintf(stderr, "WARNING! This program can confuse your I2C bus, cause data loss and worse!\n");
	fprintf(stderr, "Continue? [y/N] ");
	fflush(stderr);
	if (!user_ack(0)) {
		fprintf(stderr, "Aborting on user request.\n");
		return 0;
	}

	return 1;
}

static int ioCtl_RDWR_If(int file, struct i2c_rdwr_ioctl_data *rdwr, int simulate){
	if(simulate){
		return rdwr->nmsgs;
	}
	return ioctl(file, I2C_RDWR, rdwr);
}

static int i2cRead(int file, int reg, int dreg, int dregSize, __u8 *data, int dataSize, int simulate){

	struct i2c_msg msgs[2];
	__u8 regBuff[2];

	if( 0 > reg || reg > 0xff){
		fprintf(stderr, "Error: Read Failed, Register size invalid: %d", dregSize);
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
			fprintf(stderr, "Error: Read Failed, Register size invalid: %d", dregSize);
			return 0;
	}

	if(0 > dataSize || dataSize > 2){
		fprintf(stderr, "Error: Read Failed, Data size invalid: %d", dataSize);
		return 0;
	}

	struct i2c_rdwr_ioctl_data rdwr;
	rdwr.msgs = msgs;
	rdwr.nmsgs = 2;

	int nmsgs_sent = ioCtl_RDWR_If(file, &rdwr, simulate);

	if (nmsgs_sent < 0) {
		fprintf(stderr, "Error: Sending messages failed: %s\n", strerror(errno));
		return 0;
	} else if (nmsgs_sent < 2) {
		fprintf(stderr, "Warning: only %d/2 messages were sent\n", nmsgs_sent);
		return 0;
	}

	return 1;
}

static int i2cWrite(int file, int reg, int dreg, int dregSize, __u8 *data, int dataSize, int simulate){

	struct i2c_msg msgs;
	__u8 buff[4];

	if( 0 > reg || reg > 0xff){
		fprintf(stderr, "Error: Write Failed, Register size invalid: %d", dregSize);
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
			fprintf(stderr, "Error: Read Failed, Register size invalid: %d", dregSize);
			return 0;
	}

	if(0 > dataSize || dataSize > 2){
		fprintf(stderr, "Error: Read Failed, Data size invalid: %d", dataSize);
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

	int nmsgs_sent = ioCtl_RDWR_If(file, &rdwr, simulate);
	
	if (nmsgs_sent < 0) {
		fprintf(stderr, "Error: Sending messages failed: %s\n", strerror(errno));
		return 0;
	} else if (nmsgs_sent < 1) {
		fprintf(stderr, "Warning: only %d/1 messages were sent\n", nmsgs_sent);
		return 0;
	}

	return 1;
}

static void logIf(int logToFile, int logToTerm, const char* buff){
	if(logToFile || logToTerm){
		// Assemble message
		if(logToFile){
			// TODO
		}
		if(logToTerm){
			fprintf(stdout, "%s", buff);
		}
	}
}

static int open_i2c_dev_If(int i2cBus, char *filename, int  size, int simulate){
	if(simulate){
		snprintf(filename, size, "Sim_I2cDev_%d", i2cBus);
		return 0;
	}
	return open_i2c_dev(i2cBus, filename, size, 0);
}

static int set_slave_addr_If(int file, int address, int simulate){
	const int force = 1;
	if(simulate){
		return 0;
	}
	return set_slave_addr(file, address, force);
}

int main(int argc, char *argv[]){
	int simulate = 1;
	int quiet = 0;
	int yes = 0;
	char filename[20];
	char *inputFile = NULL;

	int opt;

	/* handle (optional) flags first */
	while ((opt = getopt(argc, argv, "ysqh:")) != -1) {
		switch (opt) {
			case 'y': yes = 1; break;
			case 's': simulate = 1; break;
			case 'q': quiet = 1; break;
			case 'h':
			case '?':
				help();
				exit(opt == '?');
		}
	}
	(void)quiet;

	if (argc == optind + 1){
		inputFile = argv[optind];
		if (access(argv[optind], F_OK) == 0) {
			fprintf(stderr,"Using %s\n", inputFile);
		} else {
			fprintf(stderr,"Cannot find file %s\n", inputFile);
			help();
		}
	}
	else{
		fprintf(stderr,"Invalid number of argument.%d : %d\n", argc, optind + 2);
		help();
	}

	if(!inputFileParser(inputFile)){
		fprintf(stderr,"FAILED PARSING FILE\nEXITING\n");
		EXIT(0);
	}
	fprintf(stderr,"SIMULATING: %s: %d\n",(simulate) ? "True" : "False", simulate);
	if (!yes && !confirm()){
		EXIT(0);
	}

	i2cBusConnection_t i2cBusFiles[I2C_MAX_BUSSES];
	for(int i = 0; i < I2C_MAX_BUSSES; i++){
		i2cBusFiles[i].m_isConnected = 0;
		i2cBusFiles[i].m_slaveAddress = I2C_INVALID_SLAVE_ADDRESS;
	}


	int activeBus = I2C_NO_BUS_SELECTED;

	int error = 0;
	int errorSupression = 0;
	int logToFile = 0;
	int logToTerm = 0;
	
	int address;
	int i2cBus;

	__u16 dAddress = 0;
	__u8* pData = NULL;
	int dAddrSize = 0;
	int dataSize = 0;
	__u8 readWriteDate[MAX_READ_WRITE_SIZE];
	__u8 varData[MAX_READ_WRITE_SIZE];

	for(int i = 0; i < g_i2cRipCmdListLength; i++){
		i2cRipCmds_t cmd = g_i2cRipCmdList[i].m_cmd;
    	i2cRipCmdData_t* data = &g_i2cRipCmdList[i].m_data;
		switch(cmd){
			case I2C_RIP_SET_BUS:
				i2cBus = data->m_single;
				if ((i2cBus < 0) || (i2cBus >= I2C_MAX_BUSSES)){
					// failed
					fprintf(stderr,"Invalid Bus selection: %d\n", i2cBus);
					error = 1;
					break;
				}

				// If bus open
				if (i2cBusFiles[i2cBus].m_isConnected == 0){
					// Open i2c port
					i2cBusFiles[i2cBus].m_file = open_i2c_dev_If(i2cBus, filename, sizeof(filename), simulate);
					if (i2cBusFiles[i2cBus].m_file < 0){
						fprintf(stderr,"Unable to open bus %d\n", i2cBus);
						error = 1;
						break;	
					}
					if(check_funcs(i2cBusFiles[i2cBus].m_file, simulate)){
						fprintf(stderr,"Unable to find RDWD Function %d\n", i2cBus);
						error = 1;
						break;	
					}
					i2cBusFiles[i2cBus].m_isConnected = 1;
				}
				activeBus = i2cBus;
				logIf(logToFile, logToTerm, "SET BUS\n");
				break;

			case I2C_RIP_SET_ID:
				address = data->m_single;
				if ((activeBus < 0) || (activeBus >= I2C_MAX_BUSSES)){
					fprintf(stderr,"Invalid Active Bus: Out of range %d\n", activeBus);
					error = 1;
					break;
				}
				if(!i2cBusFiles[activeBus].m_isConnected){
					fprintf(stderr,"Invalid Active Bus: Not Connected %d\n", activeBus);
					error = 1;
					break;
				}
				if(set_slave_addr_If(i2cBusFiles[activeBus].m_file, address, simulate)){
					fprintf(stderr,"Unable to set slave address %d to bus %d\n", address, activeBus);
					i2cBusFiles[activeBus].m_slaveAddress = I2C_INVALID_SLAVE_ADDRESS;
					error = 1;
					break;
				}
				i2cBusFiles[activeBus].m_slaveAddress = address;
				logIf(logToFile, logToTerm, "SET ID\n");
				break;

			case I2C_RIP_DELAY:
					fprintf(stderr,"Please Implement delay. Cannot delay %dms\n", data->m_single);
					logIf(logToFile, logToTerm, "DELAY\n");
					break;

			case I2C_RIP_ERROR_DETECT:
				if(!data->m_single){
					errorSupression = 1;
					break;
				}
				errorSupression = 0;
				break;


			case I2C_RIP_LOG_TO_FILE:
				if(data->m_single){
					logToFile = 1;
					break;
				}
				logToFile = 0;
				break;

			case I2C_RIP_LOG_TO_TERM:
				if(data->m_single){
					logToTerm = 1;
					break;
				}
				logToTerm = 0;
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
					fprintf(stderr,"Invalid Active Bus: Out of range %d\n", activeBus);
					error = 1;
					break;
				}
				if(!i2cBusFiles[activeBus].m_isConnected){
					fprintf(stderr,"Invalid Active Bus: Not Connected %d\n", activeBus);
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
						fprintf(stderr,"Error: Invalid Write/Read/Verify command\n");
						error = 1;
						break;
				}

				if( dataSize >=  MAX_READ_WRITE_SIZE ){
					fprintf(stderr,"Error: Write/Read/Verify size too large %d\n", dataSize);
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
						if(!i2cWrite(i2cBusFiles[activeBus].m_file, i2cBusFiles[activeBus].m_slaveAddress, dAddress, dAddrSize, pData, dataSize, simulate)){
							fprintf(stderr,"Write Failed.\n");
							error = 1;
						}
						logIf(logToFile, logToTerm, "WROTE TO BUS\n");
						break;

					case I2C_RIP_8_READ_BYTE:
					case I2C_RIP_8_READ_WORD:
					case I2C_RIP_16_READ_BYTE:
					case I2C_RIP_16_READ_WORD:
						if(!i2cRead(i2cBusFiles[activeBus].m_file, i2cBusFiles[activeBus].m_slaveAddress, dAddress, dAddrSize, pData, dataSize, simulate)){
							fprintf(stderr,"Read Failed.\n");
							error = 1;
						}
						logIf(logToFile, logToTerm, "READ FROM BUS\n");
						break;

					case I2C_RIP_8_VERIFY_BYTE:
					case I2C_RIP_8_VERIFY_WORD:
					case I2C_RIP_16_VERIFY_BYTE:
					case I2C_RIP_16_VERIFY_WORD:
						if(!i2cRead(i2cBusFiles[activeBus].m_file, i2cBusFiles[activeBus].m_slaveAddress, dAddress, dAddrSize, pData, dataSize, simulate)){
							fprintf(stderr,"Read Failed.\n");
							error = 1;
							break;
						}
						
						if(memcmp(readWriteDate, varData, dataSize) != 0){
							fprintf(stderr,"Verification Failed.\n");
							error = 1;
						}
						logIf(logToFile, logToTerm, "VERIFIED FROM BUS\n");
						break;

					default:
						fprintf(stderr,"Error: Invalid Write/Read/Verify command\n");
						error = 1;
						break;
				}
				break;				

			default:
				fprintf(stderr,"Error: Invalid Write/Read/Verify command\n");
				error = 1;
				break;
		}

		if(error){
			logIf(logToFile, logToTerm, "VERIFIED FROM BUS\n");
			if(!errorSupression){
				break;
			}
			error = 0;
		}

	}
	
	if(!simulate){
		for(int i = 0; i < I2C_MAX_BUSSES; i++){
			if(i2cBusFiles[i].m_isConnected){
				close(i2cBusFiles[i].m_file);
			}
			i2cBusFiles[i].m_isConnected = 0;
		}
	}

	EXIT(0);
}

int inputFileParser(const char* filename){
	int failed = 0;
	FILE *file = fopen(filename, "r");
	int lines = 0;
	int numOfCmds = 0;
    char buffer[100];
	int endOfFile = 0;

    if (file == NULL) {
        fprintf(stderr,"File could not be opened\n");
        return 0;
    }	
	for(lines = 0; !endOfFile ; lines++){
		if(!getLine(file, buffer, 100, &endOfFile)){
			fprintf(stderr, "Buffer too small, line number: %d\n", lines);
			return 0;
		}
		
		i2cRipCmdStruct_t i2cRipData;
		if(!parseLine(buffer, 100, &i2cRipData)){
			fprintf(stderr, "Failed to parse line: %d: %s\n", lines + 1, buffer);
			failed = 1;
		}
		if (i2cRipData.m_isValid){
			numOfCmds++;
		}

		if(lines >= 100000){
			fprintf(stderr, "File too large or in recursive loop\n");
			return 0;
		}
	}

    fclose(file);

	if(lines <= 0){
		fprintf(stderr,"Empty File\n");
        return 0;
	}
	fprintf(stderr,"Number of lines: %d\n", lines);
	fprintf(stderr,"Number of valid cmds: %d\n", numOfCmds);

	if(failed){
		return 0;
	}

	// Allocate memory for commands
	g_i2cRipCmdList = (i2cRipCmdStruct_t *)malloc(sizeof(i2cRipCmdStruct_t) * numOfCmds);
	if (g_i2cRipCmdList == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return 0;
    }
	g_i2cRipCmdListLength = numOfCmds;

	// INIT
	for(int i = 0; i < g_i2cRipCmdListLength; i++){
		g_i2cRipCmdList[i].m_cmd = I2C_RIP_INVALID;
		g_i2cRipCmdList[i].m_isValid = 0;
	}

	// Open file again
	file = fopen(filename, "r");
	if (file == NULL) {
        fprintf(stderr,"File could not be opened\n");
        return 0;
    }

	numOfCmds = 0;
	endOfFile = 0;
	for(int i = 0; !endOfFile && (numOfCmds < g_i2cRipCmdListLength); i++){
		if(!getLine(file, buffer, 100, &endOfFile)){
			fprintf(stderr, "Buffer too small, line number: %d\n", i + 1);
			return 0;
		}
		
		if(!parseLine(buffer, 100, &g_i2cRipCmdList[numOfCmds])){
			fprintf(stderr, "Failed to parse line: %d: %s\n", lines + 1, buffer);
			failed = 1;
			return 0;
		}
		if (g_i2cRipCmdList[numOfCmds].m_isValid){
			numOfCmds++;
		}
	}

	fclose(file);
	return !failed;
}

// Gets next line in file;
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

int parseLine(char* buffer, int size, i2cRipCmdStruct_t *i2cRipData){
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
					fprintf(stderr,"Error: Argument too long\n");
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
						fprintf(stderr,"Error: Invalid Cmd: %s\n", subString);
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
						fprintf(stderr,"Error: Invalid Arg: %s\n", subString);
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
									fprintf(stderr,"Error: Invalid arguemts %s\n", subString);
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
									fprintf(stderr,"Error: Invalid arguemts %s\n", subString);
									return 0;
							}
							break;

						default:
							fprintf(stderr,"Error: Invalid arguemts %s\n", subString);
							return 0;
					}
					argNum++;
				}
			}
		}
		if(argNum != numArgReq){
			fprintf(stderr,"Error: Invalid number of arguments got %d: needed %d\n", argNum, numArgReq);
			return 0;
		}

		// Empty lines parse successfully
		// But command will not be valid
		if(i2cRipData->m_cmd != I2C_RIP_INVALID){
			i2cRipData->m_isValid = 1;
		}
		return 1;
}



// Frees all allocated memory
static void i2cRipExit(int val){
	if(g_i2cRipCmdListLength > 0){
		free(g_i2cRipCmdList);
	}
	
	exit(val);
}