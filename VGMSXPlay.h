#ifndef VGMSXPLAY_H
#define VGMSXPLAY_H
// Header File for structures and constants used within VGMSXPlay.c

// --- Definitions merged from chips/mamedef.h ---

/* 8-bit values */
typedef unsigned char UINT8;
typedef signed char INT8;

/* 16-bit values */
typedef unsigned short UINT16;
typedef signed short INT16;

/* 32-bit values */
typedef unsigned int UINT32;
typedef signed int INT32;

/* 64-bit values */

__extension__ typedef unsigned long long UINT64;
__extension__ typedef signed long long INT64;

/* offsets and addresses are 32-bit (for now...) */
typedef UINT32 offs_t;

/* stream_sample_t is used to represent a single sample in a sound stream */
typedef INT32 stream_sample_t;

#if defined(VGM_BIG_ENDIAN)
#define BYTE_XOR_BE(x) (x)
#elif defined(VGM_LITTLE_ENDIAN)
#define BYTE_XOR_BE(x) ((x) ^ 0x01)
#else
// don't define BYTE_XOR_BE so that it throws an error when compiling
#endif

#if defined(_MSC_VER)
#define INLINE static __inline
#elif defined(__GNUC__)
#define INLINE static __inline__
#else
#define INLINE static inline
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _DEBUG
#define logerror printf
#else
#define logerror
#endif

extern stream_sample_t *DUMMYBUF[];

typedef void (*SRATE_CALLBACK)(void *, UINT32);

// Boolean type
#ifndef __cplusplus
typedef unsigned char bool;
#define false 0
#define true 1
#endif

// ------------------------------------------------

#include "VGMFile.h"
// #include <stdbool.h>

#define VGMPLAY_VER_STR "0.40.9.1"
// #define APLHA
// #define BETA
#define VGM_VER_STR "1.71b"
#define VGM_VER_NUM 0x170

#define CHIP_COUNT 0x0A
typedef struct chip_options {
  bool Disabled;
  UINT8 EmuCore;
  UINT8 ChnCnt;
  // Special Flags:
  //	YM2612:	Bit 0 - DAC Highpass Enable, Bit 1 - SSG-EG Enable
  //	YM-OPN:	Bit 0 - Disable AY8910-Part
  UINT16 SpecialFlags;

  // Channel Mute Mask - 1 Channel is represented by 1 bit
  UINT32 ChnMute1;
  // Mask 2 - used by YMF287B for OPL4 Wavetable Synth and by YM2608/YM2610 for
  // PCM
  UINT32 ChnMute2;
  // Mask 3 - used for the AY-part of some OPN-chips
  UINT32 ChnMute3;

  INT16 *Panning;
} CHIP_OPTS;
typedef struct chips_options {
  CHIP_OPTS SN76496;
  CHIP_OPTS YM2413;
  CHIP_OPTS YM2151;
  CHIP_OPTS YM3812;
  CHIP_OPTS YM3526;
  CHIP_OPTS Y8950;
  CHIP_OPTS YMF262;
  CHIP_OPTS YMF278B;
  CHIP_OPTS AY8910;
  CHIP_OPTS K051649;
} CHIPS_OPTION;

// Defines for the DBUS Signal handler (stubs)
#define SIGNAL_METADATA 0x01
#define SIGNAL_PLAYSTATUS 0x02
#define SIGNAL_SEEKSTATUS 0x04
#define SIGNAL_SEEK 0x08
#define SIGNAL_CONTROLS 0x10
#define SIGNAL_VOLUME 0x20
#define SIGNAL_ALL 0xFF

void DBus_ReadWriteDispatch(void);
void DBus_EmitSignal(UINT8 type);

// --- Merged from VGMPlay_Intf.h ---

#define NO_WCHAR_FILENAMES

typedef struct waveform_16bit_stereo {
  INT16 Left;
  INT16 Right;
} WAVE_16BS;

typedef struct waveform_32bit_stereo {
  INT32 Left;
  INT32 Right;
} WAVE_32BS;

void VGMPlay_Init(void);
void VGMPlay_Init2(void);
void VGMPlay_Deinit(void);
char *FindFile(const char *FileName);

UINT32 GetGZFileLength(const char *FileName);
bool OpenVGMFile(const char *FileName);
void CloseVGMFile(void);

void FreeGD3Tag(GD3_TAG *TagData);
UINT32 GetVGMFileInfo(const char *FileName, VGM_HEADER *RetVGMHead,
                      GD3_TAG *RetGD3Tag);
UINT32 CalcSampleMSec(UINT64 Value, UINT8 Mode);
UINT32 CalcSampleMSecExt(UINT64 Value, UINT8 Mode, VGM_HEADER *FileHead);
const char *GetChipName(UINT8 ChipID);
const char *GetAccurateChipName(UINT8 ChipID, UINT8 SubType);
UINT32 GetChipClock(VGM_HEADER *FileHead, UINT8 ChipID, UINT8 *RetSubType);

INT32 SampleVGM2Playback(INT32 SampleVal);
INT32 SamplePlayback2VGM(INT32 SampleVal);

void PlayVGM(void);
void StopVGM(void);
void RestartVGM(void);
void PauseVGM(bool Pause);
void SeekVGM(bool Relative, INT32 PlayBkSamples);
void RefreshMuting(void);
void RefreshPanning(void);
void RefreshPlaybackOptions(void);

extern UINT32 PLFileCount;

UINT32 FillBuffer(WAVE_16BS *Buffer, UINT32 BufferSize);

// --- Merged from Stream.h ---
#define MAX_PATH PATH_MAX

#define SAMPLESIZE sizeof(WAVE_16BS)
#define BUFSIZE_MAX 0x1000 // Maximum Buffer Size in Bytes
#define BUFSIZELD 11       // Buffer Size
#define AUDIOBUFFERS 200   // Maximum Buffer Count

UINT8 StartStream(UINT8 DeviceID);
UINT8 StopStream(void);
void StartAudioWarmup(void); // Pre-init audio in background
void PauseStream(bool PauseOn);
void WaveOutLinuxCallBack(UINT32 WrtSmpls);
extern UINT32 SMPL_P_BUFFER;

#endif
