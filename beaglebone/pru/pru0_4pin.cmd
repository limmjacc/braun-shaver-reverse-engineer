MEMORY
{
    PAGE 0: PRU_IMEM : org = 0x00000000 len = 0x00002000  /* PRU0 instruction RAM, 8KB */
    PAGE 1: PRU_DMEM : org = 0x00000000 len = 0x00001FF0  /* PRU0 own data RAM, 8KB (minus a little slack) */
}

SECTIONS
{
    .text : > PRU_IMEM, PAGE 0
    .data : > PRU_DMEM, PAGE 1
    .bss  : > PRU_DMEM, PAGE 1
}
