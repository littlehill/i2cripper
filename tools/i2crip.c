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


#define EXIT(N) i2cRipExit(N)

static int getLine(FILE* file, char* buffer, int size, int* endOfFile);

int parseLine(char* buffer, int size, i2cRipCmdStruct_t *i2cRipData);

static void i2cRipExit(int val);

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
	exit(1);
}

static int check_funcs(int file)
{
	unsigned long funcs;

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

static int i2cRead(int file, int reg, int dreg, int dregSize, __u8 *data, int dataSize){

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

	int nmsgs_sent = ioctl(file, I2C_RDWR, &rdwr);

	if (nmsgs_sent < 0) {
		fprintf(stderr, "Error: Sending messages failed: %s\n", strerror(errno));
		return 0;
	} else if (nmsgs_sent < 2) {
		fprintf(stderr, "Warning: only %d/2 messages were sent\n", nmsgs_sent);
		return 0;
	}

	return 1;
}

static int i2cWrite(int file, int reg, int dreg, int dregSize, __u8 *data, int dataSize){

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

	int nmsgs_sent = ioctl(file, I2C_RDWR, &rdwr);

	if (nmsgs_sent < 0) {
		fprintf(stderr, "Error: Sending messages failed: %s\n", strerror(errno));
		return 0;
	} else if (nmsgs_sent < 1) {
		fprintf(stderr, "Warning: only %d/1 messages were sent\n", nmsgs_sent);
		return 0;
	}

	return 1;
}

int inputFileParser(const char* filename);

int main(int argc, char *argv[]){
	char* end;
	int read = 0;
	int verify = 0;
	int write = 0;
	int yes = 0;
	int file;
	char filename[20];

	const char *maskp = NULL;
	int vmask = 0;
	int all_addrs = 0;
	int opt;
	int force = 1;


	if(!inputFileParser("./tableExample.txt")){
		fprintf(stderr,"FAILED PARSING FILE\nEXITING\n");
		EXIT(0);
	}
	EXIT(0);

	/* handle (optional) flags first */
	while ((opt = getopt(argc, argv, "wrvmyh:")) != -1) {
		switch (opt) {
			case 'w': write = 1; break;
			case 'r': read = 1; break;
			case 'v': verify = 1; break;
			case 'm': maskp = optarg; break;
			case 'y': yes = 1; break;
			case 'h':
			case '?':
				help();
				exit(opt == '?');
		}
	}

	int i2cbus, address, daddress;
	int value = 0x00;

	if (argc < optind + 3)
		help();

	i2cbus = lookup_i2c_bus(argv[optind]);
	if (i2cbus < 0)
		help();

	address = parse_i2c_address(argv[optind+1], all_addrs);
	if (address < 0)
		help();

	daddress = strtol(argv[optind+2], &end, 0);
	if (*end || daddress < 0 || daddress > 0xff) {
		fprintf(stderr, "Error: Data address invalid!\n");
		help();
	}
	
	value = 0;
	if(write || verify){
		if (argc <= optind + 3) {
			fprintf(stderr, "Error: No value to write!\n");
			help();
		}
		value = strtol(argv[optind+3], &end, 0);
		if (*end || value < 0 || value > 0xff) {
			fprintf(stderr, "Error: Data invalid!\n");
			help();
		}
	}

	if (maskp) {
		vmask = strtol(maskp, &end, 0);
		if (*end || vmask == 0) {
			fprintf(stderr, "Error: Data value mask invalid!\n");
			help();
		}
		// TODO 8bit vs 16 bit mask
		if (vmask > 0xff) {
			fprintf(stderr, "Error: Data value mask out of range!\n");
			help();
		}
	}

	// Open i2c port
	file = open_i2c_dev(i2cbus, filename, sizeof(filename), 0);
	if (file < 0 ){
		exit(1);
	}

	if(check_funcs(file)){
		exit(1);
	}

	if(set_slave_addr(file, address, force)){
		exit(1);
	}

	if (!yes && !confirm())
		exit(0);

	if(write || verify){
		if(!i2cWrite(file, address, daddress, 1, (__u8*)&value, 1)){
			fprintf(stderr, "Error: Failed to write Register\n");
			close(file);
			exit(1);
		}
	}

	unsigned char readValue;
	if (read || verify) {
		if(!i2cRead(file, address, daddress, 1, (__u8*)&readValue, 1)){
			fprintf(stderr, "Error: Failed to read Register\n");
			close(file);
			exit(1);
		}

		printf("0x%x\n", readValue);
	}

	if(verify){
		if(readValue == value){
			fprintf(stderr, "Verify Passed\n");
		}
		else{
			fprintf(stderr, "Verify Failed\n");
		}
	}
	//setupRipper()
	close(file);
	exit(0);
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
	g_i2cRipCmdListLength = lines;

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

	// for(int i = 0; i < lines; lines++){
	// 	if(!getLine(file, buffer, 100, &endOfFile)){
	// 		fprintf(stderr, "Buffer too small, line number: %d\n", lines);
	// 		return 0;
	// 	}
	// 	if(endOfFile){
	// 		break;
	// 	}
	// }

	
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
void i2cRipExit(int val){
	if(g_i2cRipCmdListLength > 0){
		free(g_i2cRipCmdList);
	}
	
	exit(val);
}