/************************
 *  DAC Stream Control  *
 ***********************/
// (Custom Driver to handle PCM Streams)
//
// Written on 3 February 2011 by Valley Bell
// Last Update: 29 September 2017
//
// Only for usage in non-commercial, VGM file related software.

#include "dac_control.h"
#include "../VGMSXPlay.h"
#include <stddef.h> // for NULL

void chip_reg_write(UINT8 ChipType, UINT8 ChipID, UINT8 Port, UINT8 Offset,
                    UINT8 Data);

extern UINT32 SampleRate;
#define DAC_SMPL_RATE SampleRate

typedef struct _dac_control {
  // Commands sent to dest-chip
  UINT8 DstChipType;
  UINT8 DstChipID;
  UINT16 DstCommand;
  UINT8 CmdSize;

  UINT32 Frequency; // Frequency (Hz) at which the commands are sent
  UINT32 DataLen;   // to protect from reading beyond End Of Data
  const UINT8 *Data;
  UINT32 DataStart; // Position where to start
  UINT8 StepSize;   // usually 1, set to 2 for L/R interleaved data
  UINT8 StepBase;   // usually 0, set to 0/1 for L/R interleaved data
  UINT32 CmdsToSend;

  UINT8 Running;
  UINT8 Reverse;
  UINT32 Step; // Position in Player SampleRate
  UINT32 Pos;  // Position in Data SampleRate
  UINT32 RemainCmds;
  UINT32 RealPos; // true Position in Data (== Pos, if Reverse is off)
  UINT8 DataStep; // always StepSize * CmdSize
} dac_control;

#define MAX_CHIPS 0xFF
static dac_control DACData[MAX_CHIPS];

INLINE void daccontrol_SendCommand(dac_control *chip) {
  UINT8 Port;
  UINT8 Command;
  UINT8 Data;
  const UINT8 *ChipData;

  if (chip->Running & 0x10) // command already sent
    return;
  if (chip->DataStart + chip->RealPos >= chip->DataLen)
    return;

  ChipData = chip->Data + (chip->DataStart + chip->RealPos);
  switch (chip->DstChipType) {
  case 0x00: // SN76496 (4-bit Register, 4-bit/10-bit Data)
    Command = (chip->DstCommand & 0x00F0) >> 0;
    Data = ChipData[0x00] & 0x0F;
    if (Command & 0x10) {
      chip_reg_write(chip->DstChipType, chip->DstChipID, 0x00, 0x00,
                     Command | Data);
    } else {
      Port = ((ChipData[0x01] & 0x03) << 4) | ((ChipData[0x00] & 0xF0) >> 4);
      chip_reg_write(chip->DstChipType, chip->DstChipID, 0x00, 0x00,
                     Command | Data);
      chip_reg_write(chip->DstChipType, chip->DstChipID, 0x00, 0x00, Port);
    }
    break;
  case 0x01: // YM2413 (1)
  case 0x02: // YM2151 (2)
  case 0x03: // YM3812 (3)
  case 0x04: // YM3526 (4)
  case 0x05: // Y8950  (5)
  case 0x08: // AY8910 (8)
    Command = (chip->DstCommand & 0x00FF) >> 0;
    Data = ChipData[0x00];
    chip_reg_write(chip->DstChipType, chip->DstChipID, 0x00, Command, Data);
    break;
  case 0x06: // YMF262 (6)
  case 0x07: // YMF278B (7)
  case 0x09: // K051649 (9)
    Port = (chip->DstCommand & 0xFF00) >> 8;
    Command = (chip->DstCommand & 0x00FF) >> 0;
    Data = ChipData[0x00];
    chip_reg_write(chip->DstChipType, chip->DstChipID, Port, Command, Data);
    break;
  }
  chip->Running |= 0x10;

  return;
}

INLINE UINT32 muldiv64round(UINT32 Multiplicand, UINT32 Multiplier,
                            UINT32 Divisor) {
  return (UINT32)(((UINT64)Multiplicand * Multiplier + Divisor / 2) / Divisor);
}

void daccontrol_update(UINT8 ChipID, UINT32 samples) {
  dac_control *chip = &DACData[ChipID];
  UINT32 NewPos;
  INT16 RealDataStp;

  if (chip->Running & 0x80) // disabled
    return;
  if (!(chip->Running & 0x01)) // stopped
    return;

  if (!chip->Reverse)
    RealDataStp = chip->DataStep;
  else
    RealDataStp = -chip->DataStep;

  if (samples > 0x20) {
    NewPos = chip->Step + (samples - 0x10);
    NewPos =
        muldiv64round(NewPos * chip->DataStep, chip->Frequency, DAC_SMPL_RATE);
    while (chip->RemainCmds && chip->Pos < NewPos) {
      chip->Pos += chip->DataStep;
      chip->RealPos += RealDataStp;
      chip->RemainCmds--;
    }
  }

  chip->Step += samples;
  NewPos = muldiv64round(chip->Step * chip->DataStep, chip->Frequency,
                         DAC_SMPL_RATE);
  daccontrol_SendCommand(chip);

  while (chip->RemainCmds && chip->Pos < NewPos) {
    daccontrol_SendCommand(chip);
    chip->Pos += chip->DataStep;
    chip->RealPos += RealDataStp;
    chip->Running &= ~0x10;
    chip->RemainCmds--;
  }

  if (!chip->RemainCmds && (chip->Running & 0x04)) {
    chip->RemainCmds = chip->CmdsToSend;
    chip->Step = 0x00;
    chip->Pos = 0x00;
    if (!chip->Reverse)
      chip->RealPos = 0x00;
    else
      chip->RealPos = (chip->CmdsToSend - 0x01) * chip->DataStep;
  }

  if (!chip->RemainCmds)
    chip->Running &= ~0x01; // stop

  return;
}

UINT8 device_start_daccontrol(UINT8 ChipID) {
  dac_control *chip;
  if (ChipID >= MAX_CHIPS)
    return 0;
  chip = &DACData[ChipID];
  chip->DstChipType = 0xFF;
  chip->DstChipID = 0x00;
  chip->DstCommand = 0x0000;
  chip->Running = 0xFF;
  return 1;
}

void device_stop_daccontrol(UINT8 ChipID) {
  dac_control *chip = &DACData[ChipID];
  chip->Running = 0xFF;
}

void device_reset_daccontrol(UINT8 ChipID) {
  dac_control *chip = &DACData[ChipID];
  chip->DstChipType = 0x00;
  chip->DstChipID = 0x00;
  chip->DstCommand = 0x00;
  chip->CmdSize = 0x00;
  chip->Frequency = 0;
  chip->DataLen = 0x00;
  chip->Data = NULL;
  chip->DataStart = 0x00;
  chip->StepSize = 0x00;
  chip->StepBase = 0x00;
  chip->Running = 0x00;
  chip->Reverse = 0x00;
  chip->Step = 0x00;
  chip->Pos = 0x00;
  chip->RealPos = 0x00;
  chip->RemainCmds = 0x00;
  chip->DataStep = 0x00;
}

void daccontrol_setup_chip(UINT8 ChipID, UINT8 ChType, UINT8 ChNum,
                           UINT16 Command) {
  dac_control *chip = &DACData[ChipID];
  chip->DstChipType = ChType;
  chip->DstChipID = ChNum;
  chip->DstCommand = Command;
  switch (chip->DstChipType) {
  case 0x00: // SN76496
    if (chip->DstCommand & 0x0010)
      chip->CmdSize = 0x01;
    else
      chip->CmdSize = 0x02;
    break;
  default:
    chip->CmdSize = 0x01;
    break;
  }
  chip->DataStep = chip->CmdSize * chip->StepSize;
}

void daccontrol_set_data(UINT8 ChipID, UINT8 *Data, UINT32 DataLen,
                         UINT8 StepSize, UINT8 StepBase) {
  dac_control *chip = &DACData[ChipID];
  if (chip->Running & 0x80)
    return;
  if (DataLen && Data != NULL) {
    chip->DataLen = DataLen;
    chip->Data = Data;
  } else {
    chip->DataLen = 0x00;
    chip->Data = NULL;
  }
  chip->StepSize = StepSize ? StepSize : 1;
  chip->StepBase = StepBase;
  chip->DataStep = chip->CmdSize * chip->StepSize;
}

void daccontrol_refresh_data(UINT8 ChipID, UINT8 *Data, UINT32 DataLen) {
  dac_control *chip = &DACData[ChipID];
  if (chip->Running & 0x80)
    return;
  if (DataLen && Data != NULL) {
    chip->DataLen = DataLen;
    chip->Data = Data;
  } else {
    chip->DataLen = 0x00;
    chip->Data = NULL;
  }
}

void daccontrol_set_frequency(UINT8 ChipID, UINT32 Frequency) {
  dac_control *chip = &DACData[ChipID];
  if (chip->Running & 0x80)
    return;
  if (Frequency)
    chip->Step = chip->Step * chip->Frequency / Frequency;
  chip->Frequency = Frequency;
}

void daccontrol_start(UINT8 ChipID, UINT32 DataPos, UINT8 LenMode,
                      UINT32 Length) {
  dac_control *chip = &DACData[ChipID];
  UINT16 CmdStepBase;
  if (chip->Running & 0x80)
    return;
  CmdStepBase = chip->CmdSize * chip->StepBase;
  if (DataPos != 0xFFFFFFFF) {
    chip->DataStart = DataPos + CmdStepBase;
    if (chip->DataStart > chip->DataLen)
      chip->DataStart = chip->DataLen;
  }
  switch (LenMode & 0x0F) {
  case DCTRL_LMODE_CMDS:
    chip->CmdsToSend = Length;
    break;
  case DCTRL_LMODE_MSEC:
    chip->CmdsToSend = 1000 * Length / chip->Frequency;
    break;
  case DCTRL_LMODE_TOEND:
    chip->CmdsToSend =
        (chip->DataLen - (chip->DataStart - CmdStepBase)) / chip->DataStep;
    break;
  case DCTRL_LMODE_BYTES:
    chip->CmdsToSend = Length / chip->DataStep;
    break;
  }
  chip->Reverse = (LenMode & 0x10) >> 4;
  chip->RemainCmds = chip->CmdsToSend;
  chip->Step = 0x00;
  chip->Pos = 0x00;
  if (!chip->Reverse)
    chip->RealPos = 0x00;
  else
    chip->RealPos = (chip->CmdsToSend - 0x01) * chip->DataStep;
  chip->Running &= ~0x04;
  chip->Running |= (LenMode & 0x80) ? 0x04 : 0x00;
  chip->Running |= 0x01;
  chip->Running &= ~0x10;
}

void daccontrol_stop(UINT8 ChipID) {
  dac_control *chip = &DACData[ChipID];
  if (chip->Running & 0x80)
    return;
  chip->Running &= ~0x01;
}
