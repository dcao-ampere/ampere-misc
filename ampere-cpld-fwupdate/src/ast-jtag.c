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

int jtag_fd;

/*************************************************************************************/
/*				AST JTAG LIB					*/
int ast_jtag_open(char *dev)
{
	jtag_fd = open(dev, O_RDWR);
	if (jtag_fd == -1) {
		perror("Can't open /dev/aspeed-jtag, please install driver!! \n");
		return -1;
	}
	return 0;
}

void ast_jtag_close(void)
{
	close(jtag_fd);
}

unsigned int ast_get_jtag_freq(void)
{
	int retval;
	unsigned int freq = 0;
	retval = ioctl(jtag_fd, JTAG_GIOCFREQ, &freq);
	if (retval == -1) {
		perror("ioctl JTAG get freq fail!\n");
		return 0;
	}

	return freq;
}

int ast_set_jtag_freq(unsigned int freq)
{
	int retval;
	retval = ioctl(jtag_fd, JTAG_SIOCFREQ, freq);
	if (retval == -1) {
		perror("ioctl JTAG set freq fail!\n");
		return -1;
	}

	return 0;
}

int ast_set_mode(unsigned int mode)
{
	int retval;
	struct jtag_mode j_mode;

	j_mode.feature = 0; /* JTAG feature setting selector for JTAG controller HW/SW */
	j_mode.mode = mode;
    
	retval = ioctl(jtag_fd, JTAG_SIOCMODE, &j_mode);
	if (retval == -1) {
		perror("ioctl JTAG set mode fail!\n");
		return -1;
	}

	return 0;
}

int ast_jtag_xfer(unsigned char type, unsigned char direct,
                  unsigned char end, unsigned int len, u32 *tdio)
{
	//write
	int retval;
	struct jtag_xfer xfer;

	xfer.type = type;
	xfer.direction = direct;
	xfer.length = len;
	xfer.tdio = *tdio;
//	printf("tdio1=%llx\n", *tdio);
//	printf("tdio2=%llx\n", xfer.tdio);
//	printf("sizeof struct jtag_xfer=%d\n", sizeof(struct jtag_xfer));
	if (end) {
		//pause
		if (type == JTAG_SIR_XFER)
			xfer.endstate = JTAG_STATE_PAUSEIR;
		else 
			xfer.endstate = JTAG_STATE_PAUSEDR;
	} else {
		//idle
		xfer.endstate = JTAG_STATE_IDLE;
	}

	retval = ioctl(jtag_fd, JTAG_IOCXFER, &xfer);
	if (retval == -1) {
		perror("ioctl JTAG data xfer fail!\n");
		return -1;
	}
	*tdio = (u32) xfer.tdio;

//	if(enddr)
//		usleep(3000);
	return 0;
}

int ast_jtag_sir_xfer(unsigned char endir, unsigned int len,
                               u32 *tdi, u32 *tdo)
{
	int 	retval;

	if (len > 32)
		return -1;


	retval = ast_jtag_xfer(JTAG_SIR_XFER, JTAG_WRITE_XFER, endir, len, tdi);
	if (retval == -1) {
		perror("ioctl JTAG sir fail!\n");
		return -1;
	}
//	if(endir)
//		usleep(3000);
	// return *sir.tdo;
	return 0;
}

int ast_jtag_tdi_xfer(unsigned char enddr, unsigned int len, u32 *tdio)
{
	//write
	int retval = 0;
	int count, i;
	unsigned int bit_len;

	count = len / 32 + 1;
	for (i = 0; i < count; i++) {
		if (i == (count - 1))
			bit_len = len % 32;
		else
			bit_len = 32;
		if (bit_len != 0)
			retval = ast_jtag_xfer(JTAG_SDR_XFER, JTAG_WRITE_XFER, enddr, bit_len, &tdio[i]);
		if (retval == -1) {
			perror("ioctl JTAG tdi fail!\n");
			return -1;
		}
	}

//	if(endir)
//		usleep(3000);
	// return *sir.tdo;
	return 0;
}

int ast_jtag_tdo_xfer(unsigned char enddr, unsigned int len, u32 *tdio)
{
	//read
	int retval;
	int count, i;
	unsigned int bit_len;

	count = len / 32 + 1;
	for (i = 0; i < count; i++) {
		if (i == (count - 1))
			bit_len = len % 32;
		else
			bit_len = 32;
		if (bit_len != 0)
			retval = ast_jtag_xfer(JTAG_SDR_XFER, JTAG_READ_XFER, enddr, bit_len, &tdio[i]);
		if (retval == -1) {
			perror("ioctl JTAG tdi fail!\n");
			return -1;
		}
	}

//	if(endir)
//		usleep(3000);
	// return *sir.tdo;
	return 0;
}

int ast_jtag_run_test_idle(unsigned char reset, unsigned char end, unsigned char tck)

{
#if 0
	int retval;
	struct runtest_idle run_idle;

	run_idle.mode = mode;
	run_idle.end = end;
	run_idle.reset = reset;
	run_idle.tck = tck;

	retval = ioctl(jtag_fd, ASPEED_JTAG_IOCRUNTEST, &run_idle);
	if (retval == -1) {
		perror("ioctl JTAG run reset fail!\n");
		return -1;
	}

//	if(end)
//		usleep(3000);
#endif
	return 0;
}

void jtag_runtest_idle(unsigned int tcks, unsigned int min_mSec)
{
	int i = 0;
	int retval;
	static unsigned int idle_count = 0;
	struct tck_bitbang tck_bitbang;

	tck_bitbang.tdi = 0;
	tck_bitbang.tms = 0;
	tck_bitbang.tdo = 0;
	for(i = 0; i< tcks; i++) {
		retval = ioctl(jtag_fd, JTAG_IOCBITBANG, &tck_bitbang);
		if (retval == -1) {
			perror("ioctl JTAG bitbang fail!\n");
			break;
		}
	}

	if (min_mSec != 0){
		usleep(min_mSec * 1000);
	}
	//msleep :: for kernel switch other task.
	if((idle_count ++ ) % 128 == 0){
		usleep(0);
 	}
}
