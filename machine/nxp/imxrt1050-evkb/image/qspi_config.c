#include <cpu/nxp/imxrt10xx/image/flexspi.h>

/*
 * FlexSPI configuration for IS25WP064AJBLE QSPI flash on IMXRT1050-EVKB and
 * MIMXRT1060-EVK boards.
 *
 * REVISIT: for now we are setting undocumented features so that the generated
 * configuration block is identical to the one generated by EVKB-IMXRT1050.
 *
 * REVISIT: we also set the version to 1.4 but reference manual rev 1 only
 * documents 1.1.
 */
const struct flexspi_boot_nor flexspi_boot_nor_ = {
	.memConfig = {
		.tag = "FCFB",
		.version.ascii = 'V',
		.version.major = 1,
		.version.minor = 4, /* REVISIT: reference manual uses 0/1 but SDK uses 4 */
		.version.bugfix = 0,
		.readSampleClkSrc = 1, /* loopback from DQS pad */
		.csHoldTime = 3,
		.csSetupTime = 3,
		.controllerMiscOption = 0,
		.sflashPadType = 4,
		.serialClkFreq = 6, /* 100MHz */
		.sflashA1Size = 8 * 1024 * 1024,
		.lookupTable = {
			{
				.opcode0 = FLEXSPI_OPCODE_CMD_SDR,
				.num_pads0 = FLEXSPI_NUM_PADS_1,
				.operand0 = 0xEB,
				.opcode1 = FLEXSPI_OPCODE_RADDR_SDR,
				.num_pads1 = FLEXSPI_NUM_PADS_4,
				.operand1 = 0x18,
			},
			{
				.opcode0 = FLEXSPI_OPCODE_DUMMY_SDR,
				.num_pads0 = FLEXSPI_NUM_PADS_4,
				.operand0 = 0x06,
				.opcode1 = FLEXSPI_OPCODE_READ_SDR,
				.num_pads1 = FLEXSPI_NUM_PADS_4,
				.operand1 = 0x04,
			},
		},
	},
	.pageSize = 256,
	.sectorSize = 4 * 1024,
	.blockSize = 256 * 1024,	/* REVISIT: undocumented */
	.isUniformBlockSize = 1,	/* REVISIT: undocumented */
};
