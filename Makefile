CC = gcc
OBJ = obj
EMUOBJ = $(OBJ)/chips
EMUSRC = chips
MAINFLAGS = -D_GNU_SOURCE -DUSE_ALSA
CFLAGS = -Os -march=x86-64 -fstrict-aliasing -flto=auto -fno-inline-small-functions -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -fomit-frame-pointer -ffast-math -fvisibility=hidden -fno-stack-protector -fno-ident -fmerge-all-constants -fuse-ld=gold -Wl,--gc-sections,--build-id=none,-O1,--hash-style=gnu $(MAINFLAGS)
LDFLAGS = -march=x86-64 -fuse-ld=gold -Wl,--gc-sections,--build-id=none,-O1,--hash-style=gnu,--as-needed,--no-undefined,--no-allow-shlib-undefined,--no-undefined-version,--no-keep-memory,-z,nodlopen,-z,nodump,-z,noexecstack,-z,now,-z,norelro,-z,combreloc -s

LIBS = -lasound -lz -lpthread
MAINOBJS = $(OBJ)/VGMSXPlay.o $(OBJ)/ChipMapper.o $(OBJ)/minilibm.o
EMUOBJS = $(EMUOBJ)/2151intf.o $(EMUOBJ)/2413intf.o $(EMUOBJ)/262intf.o $(EMUOBJ)/3526intf.o $(EMUOBJ)/3812intf.o $(EMUOBJ)/8950intf.o $(EMUOBJ)/ay_intf.o $(EMUOBJ)/sn764intf.o $(EMUOBJ)/adlibemu_opl2.o $(EMUOBJ)/adlibemu_opl3.o $(EMUOBJ)/dac_control.o $(EMUOBJ)/emu2149.o $(EMUOBJ)/emu2413.o $(EMUOBJ)/fmopl.o $(EMUOBJ)/k051649.o $(EMUOBJ)/panning.o $(EMUOBJ)/sn76496.o $(EMUOBJ)/ym2151.o $(EMUOBJ)/ymdeltat.o $(EMUOBJ)/ymf262.o $(EMUOBJ)/ymf278b.o
all: vgmsx
$(OBJ)/%.o: %.c
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@
$(EMUOBJ)/%.o: $(EMUSRC)/%.c
	@mkdir -p $(EMUOBJ)
	$(CC) $(CFLAGS) -c $< -o $@
vgmsx: $(EMUOBJS) $(MAINOBJS) $(OBJ)/VGMSXPlayUI.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)
clean:
	rm -rf $(OBJ) vgmsx
