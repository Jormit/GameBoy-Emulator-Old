# GameBoy-emulator
Gameboy emulator written in c. All instructions are included however it can only run Tetris and Dr Mario due to lack of bank switching implementation and buggy cpu emulation. Cpu usage peaks at around 10% with no visible lag. This emulator does not use a hardcoded post boot state, instead it loads the bootrom into memory until it reaches 0x100 where it then swaps it out with the main rom. Instructions are executed using a function lookup table that is indexed by each opcode instead of a traditional case break approach. All i/o is handled by the SDL library. 

# usage
main.exe /path/to/romfile

# screenshots

![](Images/1.png)  |  ![](Images/2.png) | ![](Images/3.png)


