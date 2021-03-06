/*
 * See AN385 - ARM Cortex-M3 SMM on V2M-MPS2, aka DAI0385D
 * See Cortex-M System Design Kit Technical Reference, aka DDI0479B
 *
 * WARNING: This is the bare minimum required to get the SDK UART running under
 * QEMU. It WILL NOT be functional on real hardware.
 */

#include <machine.h>

#include <boot.h>
#include <sys/include/arch.h>

struct cmsdk_uart {
	uint32_t DATA;
	union cmsdk_uart_state {
		uint32_t r;
		struct {
			uint32_t TX_FULL : 1;
			uint32_t RX_FULL : 1;
			uint32_t TX_OVERRUN : 1;
			uint32_t RX_OVERRUN : 1;
			uint32_t : 28;
		};
	} STATE;
	union cmsdk_uart_ctrl {
		uint32_t r;
		struct {
			uint32_t TX_ENABLE : 1;
			uint32_t RX_ENABLE : 1;
			uint32_t TX_INTERRUPT_ENABLE : 1;
			uint32_t RX_INTERRUPT_ENABLE : 1;
			uint32_t TX_OVERRUN_INTERRUPT_ENABLE : 1;
			uint32_t RX_OVERRUN_INTERRUPT_ENABLE : 1;
			uint32_t TX_HIGH_SPEED_TEST_MODE : 1;
		};
	} CTRL;
	union {
		uint32_t INTSTATUS;
		uint32_t INTCLEAR;
	};
	uint32_t BAUDDIV;
};

#if defined(CONFIG_BOOT_CONSOLE)
static struct cmsdk_uart *const UART = (struct cmsdk_uart*)0x40004000;
#endif

/*
 * Setup machine state
 */
void machine_setup(void)
{
#if defined(CONFIG_BOOT_CONSOLE)
	write32(&UART->BAUDDIV, 16);	    /* QEMU doesn't care as long as >= 16 */
	write32(&UART->CTRL, (union cmsdk_uart_ctrl) {
		.TX_ENABLE = 1,
	}.r);
#endif
}

/*
 * Print one chracter
 */
void machine_putc(int c)
{
#if defined(CONFIG_BOOT_CONSOLE)
	while (read32(&UART->STATE).TX_FULL);
	write32(&UART->DATA, c);
#endif
}

/*
 * Load kernel image
 */
int machine_load_image(void)
{
	return load_bootimg();
}

/*
 * Panic handler
 */
void machine_panic(void)
{
	while (1);
}

/*
 * Initialise clocks.
 */
void machine_clock_init(void)
{
	/* QEMU doesn't require clock initialisation */
}

/*
 * Initialise stack
 */
void machine_early_memory_init(void)
{
	/* QEMU doesn't require stack initialisation */
}

/*
 * Initialise memory
 */
void machine_memory_init(void)
{
	/* QEMU doesn't require memory initialisation */
}
