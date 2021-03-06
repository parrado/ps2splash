EE_BIN = main.elf
EE_BIN_PACKED = BOOT.ELF
EE_BIN_STRIPPED = main-stripped.ELF
EE_OBJS = main.o  dma_asm.o gs.o gs_asm.o ps2_asm.o libcdvd_add.o OSDInit.o timer.o spng.o
EE_LDFLAGS = -L$(GSKIT)/lib -L$(PS2SDK)/ports/lib -Wl,--gc-sections
EE_LIBS = -ldebug -lc -lpatches -lcdvd  -lfileXio -lgskit_toolkit -lgskit -ldmakit -lz  
EE_INCS= -I$(GSKIT)/include -I$(PS2SDK)/ports/include
EE_CFLAGS=-Os -ffunction-sections -fdata-sections


all:
	$(MAKE) $(EE_BIN_PACKED)

	

clean:
	rm -f *.elf *.ELF *.irx *.o *.s
			

$(EE_BIN_STRIPPED): $(EE_BIN)
	$(EE_STRIP) -s -R .comment -R .gnu.version --strip-unneeded -o $@ $<
	
$(EE_BIN_PACKED): $(EE_BIN_STRIPPED)
	ps2-packer -v $< $@
	
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

