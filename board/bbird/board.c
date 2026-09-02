/* board/bbird/board.c - the Blackbird SoC port (the example board).
 * UART: 16550-style at 0x10011000 (THR at +0, LSR at +0x14, DLL/DLM
 * behind LCR.DLAB at +0x0c/+0x00/+0x04); the divisor resets to 0 so it
 * MUST be programmed before the first byte.  IS_SIM picks 16 clk/bit
 * (fast Verilator runs); hardware runs 115200 at 100 MHz.
 * Exit: an AXI4-Lite tohost peripheral at 0x10012000; the testbench
 * decodes ((code<<1)|1) and halts the simulation. */
#define UART_BASE 0x10011000u
#define UART_THR  (*(volatile unsigned *)(UART_BASE + 0x00))
#define UART_LSR  (*(volatile unsigned *)(UART_BASE + 0x14))
#define UART_LCR  (*(volatile unsigned *)(UART_BASE + 0x0c))
#define UART_DLL  (*(volatile unsigned *)(UART_BASE + 0x00))
#define UART_DLM  (*(volatile unsigned *)(UART_BASE + 0x04))
#define TOHOST    (*(volatile unsigned *)0x10012000u)

void board_early_init(void)
{
#ifdef IS_SIM
    unsigned div = 15;            /* 16 clk/bit: what the testbench decodes */
#else
    unsigned div = 867;           /* 115200 @ 100 MHz */
#endif
    UART_LCR = 0x83;              /* DLAB + 8n1 */
    UART_DLL = div & 0xff;
    UART_DLM = div >> 8;
    UART_LCR = 0x03;              /* 8n1 */
}

void board_putc(int c)
{
    while (!(UART_LSR & 0x20)) {} /* THR empty */
    UART_THR = (unsigned)c & 0xff;
}

__attribute__((noreturn)) void board_exit(unsigned code)
{
    TOHOST = (code << 1) | 1u;
    for (;;) {}
}
