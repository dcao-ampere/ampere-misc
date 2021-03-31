#define	LATTICE_INS_LENGTH		0x08

#define	BYPASS					0xff

//MachXO2 Programming Commands
#define IDCODE                  0x16
#define IDCODE_PUB              0xE0
#define ISC_ENABLE_X            0x74
#define ISC_ENABLE              0xC6
#define LSC_CHECK_BUSY          0xF0
#define LSC_READ_STATUS         0x3C
#define ISC_ERASE               0x0E
#define LSC_ERASE_TAG           0xCB
#define LSC_INIT_ADDRESS        0x46
#define LSC_WRITE_ADDRESS       0xB4
#define LSC_PROG_INCR_NV        0x70
#define LSC_INIT_ADDR_UFM       0x47
#define LSC_PROG_TAG            0xC9
#define ISC_PROGRAM_USERCODE    0xC2
#define USERCODE                0xC0
#define LSC_PROG_FEATURE        0xE4
#define LSC_READ_FEATURE        0xE7
#define LSC_PROG_FEABITS        0xF8
#define LSC_READ_FEABITS        0xFB
#define LSC_READ_INCR_NV        0x73
#define LSC_READ_UFM            0xCA
#define ISC_PROGRAM_DONE        0x5E
#define LSC_PROG_OTP            0xF9
#define LSC_READ_OTP            0xFA
#define ISC_DISABLE             0x26
#define ISC_NOOP                0xFF
#define LSC_REFRESH             0x79
#define ISC_PROGRAM_SECURITY    0xCE
#define ISC_PROGRAM_SECPLUS     0xCF
#define UIDCODE_PUB             0x19

/*************************************************************************************/
/* LATTICE MachXO LCMXO2-4000HC CPLD */
extern int lcmxo2_4000hc_cpld_erase(void);
extern int llcmxo2_4000hc_cpld_program(FILE *jed_fd);
extern int lcmxo2_4000hc_cpld_verify(FILE *jed_fd);
/*************************************************************************************/

//--------------------------------------------------
// Change the string to hex.
//--------------------------------------------------
#define BITS_FSM_NONE     0
#define BITS_FSM_READ     1
#define BITS_FSM_RESET    2
#define BITS_FSM_BITS     3
#define BITS_FSM_PUSH_HighBIT '1'
#define BITS_FSM_PUSH_ZeroBIT '0'
#define BITS_FSM_END_MARK '*'
/*************************************************************************************/

struct cpld_dev_info {
	const char		*name;
	unsigned int 		dev_id;
	unsigned short		dr_bits;		//col
	unsigned int		row_num;		//row
	int (*cpld_id)(unsigned int *id);
	int (*cpld_erase)(void);
	int (*cpld_program)(FILE *jed_fd);
	int (*cpld_verify)(FILE *jed_fd);
};

/*************************************************************************************/

static struct cpld_dev_info lattice_device_list[] = {
    [0] = {
		.name = "LATTICE MachXO LCMXO2-4000HC CPLD",
		.dev_id = 0x012BC043,
		.dr_bits = 128,
		.row_num = 3198,
		.cpld_erase = lcmxo2_4000hc_cpld_erase,
		.cpld_program = llcmxo2_4000hc_cpld_program,
		.cpld_verify = lcmxo2_4000hc_cpld_verify,
	},
};
