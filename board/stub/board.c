/* board/stub/board.c - EMPTY PORT TEMPLATE.  Fill in the three functions
 * for your rv32imaf CPU and point the builders at this directory with
 * BOARD=path/to/your/board.  See board/bbird/ for a worked example and
 * ld/bm.ld for the memory map you must also adapt. */

void board_early_init(void)
{
    /* TODO: clocks, UART divisor -- runs before any output. */
}

void board_putc(int c)
{
    /* TODO: blocking write of one byte to your console. */
    (void)c;
}

__attribute__((noreturn)) void board_exit(unsigned code)
{
    /* TODO: report `code` (tohost store, semihosting, LED...) and stop. */
    (void)code;
    for (;;) {}
}
