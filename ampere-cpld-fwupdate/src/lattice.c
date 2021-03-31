#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <termios.h>

#include <sys/mman.h>
#include "lattice.h"
#include "ast-jtag.h"

extern struct cpld_dev_info *cur_dev;
extern int debug;

/*************************************************************************************/

int jed_file_get_cfg_bitsize(FILE *jed_fd)
{
	//file
	char tmp_buf[160];
	char *ptr;
	int bitsize = 0;

	fseek(jed_fd, 0, SEEK_SET);
	//Header paser
	while (fgets(tmp_buf, 120, jed_fd) != NULL) {
		ptr = strstr(tmp_buf, "END CONFIG DATA");
		if (ptr != NULL) {
			// Get Config Data size "Lxxxxxx"
			fgets(tmp_buf, 120, jed_fd);
			sscanf(tmp_buf,"L%06d*",&bitsize);
			break;
		}
	}
	printf("CFG DATA bit size: %d\n", bitsize);
	return bitsize;
}

u32 jed_file_get_usercode(FILE *jed_fd)
{
	//file
	char tmp_buf[160];
	char *ptr;
	u32 userdata = 0;

	//Header paser
	fseek(jed_fd, 0, SEEK_SET);
	while (fgets(tmp_buf, 120, jed_fd) != NULL) {
		ptr = strstr(tmp_buf, "User Electronic Signature");
		if (ptr != NULL) {
			// Get Config Data size "Lxxxxxx"
			fgets(tmp_buf, 120, jed_fd);
			sscanf(tmp_buf,"UH%08X*",&userdata);
			break;
		}
	}
	printf("USER DATA is: 0x%08X\n", userdata);
	return userdata;
}

void jed_file_paser_header(FILE *jed_fd)
{
	//file
	char tmp_buf[160];

	//Header paser
	while (fgets(tmp_buf, 120, jed_fd) != NULL) {
//		if (debug) printf("%s \n", tmp_buf);
		if (tmp_buf[0] == 0x4C) { // "L"
			break;
		}
	}
}

void jed_file_paser(FILE *jed_fd, unsigned int len, u32 *dr_data)
{
	int i = 0;
	unsigned char input_char, input_bit;
	int sdr_array = 0, data_bit = 0, bit_cnt = 0;
	int err_flag;

	if (debug) printf("file len = %d \n", len);
	//Jed row
	for (i = 0; i < len; i++) {
		input_char = fgetc(jed_fd);
		if ((input_char == 0x30) || (input_char == 0x31)) { // "0", "1"
//			printf("%c", input_char);
			if (input_char == 0x30) {
				input_bit = 0;
			} else {
				input_bit = 1;
			}
//			if (debug) printf(" %d ", input_bit);
			dr_data[sdr_array] |= (((u32) input_bit) << data_bit);
//			if (debug) printf("dr_data[%d]=%llx, data_bit=%d\n", sdr_array, dr_data[sdr_array], data_bit);
			data_bit++;
			bit_cnt++;

			if ((data_bit % 32) == 0) {
				if (debug) printf(" [%i] : %x \n", sdr_array, dr_data[sdr_array]);
				data_bit = 0;
				sdr_array++;
			}
			err_flag = 0;
		} else if (input_char == 0xd) {
			//printf(" ");
			i--;
//			printf("paser error [%x]\n", input_char);
		} else if (input_char == 0xa) {
			i--;
			//printf("\n");
//			printf("paser error [%x]\n", input_char);
		} else {
			printf("paser errorxx [%x : %c] \n", input_char, input_char);
			printf("%c", input_char);
			err_flag = 1;
			break;
		}
	}
	if (debug) printf(" [%i] : %llx , Total %d \n", sdr_array, dr_data[sdr_array], bit_cnt);
	
	if (bit_cnt != len) {
		if(err_flag) {
			printf("\n");
		} else {
			printf("File Error - bit_cnt %d, len %d\n", bit_cnt, len);
		}
	}
//	} while (input_char != 0x2A); // "*"

}

/*
 * bitStr2hex_fsm
 */
static unsigned int bitStr2hex_fsm(unsigned int cmd){
	static unsigned int data =0;
	static unsigned int bits =0;
	unsigned int ret = 0;

	switch(cmd){
	case BITS_FSM_BITS:
		ret = bits;
		break;
	case BITS_FSM_RESET:
		data = 0;
		bits = 0;
		break;
	case BITS_FSM_PUSH_HighBIT:
		if(bits < 32){
			data |= 1 << bits;
			bits++;
		}
		break;
	case BITS_FSM_PUSH_ZeroBIT:
		if(bits < 32){
			bits++;
		}
		break;
	case BITS_FSM_READ:
		ret = data;
		break;
	case '\n':
	case '\r':
		break;
	default://'*', '\0' or others.
		ret = -1;
		break;
	}

	return ret;
}

static unsigned int pick_bits(FILE *jed_fd, u32 *hex_buf,int range){

	unsigned int index=0;
	unsigned int ret=0;
	unsigned char input_char;

	bitStr2hex_fsm( BITS_FSM_RESET );
	//read until char is end(* or '\0')
	input_char = fgetc(jed_fd);
//	printf("CFG data is: \n");
	while(bitStr2hex_fsm(input_char) != -1 && range-- >0){
		if(bitStr2hex_fsm( BITS_FSM_BITS ) == 32) {
			hex_buf[index++] = bitStr2hex_fsm(BITS_FSM_READ);
//			printf(" %x ", bitStr2hex_fsm(BITS_FSM_READ));
//			if (index % 10 == 0)
//				printf("\n");
			bitStr2hex_fsm(BITS_FSM_RESET);
		}
		input_char = fgetc(jed_fd);
	}

	ret=index << 2;
	if(bitStr2hex_fsm( BITS_FSM_BITS ) != 0) {
		hex_buf[index++] = bitStr2hex_fsm(BITS_FSM_READ);
		ret += bitStr2hex_fsm( BITS_FSM_BITS ) >> 3;
		ret |= (bitStr2hex_fsm( BITS_FSM_BITS ) % 8) << 24;  //move the redundant bits in the MSB of "ret"
		bitStr2hex_fsm(BITS_FSM_RESET);
	}

	return ret;
}

static u8 *jed_ami(FILE *jed_fd)
{
	int i, total_bitsize;
	u8 *buff, *ptr;
	u8 redunant_bits;
	u32 shift, crc, len;

	crc = 0;
	total_bitsize = jed_file_get_cfg_bitsize(jed_fd);

	buff = malloc((total_bitsize/8 + 1) * sizeof(u8));
	memset(buff, 0, (total_bitsize/8 + 1) * sizeof(u8));

	fseek(jed_fd, 0, SEEK_SET);
	jed_file_paser_header(jed_fd);
	shift = 0;
	shift  += pick_bits(jed_fd, (u32 *) buff, 835328);
	redunant_bits= (unsigned char)(shift >> 24);
//	printf("shift=%d, redunt=%d\n", shift, redunant_bits);
	shift &= 0x00ffffff;
	len = shift*8 + redunant_bits;
//	printf("###CFG data size is: %d\n", len);
	len = len / 8;
	ptr = buff;
//	printf("CRC data is: \n");
	for (i = 0; i < len; i++) {
		crc += *ptr++;
//		if (i %128 == 0)
//			printf("0x%x\n", crc);
	}
	crc &= 0xFFFF;
	printf("###Checksum count 0x%x\n", crc);

	return buff;
}

int llcmxo2_4000hc_cpld_program(FILE *jed_fd)
{
	int i, index;
	u32 dr_data, user_data;
	u32 ir_tdi_data;
	u32 ir_tdo_data;
	int total_bitsize;
	u8 *ptr_data;

	unsigned int row  = 0;
	index = 0;

	lcmxo2_4000hc_cpld_erase();

	total_bitsize = jed_file_get_cfg_bitsize(jed_fd);
	user_data = jed_file_get_usercode(jed_fd);

	cur_dev->row_num = total_bitsize / cur_dev->dr_bits;
	printf("cur_dev->row_num is %d\n", cur_dev->row_num);

	ptr_data = jed_ami(jed_fd);

	//! Program CFG

	printf("Program CFG \n");

	//! Shift in LSC_INIT_ADDRESS(0x46) instruction
	//SIR 8	TDI  (46);
	ir_tdi_data = 0x46;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 8	TDI  (04);
	//RUNTEST IDLE	2 TCK	1.00E-002 SEC;
	dr_data = 0x04;
	ast_jtag_tdi_xfer(0, 8, &dr_data);

	printf("Program 9212 .. \n");

//	system("echo 1 > /sys/class/gpio/gpio10/value");

//	mode = SW_MODE;
	for (row = 0 ; row < cur_dev->row_num; row++) {
		//! Shift in LSC_PROG_INCR_NV(0x70) instruction
		//SIR 8 TDI  (70);
		ir_tdi_data = 0x70;
		ast_jtag_sir_xfer(1, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);


		//! Shift in Data Row = 1
		//SDR 128 TDI  (120600000040000000DCFFFFCDBDFFFF);
		//RUNTEST IDLE	2 TCK;
		ast_jtag_tdi_xfer(0, cur_dev->dr_bits, (u32 *) &ptr_data[index]);
		usleep(1000);

		//! Shift in LSC_CHECK_BUSY(0xF0) instruction
		//SIR 8 TDI  (F0);
		ir_tdi_data = 0xF0;
		ast_jtag_sir_xfer(1, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

		//LOOP 10 ;
		//RUNTEST IDLE	1.00E-003 SEC;
		//SDR 1 TDI  (0)
		//		TDO  (0);
		//ENDLOOP ;
		for (i = 0; i < 10; i++) {
			usleep(3000);
			dr_data = 0;
			ast_jtag_tdo_xfer(0, 1, &dr_data);
			if (dr_data == 0) break;
		}

		if (dr_data != 0)
			printf("row %d, Fail [%d] \n", row, dr_data);
		else
			printf(".");
		index += cur_dev->dr_bits / 32;

	}
//	mode = HW_MODE;
	printf("\nDone\n");
#if 0
	//! Program the UFM
	printf("Program the UFM : 2048\n");

	//! Shift in LSC_INIT_ADDR_UFM(0x47) instruction
	//SIR 8	TDI  (47);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0x47;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);

	for (row = 0 ; row < 2048; row++) {
		memset(dr_data, 0, (cur_dev->dr_bits / 32) * sizeof(unsigned int));
		jed_file_paser(jed_fd, cur_dev->dr_bits, dr_data);

		//! Shift in LSC_PROG_INCR_NV(0x70) instruction
		//SIR 8	TDI  (70);
		ir_tdi_data = 0x70;
		ast_jtag_sir_xfer(1, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);

		//! Shift in Data Row = 1
		//SDR 128 TDI  (00000000000000000000000000000000);
		//RUNTEST IDLE	2 TCK;
		ast_jtag_tdi_xfer(0, cur_dev->dr_bits, dr_data);

		//! Shift in LSC_CHECK_BUSY(0xF0) instruction
		//SIR 8	TDI  (F0);
		ir_tdi_data = 0xF0;
		ast_jtag_sir_xfer(1, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);

		//LOOP 10 ;
		//RUNTEST IDLE	1.00E-003 SEC;
		//SDR 1	TDI  (0)
		//		TDO  (0);
		//ENDLOOP ;
		for (i = 0; i < 10; i++) {
			usleep(3000);
			dr_data[0] = 0;
			ast_jtag_tdo_xfer(0, 1, dr_data);
			if (dr_data[0] == 0) break;
		}

		if (dr_data[0] != 0)
			printf("row %d, Fail [%d] \n", row, dr_data[0]);
		else
			printf(".");

	}
	printf("\nDone\n");
#endif
	//! Program USERCODE

	//! Shift in READ USERCODE(0xC0) instruction
	//SIR 8	TDI  (C0);
	ir_tdi_data = 0xC0;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 32	TDI  (00000000);
	ast_jtag_tdi_xfer(0, 32, &user_data);

	//! Shift in ISC PROGRAM USERCODE(0xC2) instruction
	//SIR 8	TDI  (C2);
	//RUNTEST IDLE	2 TCK	1.00E-002 SEC;
	ir_tdi_data = 0xC2;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
	usleep(2000);

	//! Read the status bit

	//! Shift in LSC_READ_STATUS(0x3C) instruction
	//SIR 8	TDI  (3C);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0x3C;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 32	TDI  (00000000)
	//		TDO  (00000000)
	//		MASK (00003000);
	dr_data = 0x00000000;
	ast_jtag_tdo_xfer(0, 32, &dr_data);
	if(dr_data != 0x0) printf("Read the status error %x \n", dr_data);
	printf("Prgram usercode status: 0x%x\n", dr_data & 0x00003000);

#if 0
	//! Program Feature Rows

	//! Shift in LSC_INIT_ADDRESS(0x46) instruction
	//SIR 8	TDI  (46);
	ir_tdi_data = 0x46;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);

	//SDR 8	TDI  (02);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	dr_data[0] = 0x02;
	ast_jtag_tdi_xfer(0, 8, dr_data);
	usleep(3000);

	//! Shift in LSC_PROG_FEATURE(0xE4) instruction
	//SIR 8	TDI  (E4);
	ir_tdi_data = 0xE4;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);

	//SDR 64	TDI  (0000000000000000);
	//RUNTEST IDLE	2 TCK;
	dr_data[0] = 0x00000000;
	dr_data[1] = 0x00000000;
	ast_jtag_tdi_xfer(0, 64, dr_data);


	//! Shift in LSC_CHECK_BUSY(0xF0) instruction
	//SIR 8	TDI  (F0);
	ir_tdi_data = 0xF0;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);

	//LOOP 10 ;
	//RUNTEST IDLE	1.00E-003 SEC;
	//SDR 1	TDI  (0)
	//		TDO  (0);
	//ENDLOOP ;
	for (i = 0; i < 10; i++) {
		usleep(3000);
		dr_data[0] = 0;
		ast_jtag_tdo_xfer(0, 1, dr_data);
	}

	//! Shift in LSC_READ_FEATURE (0xE7) instruction
	//SIR 8	TDI  (E7);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0xE7;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);

	//SDR 64	TDI  (0000000000000000)
	//		TDO  (0000000000000000);
	dr_data[0] = 0x00000000;
	dr_data[1] = 0x00000000;
	ast_jtag_tdo_xfer(0, 64, dr_data);
//	if((dr_data[0] != 0x0) || (dr_data[1] != 0x0)) printf(" %x %x [0x0, 0x0] \n", dr_data[0], dr_data[1]);

	//! Shift in in LSC_PROG_FEABITS(0xF8) instruction
	//SIR 8	TDI  (F8);
	ir_tdi_data = 0xF8;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);

	//SDR 16	TDI  (0620);
	//RUNTEST IDLE	2 TCK;
	dr_data[0] = 0x0620;
	ast_jtag_tdi_xfer(0, 16, dr_data);

	//! Shift in LSC_CHECK_BUSY(0xF0) instruction
	//SIR 8	TDI  (F0);
	ir_tdi_data = 0xF0;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);

	//LOOP 10 ;
	//RUNTEST IDLE	1.00E-003 SEC;
	//SDR 1	TDI  (0)
	//		TDO  (0);
	//ENDLOOP ;
	for (i = 0; i < 10; i++) {
		usleep(3000);
		dr_data[0] = 0;
		ast_jtag_tdo_xfer(0, 1, dr_data);
	}

	//! Shift in in LSC_READ_FEABITS(0xFB) instruction
	//SIR 8	TDI  (FB);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0xFB;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);

	//SDR 16	TDI  (0000)
	//		TDO  (0620)
	//		MASK (FFF2);
	dr_data[0] = 0x0;
	ast_jtag_tdo_xfer(0, 16, dr_data);
//	if(dr_data[0] != 0x0620) printf("%04x [0620]\n", dr_data[0] & 0xfff2);

#endif
	//! Program DONE bit

	//! Shift in ISC PROGRAM DONE(0x5E) instruction
	//SIR 8	TDI  (5E);
	//RUNTEST IDLE	2 TCK;
	ir_tdi_data = 0x5E;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//! Shift in LSC_CHECK_BUSY(0xF0) instruction
	//SIR 8	TDI  (F0);
	ir_tdi_data = 0xF0;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//LOOP 10 ;
	//RUNTEST IDLE	1.00E-003 SEC;
	//SDR 1	TDI  (0)
	//		TDO  (0);
	//ENDLOOP ;
	for (i = 0; i < 10; i++) {
		usleep(3000);
		dr_data = 0;
		ast_jtag_tdo_xfer(0, 1, &dr_data);
	}

	//! Shift in BYPASS(0xFF) instruction
	//SIR 8	TDI  (FF)
	//		TDO  (04)
	//		MASK (C4);
	ir_tdi_data = 0xFF;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//! Exit the programming mode

	//! Shift in ISC DISABLE(0x26) instruction
	//SIR 8	TDI  (26);
	//RUNTEST IDLE	2 TCK	1.00E+000 SEC;
	ir_tdi_data = 0x26;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//! Shift in BYPASS(0xFF) instruction
	//SIR 8	TDI  (FF);
	//RUNTEST IDLE	2 TCK	1.00E-001 SEC;
	ir_tdi_data = 0xFF;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	free(ptr_data);

	return 0;

}

int lcmxo2_4000hc_cpld_verify(FILE *jed_fd)
{
	int i, index;
	u32 data = 0;
	u8 *jed_data,* read_data;
	u32 dr_data;
	u32 ir_tdi_data;
	u32 ir_tdo_data;
	unsigned int row  = 0;
	int cmp_err = 0;
	u32 *sdr_data;
	int sdr_array;
	u16 crc_jed = 0;
	u16 crc_data = 0;
	u8 *ptr_jed, *ptr_data;
	u32 total_bitsize, user_data;

	//RUNTEST	IDLE	15 TCK	1.00E-003 SEC;
	ast_jtag_run_test_idle(0, 0, 3);
	usleep(3000);

	//! Check the IDCODE_PUB
	//SIR	8	TDI  (E0);
	ir_tdi_data = IDCODE_PUB;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 32	TDI  (00000000)
	//		TDO  (012B5043)
	//		MASK (FFFFFFFF);
	ast_jtag_tdo_xfer(0, 32, &dr_data);

	if (dr_data != 0x12BC043) {
		printf("ID Fail : %08x [0x012B5043] \n", dr_data);
		return -1;
	}
#if 0
	//! Program Bscan register

	//! Shift in Preload(0x1C) instruction
	//SIR	8	TDI  (1C);
	ir_tdi_data = 0x1C;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR	424 TDI (FFFF...FFF);
	sdr_array = 424 / 64 + 1;
	sdr_data = malloc(sdr_array * sizeof(u64));
	memset(sdr_data, 0xff, sdr_array * sizeof(u64));
	ast_jtag_tdi_xfer(0, 424, sdr_data);
	free(sdr_data);

	//! Enable the Flash
	//! Shift in ISC ENABLE(0xC6) instruction
	//SIR 8	TDI  (C6);
	ir_tdi_data = 0xC6;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 8	TDI  (00);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	data = 0x00;
	ast_jtag_tdi_xfer(0, 8, &data);
	usleep(3000);

	//! Shift in ISC ERASE(0x0E) instruction
	//SIR 8	TDI  (0E);
	ir_tdi_data = 0x0E;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 8	TDI  (01);
	//RUNTEST IDLE	2 TCK	1.00E+000 SEC;
	data = 0x01;
	ast_jtag_tdi_xfer(0, 8, &data);
	usleep(1000);

	//! Shift in BYPASS(0xFF) instruction
	//SIR 8	TDI  (FF)
	//		TDO  (00)
	//		MASK (C0);
	ir_tdi_data = BYPASS;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
//	printf("Bypass : %02x [0x00] , ",tdo & 0xff);

	//! Shift in ISC ENABLE(0xC6) instruction
	//SIR 8	TDI  (C6);
	ir_tdi_data = 0xC6;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 8	TDI  (08);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	data = 0x08;
	ast_jtag_tdi_xfer(0, 8, &data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	usleep(3000);
#endif
	//! Verify the Flash
	printf("Starting to Verify Device . . . This will take a few seconds\n");

	//! Shift in LSC_INIT_ADDRESS(0x46) instruction
	//SIR 8	TDI  (46);
	ir_tdi_data = 0x46;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 8	TDI  (04);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	data = 0x04;
	ast_jtag_tdi_xfer(0, 8, &data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	jtag_runtest_idle(2,1);
//	usleep(3000);

	//! Shift in LSC_READ_INCR_NV(0x73) instruction
	//SIR 8	TDI  (73);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0x73;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);
//	usleep(3000);
	jtag_runtest_idle(2,1);

	total_bitsize = jed_file_get_cfg_bitsize(jed_fd);
	user_data = jed_file_get_usercode(jed_fd);

	cur_dev->row_num = total_bitsize / cur_dev->dr_bits;
	jed_data = jed_ami(jed_fd);
	read_data = malloc((total_bitsize/8 + 1) * sizeof(u8));
	memset(read_data, 0, (total_bitsize/8 + 1) * sizeof(u8));

	printf("Verify CONFIG 9192 \n");
	cmp_err = 0;
	row = 0;
	index = 0;

	for (row = 0 ; row < cur_dev->row_num; row++) {
//		printf("%d \n", row);
		ast_jtag_tdo_xfer(0, cur_dev->dr_bits, (u32 *) &read_data[index]);

		for (i = 0; i < (cur_dev->dr_bits / 32); i++) {
			if (read_data[i] != jed_data[i]) {
				printf("JED : %x, SDR : %x \n", jed_data[i], read_data[i]);
				cmp_err = 1;
			}
		}

		//RUNTEST	IDLE	2 TCK	1.00E-003 SEC;
//		ast_jtag_run_test_idle( 0, 0, 2);
//		usleep(3000);
		jtag_runtest_idle(2,1);
//		printf("\n");
//		if (cmp_err) {
//			goto cmp_error;
//			break;
//		}
		index += cur_dev->dr_bits / 32;
	}

#if 0
	//! Verify the UFM


	//! Shift in LSC_INIT_ADDR_UFM(0x47) instruction
	//SIR	8	TDI  (47);
	//RUNTEST	IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0x47;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	usleep(3000);


	//! Shift in LSC_READ_INCR_NV(0x73) instruction
	//SIR	8	TDI  (73);
	//RUNTEST	IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0x73;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	usleep(3000);

	printf("Verify the UFM 2048 \n");
	//! Shift out Data Row
	for (row = 0 ; row < 2048; row++) {
//		printf("%d \n", row);
		memset(dr_data, 0, (cur_dev->dr_bits / 32) * sizeof(unsigned int));
		memset(jed_data, 0, (cur_dev->dr_bits / 32) * sizeof(unsigned int));
		jed_file_paser(jed_fd, cur_dev->dr_bits, jed_data);
		ast_jtag_tdo_xfer(0, cur_dev->dr_bits, dr_data);

		for (i = 0; i < (cur_dev->dr_bits / 32); i++) {
			if (dr_data[i] != jed_data[i]) {
				printf("JED : %x, SDR : %x \n", jed_data[i], dr_data[i]);
				cmp_err = 1;
			}
		}
		//RUNTEST	IDLE	2 TCK	1.00E-003 SEC;
//		ast_jtag_run_test_idle( 0, 0, 2);
		usleep(3000);
//		printf("\n");
		if (cmp_err) {
			break;
		}
	}
#endif

	//! Verify USERCODE
	printf("Verify USERCODE \n");
	//! Shift in READ USERCODE(0xC0) instruction
	//SIR 8	TDI  (C0);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0xC0;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	usleep(3000);

	//SDR 32	TDI  (00000000)
	//		TDO  (00000000)
	//		MASK (FFFFFFFF);
	dr_data = 0x00000000;
	ast_jtag_tdo_xfer(0, 32, &dr_data);

	//! Read the status bit

	//! Shift in LSC_READ_STATUS(0x3C) instruction
	//SIR 8	TDI  (3C);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0x3C;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	usleep(3000);

	//SDR 32	TDI  (00000000)
	//		TDO  (00000000)
	//		MASK (00003000);
	dr_data = 0x00000000;
	ast_jtag_tdo_xfer(0, 32, &dr_data);

#if 0
	//! Verify Feature Rows

	//! Shift in LSC_READ_STATUS(0x3C) instruction
	//SIR 8	TDI  (3C);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0x3C;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	usleep(3000);

	//SDR 32	TDI  (00000000)
	//		TDO  (00000000)
	//		MASK (00010000);
	dr_data[0] = 0x00000000;
	ast_jtag_tdo_xfer(0, 32, dr_data);

	//! Shift in LSC_READ_FEATURE (0xE7) instruction
	//SIR 8	TDI  (E7);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0xE7;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);

	usleep(3000);

	//SDR 64	TDI  (0000000000000000)
	//		TDO  (0000000000000000);
	dr_data[0] = 0x00000000;
	dr_data[1] = 0x00000000;
	ast_jtag_tdo_xfer(0, 64, dr_data);

	//! Shift in in LSC_READ_FEABITS(0xFB) instruction
	//SIR 8	TDI  (FB);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0xFB;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	usleep(3000);

	//SDR 16	TDI  (0000)
	//		TDO  (0620)
	//		MASK (FFF2);
	dr_data[0] = 0x00000000;
	ast_jtag_tdo_xfer(0, 16, dr_data);
	printf("read %x [0x0620] \n", dr_data[0] & 0xffff);

	//! Read the status bit

	//! Shift in LSC_READ_STATUS(0x3C) instruction
	//SIR 8	TDI  (3C);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0x3C;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, ir_tdi_data, ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	usleep(3000);

	//SDR 32	TDI  (00000000)
	//		TDO  (00000000)
	//		MASK (00003000);
	dr_data[0] = 0x00000000;
	ast_jtag_tdo_xfer(0, 32, dr_data);
#endif
	//! Verify Done Bit

	//! Shift in BYPASS(0xFF) instruction
	//SIR 8	TDI  (FF)
	//		TDO  (04)
	//		MASK (C4);
	ir_tdi_data = BYPASS;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
	if((ir_tdo_data & 0xff) != 0x04)
		printf("BYPASS error %x \n", ir_tdo_data & 0xff);

	//! Exit the programming mode

	//! Shift in ISC DISABLE(0x26) instruction
	//SIR 8	TDI  (26);
	//RUNTEST IDLE	2 TCK	1.00E+000 SEC;
	ir_tdi_data = 0x26;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	usleep(1000);

	//! Shift in BYPASS(0xFF) instruction
	//SIR 8	TDI  (FF);
	//RUNTEST IDLE	2 TCK	1.00E-001 SEC;
	ir_tdi_data = BYPASS;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
//	ast_jtag_run_test_idle( 0, 0, 2);
	usleep(1000);

	//! Verify SRAM DONE Bit

	//! Shift in BYPASS(0xFF) instruction
	//SIR 8	TDI  (FF)
	//		TDO  (04)
	//		MASK (84);
	ir_tdi_data = BYPASS;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
	if((ir_tdo_data & 0xff) != 0x04)
		printf("BYPASS error %x \n", ir_tdo_data & 0xff);


cmp_error:
	free(jed_data);
	free(read_data);
	if (cmp_err)
		printf("Verify Error !!\n");
	else
		printf("Verify Done !!\n");

	return 0;

}

int lcmxo2_4000hc_cpld_erase(void)
{
	int i = 0;
	u32 ir_tdi_data;
	u32 ir_tdo_data;
	u32 data = 0;
	int sdr_array;
	u32 *sdr_data;

	//! Check the IDCODE_PUB
	//SIR   8   TDI  (E0);
	ir_tdi_data = IDCODE_PUB;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 32    TDI  (00000000)
	//      TDO  (01285043)
	//      MASK (FFFFFFFF);
	data = 0x00000000;
	ast_jtag_tdo_xfer(0, 32, &data);

	if (data != 0x12BC043) {
		printf("ID Fail : %08x [0x012B5043] \n", data);
		return -1;
	}
#if 0
	//! Program Bscan register

	//! Shift in Preload(0x1C) instruction
	//SIR	8	TDI  (1C);
	ir_tdi_data = 0x1C;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR	424 TDI (FFFF...FFF);
	sdr_array = 424 / 64 + 1;
	sdr_data = malloc(sdr_array * sizeof(u64));
	memset(sdr_data, 0xff, sdr_array * sizeof(u64));
	ast_jtag_tdi_xfer(0, 424, sdr_data);
	free(sdr_data);

	//! Enable the Flash

	//! Shift in ISC ENABLE(0xC6) instruction
	//SIR 8 TDI  (C6);
	ir_tdi_data = 0xC6;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 8 TDI  (00);
	//RUNTEST IDLE  2 TCK   1.00E-003 SEC;
	data = 0x00;
	ast_jtag_tdi_xfer(0, 8, &data);
	usleep(3000);

	//! Shift in ISC ERASE(0x0E) instruction
	//SIR 8 TDI  (0E);
	ir_tdi_data = 0x0E;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 8 TDI  (04);
	//RUNTEST IDLE  2 TCK   1.00E+000 SEC;
	data = 0x01;
	ast_jtag_tdi_xfer(0, 8, &data);
	usleep(1000);

	//! Shift in BYPASS(0xFF) instruction
	//SIR 8 TDI  (FF)
	//      TDO  (00)
	//      MASK (C0);
	ir_tdi_data = BYPASS;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//! Shift in ISC ENABLE(0xC6) instruction
	//SIR 8 TDI  (C6);
	ir_tdi_data = 0xC6;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 8 TDI  (08);
	//RUNTEST IDLE  2 TCK   1.00E-003 SEC;
	data = 0x08;
	ast_jtag_tdi_xfer(0, 8, &data);
	usleep(3000);

	//! Check the Key Protection fuses

	//! Shift in LSC_READ_STATUS(0x3C) instruction
	//SIR 8 TDI  (3C);
	//RUNTEST IDLE  2 TCK   1.00E-003 SEC;
	ir_tdi_data = 0x3C;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
	usleep(3000);

	//SDR 32    TDI  (00000000)
	//      TDO  (00000000)
	//      MASK (00024040);
	data = 0x00000000;
	ast_jtag_tdo_xfer(0, 32, &data);

	//! Shift in LSC_READ_STATUS(0x3C) instruction
	//SIR 8 TDI  (3C);
	//RUNTEST IDLE  2 TCK   1.00E-003 SEC;
	ir_tdi_data = 0x3C;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
	usleep(3000);

	//SDR 32    TDI  (00000000)
	//      TDO  (00000000)
	//      MASK (00010000);
	data = 0x00000000;
	ast_jtag_tdo_xfer(0, 32, &data);

#endif
	//-----------------------------------------------
	//    Erase the Flash

	//! Shift in ISC ERASE(0x0E) instruction
	//SIR 8	TDI  (0E);
	ir_tdi_data = 0x0E;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//SDR 8	TDI  (04);
	//RUNTEST IDLE	2 TCK	1.00E+000 SEC;
	data = 0x04;
	ast_jtag_tdi_xfer(0, 8, &data);
	usleep(1000);

	//! Shift in LSC_CHECK_BUSY(0xF0) instruction
	//SIR 8	TDI  (F0);
	ir_tdi_data = 0xF0;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	//LOOP 350 ;
	//RUNTEST IDLE	2 TCK	1.00E-002 SEC;
	//SDR 1	TDI  (0)
	//		TDO  (0);
	//ENDLOOP ;
	printf("LOOP 350  \n");
	data = 0;
	for (i = 0; i < 350 ; i++) {
//		printf("loop count %d \n",i);
//		ast_jtag_run_test_idle( 0, 0, 2);
		usleep(2000);
		ast_jtag_tdo_xfer(0, 1, &data);
		if (data == 0) {
			printf("Loop i=%d, exit busy\n", i);
			break;
		}
	}

	//! Read the status bit

	//! Shift in LSC_READ_STATUS(0x3C) instruction
	//SIR 8	TDI  (3C);
	//RUNTEST IDLE	2 TCK	1.00E-003 SEC;
	ir_tdi_data = 0x3C;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
	usleep(3000);

	//SDR 32	TDI  (00000000)
	//		TDO  (00000000)
	//		MASK (00003000);
	data = 0x00000000;
	ast_jtag_tdo_xfer(0, 32, &data);
	printf("Erase Status: 0x%x\n", data & 0x00003000);

	//! Exit the programming mode

	//! Shift in ISC DISABLE(0x26) instruction
	//SIR 8	TDI  (26);
	//RUNTEST IDLE	2 TCK	1.00E+000 SEC;
	ir_tdi_data = 0x26;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);
	usleep(3000);

	//! Shift in BYPASS(0xFF) instruction
	//SIR 8	TDI  (FF);
	//RUNTEST IDLE	2 TCK	1.00E-001 SEC;
	ir_tdi_data = BYPASS;
	ast_jtag_sir_xfer(0, LATTICE_INS_LENGTH, &ir_tdi_data, &ir_tdo_data);

	usleep(1000);

	printf("Erase Done \n");

	return 0;

}

