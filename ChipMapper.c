#include "ChipMapper.h"
#include "VGMSXPlay.h"
#include "chips/ChipIncl.h"
// #include "chips/mamedef.h"
// #include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

bool OpenedFM = false;

void chip_reg_write(UINT8 ChipType, UINT8 ChipID, UINT8 Port, UINT8 Offset,
                    UINT8 Data) {
  if (ChipID > 1)
    return;
  switch (ChipType) {
  case 0x00: // SN76496
    sn764xx_w(ChipID, Port, Data);
    break;
  case 0x01: // YM2413
    ym2413_w(ChipID, 0, Offset);
    ym2413_w(ChipID, 1, Data);
    break;
  case 0x02: // YM2151
    ym2151_w(ChipID, 0, Offset);
    ym2151_w(ChipID, 1, Data);
    break;
  case 0x03: // YM3812
    ym3812_w(ChipID, 0, Offset);
    ym3812_w(ChipID, 1, Data);
    break;
  case 0x04: // YM3526
    ym3526_w(ChipID, 0, Offset);
    ym3526_w(ChipID, 1, Data);
    break;
  case 0x05: // Y8950
    y8950_w(ChipID, 0, Offset);
    y8950_w(ChipID, 1, Data);
    break;
  case 0x06: // YMF262
    ymf262_w(ChipID, (Port << 1) | 0, Offset);
    ymf262_w(ChipID, (Port << 1) | 1, Data);
    break;
  case 0x07: // YMF278B
    ymf278b_w(ChipID, (Port << 1) | 0, Offset);
    ymf278b_w(ChipID, (Port << 1) | 1, Data);
    break;
  case 0x08: // AY8910
    ayxx_w(ChipID, 0, Offset);
    ayxx_w(ChipID, 1, Data);
    break;
  case 0x09: // K051649 (SCC)
    k051649_w(ChipID, (Port << 1) | 0, Offset);
    k051649_w(ChipID, (Port << 1) | 1, Data);
    break;
  }
}

void chip_reg_write_ext(UINT8 ChipType, UINT8 ChipID, UINT8 Port, UINT16 Offset,
                        UINT8 Data) {
  chip_reg_write(ChipType, ChipID, Port, (UINT8)Offset, Data);
}

extern UINT8 ym3812_r(UINT8 ChipID, offs_t offset);
extern UINT8 ym3526_r(UINT8 ChipID, offs_t offset);
extern UINT8 y8950_r(UINT8 ChipID, offs_t offset);
extern UINT8 ymf262_r(UINT8 ChipID, offs_t offset);

UINT8 chip_reg_read(UINT8 ChipType, UINT8 ChipID, UINT8 Port, UINT8 Offset) {
  if (ChipID > 1)
    return 0xFF;
  switch (ChipType) {
  case 0x03: // YM3812
    return ym3812_r(ChipID, 0);
  case 0x04: // YM3526
    return ym3526_r(ChipID, 0);
  case 0x05: // Y8950
    return y8950_r(ChipID, 0);
  case 0x06: // YMF262
    return ymf262_r(ChipID, (Port << 1) | 0);
  default:
    return 0xFF;
  }
}

// Stubs for hardware FM functions
void setup_real_fm(UINT8 ChipType, UINT8 ChipID) {}
void setup_chips_hw(void) {}
void close_real_fm(void) {}
void open_real_fm(void) {}
void RefreshVolume(void) {}
void StartSkipping(void) {}
void StopSkipping(void) {}
void OpenPortTalk(void) {}
void OPL_Hardware_Detecton(void) {}

void open_fm_option(UINT8 ChipType, UINT8 OptType, UINT32 OptVal) {}
void ym2413opl_set_emu_core(UINT8 Emulator) {}
