/****************************************************************

    MAME / MESS functions

****************************************************************/

#include "../VGMSXPlay.h"
#include <stddef.h> // for NULL
#include <stdlib.h>
// #include "sndintrf.h"
// #include "streams.h"
#include "2413intf.h"
#include "emu2413.h"

#ifdef ENABLE_ALL_CORES
#define EC_NUKED 0x02 // Nuked OPLL
#define EC_MAME 0x01  // YM2413 core from MAME
#endif
#define EC_EMU2413                                                             \
  0x00 // EMU2413 core from in_vgm, value 0 because it's better than MAME

/* for stream system */
typedef struct _ym2413_state ym2413_state;
struct _ym2413_state {
  // sound_stream *	stream;
  void *chip;
  UINT8 Mode;
};

static unsigned char vrc7_inst[(16 + 3) * 8] = {
#include "vrc7tone.h"
};

extern UINT8 CHIP_SAMPLING_MODE;
extern INT32 CHIP_SAMPLE_RATE;
static UINT8 EMU_CORE = 0x00;

#define MAX_CHIPS 0x02
static ym2413_state YM2413Data[MAX_CHIPS];

/*INLINE ym2413_state *get_safe_token(const device_config *device)
{
        assert(device != NULL);
        assert(device->token != NULL);
        assert(device->type == SOUND);
        assert(sound_get_type(device) == SOUND_YM2413);
        return (ym2413_state *)device->token;
}*/

#ifdef UNUSED_FUNCTION
void YM2413DAC_update(int chip, stream_sample_t **inputs,
                      stream_sample_t **_buffer, int length) {
  INT16 *buffer = _buffer[0];
  static int out = 0;

  if (ym2413[chip].reg[0x0F] & 0x01) {
    out = ((ym2413[chip].reg[0x10] & 0xF0) << 7);
  }
  while (length--)
    *(buffer++) = out;
}
#endif

static void _emu2413_calc_stereo(OPLL *opll, INT32 **out, int samples) {
  INT32 *bufL = out[0];
  INT32 *bufR = out[1];
  INT32 buffers[2];
  int i;

  for (i = 0; i < samples; i++) {
    OPLL_calcStereo(opll, buffers);
    bufL[i] = buffers[0];
    bufR[i] = buffers[1];
  }
}

static void _emu2413_set_mute_mask(OPLL *opll, UINT32 MuteMask) {
  unsigned char CurChn;
  UINT32 ChnMsk;

  for (CurChn = 0; CurChn < 14; CurChn++) {
    if (CurChn < 9) {
      ChnMsk = OPLL_MASK_CH(CurChn);
    } else {
      switch (CurChn) {
      case 9:
        ChnMsk = OPLL_MASK_BD;
        break;
      case 10:
        ChnMsk = OPLL_MASK_SD;
        break;
      case 11:
        ChnMsk = OPLL_MASK_TOM;
        break;
      case 12:
        ChnMsk = OPLL_MASK_CYM;
        break;
      case 13:
        ChnMsk = OPLL_MASK_HH;
        break;
      default:
        ChnMsk = 0;
        break;
      }
    }
    if ((MuteMask >> CurChn) & 0x01)
      opll->mask |= ChnMsk;
    else
      opll->mask &= ~ChnMsk;
  }

  return;
}

// static STREAM_UPDATE( ym2413_stream_update )
void ym2413_stream_update(UINT8 ChipID, stream_sample_t **outputs,
                          int samples) {
  ym2413_state *info = &YM2413Data[ChipID];
  _emu2413_calc_stereo(info->chip, outputs, samples);
}

static void _stream_update(void *param, int interval) {
  ym2413_state *info = (ym2413_state *)param;
  _emu2413_calc_stereo(info->chip, DUMMYBUF, 0);
}

// static DEVICE_START( ym2413 )
int device_start_ym2413(UINT8 ChipID, int clock) {
  // ym2413_state *info = get_safe_token(device);
  ym2413_state *info;
  int rate;

  if (ChipID >= MAX_CHIPS)
    return 0;

  info = &YM2413Data[ChipID];
  info->Mode = (clock & 0x80000000) >> 31;
  clock &= 0x7FFFFFFF;

  rate = clock / 72;
  if ((CHIP_SAMPLING_MODE == 0x01 && rate < CHIP_SAMPLE_RATE) ||
      CHIP_SAMPLING_MODE == 0x02)
    rate = CHIP_SAMPLE_RATE;
  
  /* emulator create */
  info->chip = OPLL_new(clock, rate);
  if (info->chip == NULL)
    return 0;

  OPLL_setChipMode(info->chip, info->Mode);
  if (info->Mode)
    OPLL_setPatch(info->chip, vrc7_inst);

  // Note: VRC7 instruments are set in device_reset if necessary.
  return rate;
}

// static DEVICE_STOP( ym2413 )
void device_stop_ym2413(UINT8 ChipID) {
  // ym2413_state *info = get_safe_token(device);
  ym2413_state *info = &YM2413Data[ChipID];
  OPLL_delete(info->chip);
}

// static DEVICE_RESET( ym2413 )
void device_reset_ym2413(UINT8 ChipID) {
  // ym2413_state *info = get_safe_token(device);
  ym2413_state *info = &YM2413Data[ChipID];
  OPLL_reset(info->chip);
}

// WRITE8_DEVICE_HANDLER( ym2413_w )
void ym2413_w(UINT8 ChipID, offs_t offset, UINT8 data) {
  // ym2413_state *info = get_safe_token(device);
  ym2413_state *info = &YM2413Data[ChipID];
  OPLL_writeIO(info->chip, offset & 1, data);
}

// WRITE8_DEVICE_HANDLER( ym2413_register_port_w )
void ym2413_register_port_w(UINT8 ChipID, offs_t offset, UINT8 data) {
  ym2413_w(ChipID, 0, data);
}
// WRITE8_DEVICE_HANDLER( ym2413_data_port_w )
void ym2413_data_port_w(UINT8 ChipID, offs_t offset, UINT8 data) {
  ym2413_w(ChipID, 1, data);
}

void ym2413_set_emu_core(UINT8 Emulator) {
  EMU_CORE = EC_EMU2413;
  return;
}

void ym2413_set_mute_mask(UINT8 ChipID, UINT32 MuteMask) {
  ym2413_state *info = &YM2413Data[ChipID];
  _emu2413_set_mute_mask(info->chip, MuteMask);
  return;
}

void ym2413_set_panning(UINT8 ChipID, INT16 *PanVals) {
  ym2413_state *info = &YM2413Data[ChipID];
  UINT8 CurChn;
  UINT8 EmuChn;

  for (CurChn = 0x00; CurChn < 0x0E; CurChn++) {
    // input:  0..8, BD, SD, TOM, CYM, HH
    // output: 0..8, BD, HH, SD, TOM, CYM
    if (CurChn < 10)
      EmuChn = CurChn;
    else if (CurChn < 13)
      EmuChn = CurChn + 1;
    else
      EmuChn = 10;
    OPLL_setPanEx(info->chip, EmuChn, PanVals[CurChn]);
  }

  return;
}

/**************************************************************************
 * Generic get_info
 **************************************************************************/

/*DEVICE_GET_INFO( ym2413 )
{
        switch (state)
        {
                // --- the following bits of info are returned as 64-bit signed
integers --- case DEVINFO_INT_TOKEN_BYTES:
info->i = sizeof(ym2413_state);				break;

                // --- the following bits of info are returned as pointers to
data or functions --- case DEVINFO_FCT_START:
info->start = DEVICE_START_NAME( ym2413 );				break;
                case DEVINFO_FCT_STOP:
info->stop = DEVICE_STOP_NAME( ym2413 );				break;
                case DEVINFO_FCT_RESET:
info->reset = DEVICE_RESET_NAME( ym2413 );				break;

                // --- the following bits of info are returned as
NULL-terminated strings --- case DEVINFO_STR_NAME:
strcpy(info->s, "YM2413"); break; case DEVINFO_STR_FAMILY:
strcpy(info->s, "Yamaha FM");						break;
                case DEVINFO_STR_VERSION:
strcpy(info->s, "1.0"); break; case DEVINFO_STR_SOURCE_FILE:
strcpy(info->s, __FILE__); break; case DEVINFO_STR_CREDITS:
strcpy(info->s, "Copyright Nicola Salmoria and the MAME Team"); break;
        }
}*/
