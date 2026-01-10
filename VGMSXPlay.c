// VGMSXPlay.c: C Source File of the Main Executable
//


#include <math.h> // for pow()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <limits.h> // for PATH_MAX

#undef MIXER_MUTING

#include <alsa/asoundlib.h>
#include <fcntl.h>
#include <pthread.h>

#include <time.h> // for clock_gettime()

#include <unistd.h> // for usleep()
#define Sleep(msec) usleep(msec * 1000)

#include <zlib.h>

#define FUINT8 unsigned int
#define FUINT16 unsigned int

#include "VGMSXPlay.h"
#ifdef CONSOLE_MODE
#include "Stream.h"
#endif

#include "chips/ChipIncl.h"

#include "ChipMapper.h"

typedef void (*strm_func)(UINT8 ChipID, stream_sample_t **outputs, int samples);

typedef struct chip_audio_attributes CAUD_ATTR;
struct chip_audio_attributes {
  UINT32 SmpRate;
  UINT16 Volume;
  UINT8 ChipType;
  UINT8 ChipID; // 0 - 1st chip, 1 - 2nd chip, etc.
  // Resampler Type:
  //	00 - Old
  //	01 - Upsampling
  //	02 - Copy
  //	03 - Downsampling
  UINT8 Resampler;
  strm_func StreamUpdate;
  UINT32 SmpP;     // Current Sample (Playback Rate)
  UINT32 SmpLast;  // Sample Number Last
  UINT32 SmpNext;  // Sample Number Next
  WAVE_32BS LSmpl; // Last Sample
  WAVE_32BS NSmpl; // Next Sample
  CAUD_ATTR *Paired;
};

typedef struct chip_audio_struct {
  CAUD_ATTR SN76496;
  CAUD_ATTR YM2413;
  CAUD_ATTR YM2151;
  CAUD_ATTR YM3812;
  CAUD_ATTR YM3526;
  CAUD_ATTR Y8950;
  CAUD_ATTR YMF262;
  CAUD_ATTR YMF278B;
  CAUD_ATTR AY8910;
  CAUD_ATTR K051649;
} CHIP_AUDIO;

typedef struct chip_aud_list CA_LIST;
struct chip_aud_list {
  CAUD_ATTR *CAud;
  CHIP_OPTS *COpts;
  CA_LIST *next;
};

typedef struct daccontrol_data {
  bool Enable;
  UINT8 Bank;
} DACCTRL_DATA;

typedef struct pcmbank_table {
  UINT8 ComprType;
  UINT8 CmpSubType;
  UINT8 BitDec;
  UINT8 BitCmp;
  UINT16 EntryCount;
  void *Entries;
} PCMBANK_TBL;


INLINE UINT16 ReadLE16(const UINT8 *Data);
INLINE UINT16 ReadBE16(const UINT8 *Data);
INLINE UINT32 ReadLE24(const UINT8 *Data);
INLINE UINT32 ReadLE32(const UINT8 *Data);
INLINE int gzgetLE16(gzFile hFile, UINT16 *RetValue);
INLINE int gzgetLE32(gzFile hFile, UINT32 *RetValue);
static UINT32 gcd(UINT32 x, UINT32 y);
static UINT32 GetGZFileLength_Internal(FILE *hFile);
static bool OpenVGMFile_Internal(gzFile hFile, UINT32 FileSize);
static void ReadVGMHeader(gzFile hFile, VGM_HEADER *RetVGMHead);
static UINT8 ReadGD3Tag(gzFile hFile, UINT32 GD3Offset, GD3_TAG *RetGD3Tag);
static void ReadChipExtraData32(UINT32 StartOffset, VGMX_CHP_EXTRA32 *ChpExtra);
static void ReadChipExtraData16(UINT32 StartOffset, VGMX_CHP_EXTRA16 *ChpExtra);
static wchar_t *MakeEmptyWStr(void);
static wchar_t *ReadWStrFromFile(gzFile hFile, UINT32 *FilePos, UINT32 EOFPos);
static UINT32 GetVGMFileInfo_Internal(gzFile hFile, UINT32 FileSize,
                                      VGM_HEADER *RetVGMHead,
                                      GD3_TAG *RetGD3Tag);
INLINE UINT32 MulDivRound(UINT64 Number, UINT64 Numerator, UINT64 Denominator);
static UINT16 GetChipVolume(VGM_HEADER *FileHead, UINT8 ChipID, UINT8 ChipNum,
                            UINT8 ChipCnt);

static void RestartPlaying(void);
static void Chips_GeneralActions(UINT8 Mode);

INLINE INT32 SampleVGM2Pbk_I(INT32 SampleVal); // inline functions
INLINE INT32 SamplePbk2VGM_I(INT32 SampleVal);
static UINT8 StartThread(void);
static UINT8 StopThread(void);
static bool SetMuteControl(bool mute);

static void InterpretFile(UINT32 SampleCount);
static void AddPCMData(UINT8 Type, UINT32 DataSize, const UINT8 *Data);
static bool DecompressDataBlk(VGM_PCM_DATA *Bank, UINT32 DataSize,
                              const UINT8 *Data);
static UINT8 GetDACFromPCMBank(void);
static UINT8 *GetPointerFromPCMBank(UINT8 Type, UINT32 DataPos);
static void ReadPCMTable(UINT32 DataSize, const UINT8 *Data);
static void InterpretVGM(UINT32 SampleCount);

static void GeneralChipLists(void);
static void SetupResampler(CAUD_ATTR *CAA);

INLINE INT16 Limit2Short(INT32 Value);
static void null_update(UINT8 ChipID, stream_sample_t **outputs, int samples);
static void dual_opl2_stereo(UINT8 ChipID, stream_sample_t **outputs,
                             int samples);
static void ResampleChipStream(CA_LIST *CLst, WAVE_32BS *RetSample,
                               UINT32 Length);
static INT32 RecalcFadeVolume(void);

UINT64 TimeSpec2Int64(const struct timespec *ts);

UINT32 SampleRate;

UINT32 VGMMaxLoop;
UINT32 VGMPbRate;
UINT32 FadeTime;
UINT32 PauseTime;
float VolumeLevel;
bool SurroundSound;
UINT8 HardStopOldVGMs;
bool FadeRAWLog;
bool PauseEmulate;
bool DoubleSSGVol;
UINT8 ResampleMode;
UINT8 CHIP_SAMPLING_MODE;
INT32 CHIP_SAMPLE_RATE;
UINT16 FMPort;
bool FMForce;
bool FMBreakFade;
float FMVol;
bool FMOPL2Pan;
CHIPS_OPTION ChipOpts[0x02];
UINT8 OPL_MODE;
UINT8 OPL_CHIPS;
stream_sample_t *DUMMYBUF[0x02] = {NULL, NULL};
char *AppPaths[8];
bool AutoStopSkip;
UINT8 FileMode;
VGM_HEADER VGMHead;
VGM_HDR_EXTRA VGMHeadX;
VGM_EXTRA VGMH_Extra;
UINT32 VGMDataLen;
UINT8 *VGMData;
GD3_TAG VGMTag;
#define PCM_BANK_COUNT 0x40
VGM_PCM_BANK PCMBank[PCM_BANK_COUNT];
PCMBANK_TBL PCMTbl;
UINT8 DacCtrlUsed;
UINT8 DacCtrlUsg[0xFF];
DACCTRL_DATA DacCtrl[0xFF];
CHIP_AUDIO ChipAudio[0x02];
CAUD_ATTR CA_Paired[0x02][0x03];
float MasterVol;
CA_LIST ChipListBuffer[0x200];
CA_LIST *ChipListAll;
CA_LIST *ChipListPause;
CA_LIST *CurChipList;

#define SMPL_BUFSIZE 0x2000
static INT32 *StreamBufs[0x02];

float VolumeBak;
// #endif

UINT32 VGMPos;
INT32 VGMSmplPos;
INT32 VGMSmplPlayed;
INT32 VGMSampleRate;
static UINT32 VGMPbRateMul;
static UINT32 VGMPbRateDiv;
static UINT32 VGMSmplRateMul;
static UINT32 VGMSmplRateDiv;
static UINT32 PauseSmpls;
bool VGMEnd;
bool EndPlay;
bool PausePlay;
bool FadePlay;
bool ForceVGMExec;
UINT32 PlayingTime;
UINT32 FadeStart;
UINT32 VGMMaxLoopM;
UINT32 VGMCurLoop;
float VolumeLevelM;
float FinalVol;

static bool Interpreting;

UINT8 IsVGMInit;

void VGMPlay_Init(void) {
  UINT8 CurChip;
  UINT8 CurCSet;
  UINT8 CurChn;
  CHIP_OPTS *TempCOpt;
  CAUD_ATTR *TempCAud;

  SampleRate = 44100;
  FadeTime = 5000;
  PauseTime = 0;

  HardStopOldVGMs = 0x00;
  FadeRAWLog = false;
  VolumeLevel = 1.0f;
  FMPort = 0x0000;
  FMForce = false;
  FMBreakFade = false;
  FMVol = 0.0f;
  FMOPL2Pan = false;
  SurroundSound = false;
  VGMMaxLoop = 0x02;
  VGMPbRate = 0;
  ResampleMode = 0x00;
  CHIP_SAMPLING_MODE = 0x00;
  CHIP_SAMPLE_RATE = 0x00000000;
  PauseEmulate = false;
  DoubleSSGVol = false;

  for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
    TempCAud = (CAUD_ATTR *)&ChipAudio[CurCSet];
    for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++, TempCAud++) {
      TempCOpt = (CHIP_OPTS *)&ChipOpts[CurCSet] + CurChip;

      TempCOpt->Disabled = false;
      TempCOpt->EmuCore = 0x00;
      TempCOpt->SpecialFlags = 0x00;
      TempCOpt->ChnCnt = 0x00;
      TempCOpt->ChnMute1 = 0x00;
      TempCOpt->ChnMute2 = 0x00;
      TempCOpt->ChnMute3 = 0x00;
      TempCOpt->Panning = NULL;

      TempCAud->ChipType = 0xFF;
      TempCAud->ChipID = CurCSet;
      TempCAud->Paired = NULL;
    }

    TempCAud = CA_Paired[CurCSet];
    for (CurChip = 0x00; CurChip < 0x03; CurChip++, TempCAud++) {
      TempCAud->ChipType = 0xFF;
      TempCAud->ChipID = CurCSet;
      TempCAud->Paired = NULL;
    }

    TempCOpt = (CHIP_OPTS *)&ChipOpts[CurCSet].SN76496;
    TempCOpt->ChnCnt = 0x04;
    TempCOpt->Panning = (INT16 *)malloc(sizeof(INT16) * TempCOpt->ChnCnt);
    for (CurChn = 0x00; CurChn < TempCOpt->ChnCnt; CurChn++)
      TempCOpt->Panning[CurChn] = 0x00;

    TempCOpt = (CHIP_OPTS *)&ChipOpts[CurCSet].YM2413;
    TempCOpt->ChnCnt = 0x09;
    TempCOpt->Panning = (INT16 *)malloc(sizeof(INT16) * TempCOpt->ChnCnt);
    for (CurChn = 0x00; CurChn < TempCOpt->ChnCnt; CurChn++)
      TempCOpt->Panning[CurChn] = 0x00;

    TempCOpt = (CHIP_OPTS *)&ChipOpts[CurCSet].YM2151;
    TempCOpt->ChnCnt = 0x08;
    TempCOpt->Panning = (INT16 *)malloc(sizeof(INT16) * TempCOpt->ChnCnt);
    for (CurChn = 0x00; CurChn < TempCOpt->ChnCnt; CurChn++)
      TempCOpt->Panning[CurChn] = 0x00;

    TempCOpt = (CHIP_OPTS *)&ChipOpts[CurCSet].AY8910;
    TempCOpt->ChnCnt = 0x03;
    TempCOpt->Panning = (INT16 *)malloc(sizeof(INT16) * TempCOpt->ChnCnt);
    for (CurChn = 0x00; CurChn < TempCOpt->ChnCnt; CurChn++)
      TempCOpt->Panning[CurChn] = 0x00;
  }

  for (CurChn = 0; CurChn < 8; CurChn++)
    AppPaths[CurChn] = NULL;
  AppPaths[0] = "";

  FileMode = 0xFF;

  PausePlay = false;

#ifdef _DEBUG
  if (sizeof(CHIP_AUDIO) != sizeof(CAUD_ATTR) * CHIP_COUNT) {
    fprintf(stderr, "Fatal Error! ChipAudio structure invalid!\n");
    getchar();
    exit(-1);
  }
  if (sizeof(CHIPS_OPTION) != sizeof(CHIP_OPTS) * CHIP_COUNT) {
    fprintf(stderr, "Fatal Error! ChipOpts structure invalid!\n");
    getchar();
    exit(-1);
  }
#endif

  return;
}

void VGMPlay_Init2(void) {


  StreamBufs[0x00] = (INT32 *)malloc(SMPL_BUFSIZE * sizeof(INT32));
  StreamBufs[0x01] = (INT32 *)malloc(SMPL_BUFSIZE * sizeof(INT32));

  if (CHIP_SAMPLE_RATE <= 0)
    CHIP_SAMPLE_RATE = SampleRate;
  
  return;
}

void VGMPlay_Deinit(void) {
  UINT8 CurChip;
  UINT8 CurCSet;
  CHIP_OPTS *TempCOpt;
  free(StreamBufs[0x00]);
  StreamBufs[0x00] = NULL;
  free(StreamBufs[0x01]);
  StreamBufs[0x01] = NULL;

  for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
    for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++) {
      TempCOpt = (CHIP_OPTS *)&ChipOpts[CurCSet] + CurChip;

      if (TempCOpt->Panning != NULL) {
        free(TempCOpt->Panning);
        TempCOpt->Panning = NULL;
      }
    }
  }

  return;
}

// Note: Caller must free the returned string.
char *FindFile(const char *FileName) {
  char *FullName;
  char **CurPath;
  UINT32 NameLen;
  UINT32 PathLen;
  UINT32 FullLen;
  FILE *hFile;

  NameLen = strlen(FileName);

  // go to end of the list + get size of largest path
  // (The first entry has the lowest priority.)
  PathLen = 0;
  CurPath = AppPaths;
  while (*CurPath != NULL) {
    FullLen = strlen(*CurPath);
    if (FullLen > PathLen)
      PathLen = FullLen;
    CurPath++;
  }
  CurPath--;
  FullLen = PathLen + NameLen;
  FullName = (char *)malloc(FullLen + 1);

  hFile = NULL;
  for (; CurPath >= AppPaths; CurPath--) {
    strcpy(FullName, *CurPath);
    strcat(FullName, FileName);

    hFile = fopen(FullName, "r");
    if (hFile != NULL)
      break;
  }

  if (hFile != NULL) {
    fclose(hFile);

    return FullName; // The caller has to free the string.
  } else {
    free(FullName);

    return NULL;
  }
}

INLINE UINT16 ReadLE16(const UINT8 *Data) {
  // read 16-Bit Word (Little Endian/Intel Byte Order)
#ifdef VGM_LITTLE_ENDIAN
  return *(UINT16 *)Data;
#else
  return (Data[0x01] << 8) | (Data[0x00] << 0);
#endif
}

INLINE UINT16 ReadBE16(const UINT8 *Data) {
  // read 16-Bit Word (Big Endian/Motorola Byte Order)
#ifdef VGM_BIG_ENDIAN
  return *(UINT16 *)Data;
#else
  return (Data[0x00] << 8) | (Data[0x01] << 0);
#endif
}

INLINE UINT32 ReadLE24(const UINT8 *Data) {
  // read 24-Bit Word (Little Endian/Intel Byte Order)
#ifdef VGM_LITTLE_ENDIAN
  return (*(UINT32 *)Data) & 0x00FFFFFF;
#else
  return (Data[0x02] << 16) | (Data[0x01] << 8) | (Data[0x00] << 0);
#endif
}

INLINE UINT32 ReadLE32(const UINT8 *Data) {
  // read 32-Bit Word (Little Endian/Intel Byte Order)
#ifdef VGM_LITTLE_ENDIAN
  return *(UINT32 *)Data;
#else
  return (Data[0x03] << 24) | (Data[0x02] << 16) | (Data[0x01] << 8) |
         (Data[0x00] << 0);
#endif
}

INLINE int gzgetLE16(gzFile hFile, UINT16 *RetValue) {
#ifdef VGM_LITTLE_ENDIAN
  return gzread(hFile, RetValue, 0x02);
#else
  int RetVal;
  UINT8 Data[0x02];

  RetVal = gzread(hFile, Data, 0x02);
  *RetValue = (Data[0x01] << 8) | (Data[0x00] << 0);
  return RetVal;
#endif
}

INLINE int gzgetLE32(gzFile hFile, UINT32 *RetValue) {
#ifdef VGM_LITTLE_ENDIAN
  return gzread(hFile, RetValue, 0x04);
#else
  int RetVal;
  UINT8 Data[0x04];

  RetVal = gzread(hFile, Data, 0x04);
  *RetValue = (Data[0x03] << 24) | (Data[0x02] << 16) | (Data[0x01] << 8) |
              (Data[0x00] << 0);
  return RetVal;
#endif
}

static UINT32 gcd(UINT32 x, UINT32 y) {
  UINT32 shift;
  UINT32 diff;

  // Thanks to Wikipedia for this algorithm
  // http://en.wikipedia.org/wiki/Binary_GCD_algorithm
  if (!x || !y)
    return x | y;

  for (shift = 0; ((x | y) & 1) == 0; shift++) {
    x >>= 1;
    y >>= 1;
  }

  while ((x & 1) == 0)
    x >>= 1;

  do {
    while ((y & 1) == 0)
      y >>= 1;

    if (x < y) {
      y -= x;
    } else {
      diff = x - y;
      x = y;
      y = diff;
    }
    y >>= 1;
  } while (y);

  return x << shift;
}

void PlayVGM(void) {
  UINT8 CurChip;
  INT32 TempSLng;

  // PausePlay = false;
  FadePlay = false;
  MasterVol = 1.0f;
  ForceVGMExec = false;
  AutoStopSkip = false;
  FadeStart = 0;
  ForceVGMExec = true;

  if (VGMHead.bytVolumeModifier <= VOLUME_MODIF_WRAP)
    TempSLng = VGMHead.bytVolumeModifier;
  else if (VGMHead.bytVolumeModifier == (VOLUME_MODIF_WRAP + 0x01))
    TempSLng = VOLUME_MODIF_WRAP - 0x100;
  else
    TempSLng = VGMHead.bytVolumeModifier - 0x100;
  VolumeLevelM = (float)(VolumeLevel * pow(2.0, TempSLng / (double)0x20));
  
  FinalVol = VolumeLevelM;

  if (!VGMMaxLoop) {
    VGMMaxLoopM = 0x00;
  } else {
    TempSLng = (VGMMaxLoop * VGMHead.bytLoopModifier + 0x08) / 0x10 -
               VGMHead.bytLoopBase;
    VGMMaxLoopM = (TempSLng >= 0x01) ? TempSLng : 0x01;
  }

  if (!VGMPbRate || !VGMHead.lngRate) {
    VGMPbRateMul = 1;
    VGMPbRateDiv = 1;
  } else {
    // I prefer small Multiplers and Dividers, as they're used very often
    TempSLng = gcd(VGMHead.lngRate, VGMPbRate);
    VGMPbRateMul = VGMHead.lngRate / TempSLng;
    VGMPbRateDiv = VGMPbRate / TempSLng;
  }
  VGMSmplRateMul = SampleRate * VGMPbRateMul;
  VGMSmplRateDiv = VGMSampleRate * VGMPbRateDiv;
  // same as above - to speed up the VGM <-> Playback calculation
  TempSLng = gcd(VGMSmplRateMul, VGMSmplRateDiv);
  VGMSmplRateMul /= TempSLng;
  VGMSmplRateDiv /= TempSLng;

  PlayingTime = 0;
  EndPlay = false;

  VGMPos = VGMHead.lngDataOffset;
  VGMSmplPos = 0;
  VGMSmplPlayed = 0;
  VGMEnd = false;
  VGMCurLoop = 0x00;
  PauseSmpls = (PauseTime * SampleRate + 500) / 1000;
  if (VGMPos >= VGMHead.lngEOFOffset)
    VGMEnd = true;

#ifdef CONSOLE_MODE
  memset(CmdList, 0x00, 0x100 * sizeof(UINT8));
#endif

  if (!PauseEmulate) {
      PauseStream(PausePlay);
  }

  Chips_GeneralActions(0x00); // Start chips
  // also does Reset (0x01), Muting Mask (0x10) and Panning (0x20)
}

void StopVGM(void) {

  Chips_GeneralActions(0x02); // Stop chips

  return;
}

void RestartVGM(void) {
  if (!VGMSmplPlayed)
    return;

  RestartPlaying();

  return;
}

void PauseVGM(bool Pause) {
  if (Pause == PausePlay)
    return;


  if (!PauseEmulate) {
    PauseStream(Pause);
  }
  PausePlay = Pause;

  return;
}

void SeekVGM(bool Relative, INT32 PlayBkSamples) {
  INT32 Samples;
  UINT32 LoopSmpls;

  if (Relative && !PlayBkSamples)
    return;

  LoopSmpls = VGMCurLoop * SampleVGM2Pbk_I(VGMHead.lngLoopSamples);
  if (!Relative)
    Samples = PlayBkSamples - (LoopSmpls + VGMSmplPlayed);
  else
    Samples = PlayBkSamples;

  if (Samples < 0) {
    Samples = LoopSmpls + VGMSmplPlayed + Samples;
    if (Samples < 0)
      Samples = 0;
    RestartPlaying();
  }

  ForceVGMExec = true;
  InterpretFile(Samples);
  ForceVGMExec = false;
#ifdef CONSOLE_MODE
  if (FadePlay && FadeStart)
    FadeStart += Samples;
#endif

  return;
}

void RefreshMuting(void) {
  Chips_GeneralActions(0x10); // set muting mask

  return;
}

void RefreshPanning(void) {
  Chips_GeneralActions(0x20); // set panning

  return;
}

void RefreshPlaybackOptions(void) {
  INT32 TempVol;
  UINT8 CurChip;
  CHIP_OPTS *TempCOpt1;
  CHIP_OPTS *TempCOpt2;

  if (VGMHead.bytVolumeModifier <= VOLUME_MODIF_WRAP)
    TempVol = VGMHead.bytVolumeModifier;
  else if (VGMHead.bytVolumeModifier == (VOLUME_MODIF_WRAP + 0x01))
    TempVol = VOLUME_MODIF_WRAP - 0x100;
  else
    TempVol = VGMHead.bytVolumeModifier - 0x100;
  VolumeLevelM = (float)(VolumeLevel * pow(2.0, TempVol / (double)0x20));

  FinalVol = VolumeLevelM * MasterVol;

  // PauseSmpls = (PauseTime * SampleRate + 500) / 1000;

  return;
}

UINT32 GetGZFileLength(const char *FileName) {
  FILE *hFile;
  UINT32 FileSize;

  hFile = fopen(FileName, "rb");
  if (hFile == NULL)
    return 0xFFFFFFFF;

  FileSize = GetGZFileLength_Internal(hFile);

  fclose(hFile);
  return FileSize;
}

static UINT32 GetGZFileLength_Internal(FILE *hFile) {
  UINT32 FileSize;
  UINT16 gzHead;
  size_t RetVal;

  RetVal = fread(&gzHead, 0x02, 0x01, hFile);
  if (RetVal >= 1) {
    gzHead = ReadBE16((UINT8 *)&gzHead);
    if (gzHead != 0x1F8B) {
      RetVal = 0; // no .gz signature - treat as normal file
    } else {
      // .gz File
      fseek(hFile, -4, SEEK_END);
      // Note: In the error case it falls back to fseek/ftell.
      RetVal = fread(&FileSize, 0x04, 0x01, hFile);
#ifndef VGM_LITTLE_ENDIAN
      FileSize = ReadLE32((UINT8 *)&FileSize);
#endif
    }
  }
  if (RetVal <= 0) {
    // normal file
    fseek(hFile, 0x00, SEEK_END);
    FileSize = ftell(hFile);
  }

  return FileSize;
}

bool OpenVGMFile(const char *FileName) {
  gzFile hFile;
  UINT32 FileSize;
  bool RetVal;

  FileSize = GetGZFileLength(FileName);

  hFile = gzopen(FileName, "rb");
  if (hFile == NULL)
    return false;

  RetVal = OpenVGMFile_Internal(hFile, FileSize);

  gzclose(hFile);
  return RetVal;
}

static bool OpenVGMFile_Internal(gzFile hFile, UINT32 FileSize) {
  UINT32 fccHeader;
  UINT32 CurPos;
  UINT32 HdrLimit;

  // gzseek(hFile, 0x00, SEEK_SET);
  gzrewind(hFile);
  gzgetLE32(hFile, &fccHeader);
  if (fccHeader != FCC_VGM)
    return false;

  if (FileMode != 0xFF)
    CloseVGMFile();

  FileMode = 0x00;
  VGMDataLen = FileSize;

  gzseek(hFile, 0x00, SEEK_SET);
  // gzrewind(hFile);
  ReadVGMHeader(hFile, &VGMHead);
  if (VGMHead.fccVGM != FCC_VGM) {
    fprintf(stderr, "VGM signature matched on the first read, but not on the "
                    "second one!\n");
    fprintf(stderr, "This is a known zlib bug where gzseek fails. Please "
                    "install a fixed zlib.\n");
    return false;
  }

  VGMSampleRate = 44100;
  if (!VGMDataLen)
    VGMDataLen = VGMHead.lngEOFOffset;
  if (!VGMHead.lngEOFOffset || VGMHead.lngEOFOffset > VGMDataLen) {
    fprintf(stderr, "Warning! Invalid EOF Offset 0x%02X! (should be: 0x%02X)\n",
            VGMHead.lngEOFOffset, VGMDataLen);
    VGMHead.lngEOFOffset = VGMDataLen;
  }
  if (VGMHead.lngLoopOffset && !VGMHead.lngLoopSamples) {
    // 0-Sample-Loops causes the program to hangs in the playback routine
    fprintf(stderr, "Warning! Ignored Zero-Sample-Loop!\n");
    VGMHead.lngLoopOffset = 0x00000000;
  }
  if (VGMHead.lngDataOffset < 0x00000040) {
    fprintf(stderr, "Warning! Invalid Data Offset 0x%02X!\n",
            VGMHead.lngDataOffset);
    VGMHead.lngDataOffset = 0x00000040;
  }

  memset(&VGMHeadX, 0x00, sizeof(VGM_HDR_EXTRA));
  memset(&VGMH_Extra, 0x00, sizeof(VGM_EXTRA));

  // Read Data
  VGMDataLen = VGMHead.lngEOFOffset;
  VGMData = (UINT8 *)malloc(VGMDataLen);
  if (VGMData == NULL)
    return false;
  // gzseek(hFile, 0x00, SEEK_SET);
  gzrewind(hFile);
  gzread(hFile, VGMData, VGMDataLen);

  // Read Extra Header Data
  if (VGMHead.lngExtraOffset) {
    UINT32 *TempPtr;

    CurPos = VGMHead.lngExtraOffset;
    TempPtr = (UINT32 *)&VGMHeadX;
    // Read Header Size
    VGMHeadX.DataSize = ReadLE32(&VGMData[CurPos]);
    if (VGMHeadX.DataSize > sizeof(VGM_HDR_EXTRA))
      VGMHeadX.DataSize = sizeof(VGM_HDR_EXTRA);
    HdrLimit = CurPos + VGMHeadX.DataSize;
    CurPos += 0x04;
    TempPtr++;

    // Read all relative offsets of this header and make them absolute.
    for (; CurPos < HdrLimit; CurPos += 0x04, TempPtr++) {
      *TempPtr = ReadLE32(&VGMData[CurPos]);
      if (*TempPtr)
        *TempPtr += CurPos;
    }

    ReadChipExtraData32(VGMHeadX.Chp2ClkOffset, &VGMH_Extra.Clocks);
    ReadChipExtraData16(VGMHeadX.ChpVolOffset, &VGMH_Extra.Volumes);
  }

  // Read GD3 Tag
  HdrLimit = ReadGD3Tag(hFile, VGMHead.lngGD3Offset, &VGMTag);
  if (HdrLimit == 0x10) {
    VGMHead.lngGD3Offset = 0x00000000;
    // return false;
  }
  if (!VGMHead.lngGD3Offset) {
    // replace all NULL pointers with empty strings
    VGMTag.strTrackNameE = MakeEmptyWStr();
    VGMTag.strTrackNameJ = MakeEmptyWStr();
    VGMTag.strGameNameE = MakeEmptyWStr();
    VGMTag.strGameNameJ = MakeEmptyWStr();
    VGMTag.strSystemNameE = MakeEmptyWStr();
    VGMTag.strSystemNameJ = MakeEmptyWStr();
    VGMTag.strAuthorNameE = MakeEmptyWStr();
    VGMTag.strAuthorNameJ = MakeEmptyWStr();
    VGMTag.strReleaseDate = MakeEmptyWStr();
  }

  return true;
}

static void ReadVGMHeader(gzFile hFile, VGM_HEADER *RetVGMHead) {
  VGM_HEADER CurHead;
  UINT32 CurPos;
  UINT32 HdrLimit;

  gzread(hFile, &CurHead, sizeof(VGM_HEADER));
#ifndef VGM_LITTLE_ENDIAN
  {
    UINT8 *TempPtr;

    // Warning: Lots of pointer casting ahead!
    for (CurPos = 0x00; CurPos < sizeof(VGM_HEADER); CurPos += 0x04) {
      TempPtr = (UINT8 *)&CurHead + CurPos;
      switch (CurPos) {
      case 0x28:
        // 0x28	[16-bit] SN76496 Feedback Mask
        // 0x2A	[ 8-bit] SN76496 Shift Register Width
        // 0x2B	[ 8-bit] SN76496 Flags
        *(UINT16 *)TempPtr = ReadLE16(TempPtr);
        break;
      case 0x78: // 78-7B [8-bit] AY8910 Type/Flags
      case 0x7C: // 7C-7F [8-bit] Volume/Loop Modifiers
      case 0x94: // 94-97 [8-bit] various flags
        break;
      default:
        // everything else is 32-bit
        *(UINT32 *)TempPtr = ReadLE32(TempPtr);
        break;
      }
    }
  }
#endif

  // Header preperations
  if (CurHead.lngVersion < 0x00000101) {
    CurHead.lngRate = 0;
  }
  if (CurHead.lngVersion < 0x00000110) {
    CurHead.shtPSG_Feedback = 0x0000;
    CurHead.bytPSG_SRWidth = 0x00;
    CurHead.lngHzYM2612 = CurHead.lngHzYM2413;
    CurHead.lngHzYM2151 = CurHead.lngHzYM2413;
  }
  if (CurHead.lngVersion < 0x00000150) {
    CurHead.lngDataOffset = 0x00000000;
    // If I would aim to be very strict, I would uncomment these few lines,
    // but I sometimes use v1.51 Flags with v1.50 for better compatibility.
    // (Some hyper-strict players refuse to play v1.51 files, even if there's
    //  no new chip used.)
    //}
    // if (CurHead.lngVersion < 0x00000151)
    //{
    CurHead.bytPSG_Flags = 0x00;
    CurHead.lngHzSPCM = 0x0000;
    CurHead.lngSPCMIntf = 0x00000000;
    // all others are zeroed by memset
  }

  if (CurHead.lngHzPSG) {
    if (!CurHead.shtPSG_Feedback)
      CurHead.shtPSG_Feedback = 0x0009;
    if (!CurHead.bytPSG_SRWidth)
      CurHead.bytPSG_SRWidth = 0x10;
  }

  // relative -> absolute addresses
  if (CurHead.lngEOFOffset)
    CurHead.lngEOFOffset += 0x00000004;
  if (CurHead.lngGD3Offset)
    CurHead.lngGD3Offset += 0x00000014;
  if (CurHead.lngLoopOffset)
    CurHead.lngLoopOffset += 0x0000001C;

  if (CurHead.lngVersion < 0x00000150)
    CurHead.lngDataOffset = 0x0000000C;
  // if (CurHead.lngDataOffset < 0x0000000C)
  //	CurHead.lngDataOffset = 0x0000000C;
  if (CurHead.lngDataOffset)
    CurHead.lngDataOffset += 0x00000034;

  CurPos = CurHead.lngDataOffset;
  // should actually check v1.51 (first real usage of DataOffset)
  // v1.50 is checked to support things like the Volume Modifiers in v1.50
  // files
  if (CurHead.lngVersion < 0x00000150 /*0x00000151*/)
    CurPos = 0x40;
  if (!CurPos)
    CurPos = 0x40;
  HdrLimit = sizeof(VGM_HEADER);
  if (HdrLimit > CurPos)
    memset((UINT8 *)&CurHead + CurPos, 0x00, HdrLimit - CurPos);

  if (!CurHead.bytLoopModifier)
    CurHead.bytLoopModifier = 0x10;

  if (CurHead.lngExtraOffset) {
    CurHead.lngExtraOffset += 0xBC;

    CurPos = CurHead.lngExtraOffset;
    if (CurPos < HdrLimit)
      memset((UINT8 *)&CurHead + CurPos, 0x00, HdrLimit - CurPos);
  }

  if (CurHead.lngGD3Offset >= CurHead.lngEOFOffset)
    CurHead.lngGD3Offset = 0x00;
  if (CurHead.lngLoopOffset >= CurHead.lngEOFOffset)
    CurHead.lngLoopOffset = 0x00;
  if (CurHead.lngDataOffset >= CurHead.lngEOFOffset)
    CurHead.lngDataOffset = 0x40;
  if (CurHead.lngExtraOffset >= CurHead.lngEOFOffset)
    CurHead.lngExtraOffset = 0x00;

  *RetVGMHead = CurHead;
  return;
}

static UINT8 ReadGD3Tag(gzFile hFile, UINT32 GD3Offset, GD3_TAG *RetGD3Tag) {
  UINT32 CurPos;
  UINT32 TempLng;
  UINT8 ResVal;

  ResVal = 0x00;

  // Read GD3 Tag
  if (GD3Offset) {
    gzseek(hFile, GD3Offset, SEEK_SET);
    gzgetLE32(hFile, &TempLng);
    if (TempLng != FCC_GD3) {
      GD3Offset = 0x00000000;
      ResVal = 0x10; // invalid GD3 offset
    }
  }

  if (RetGD3Tag == NULL)
    return ResVal;

  if (!GD3Offset) {
    RetGD3Tag->fccGD3 = 0x00000000;
    RetGD3Tag->lngVersion = 0x00000000;
    RetGD3Tag->lngTagLength = 0x00000000;
    RetGD3Tag->strTrackNameE = NULL;
    RetGD3Tag->strTrackNameJ = NULL;
    RetGD3Tag->strGameNameE = NULL;
    RetGD3Tag->strGameNameJ = NULL;
    RetGD3Tag->strSystemNameE = NULL;
    RetGD3Tag->strSystemNameJ = NULL;
    RetGD3Tag->strAuthorNameE = NULL;
    RetGD3Tag->strAuthorNameJ = NULL;
    RetGD3Tag->strReleaseDate = NULL;
  } else {


    CurPos = GD3Offset + 0x04;   // Save some back seeking, yay!
    RetGD3Tag->fccGD3 = TempLng; // (That costs lots of CPU in .gz files.)
    CurPos += gzgetLE32(hFile, &RetGD3Tag->lngVersion);
    CurPos += gzgetLE32(hFile, &RetGD3Tag->lngTagLength);

    TempLng = CurPos + RetGD3Tag->lngTagLength;
    RetGD3Tag->strTrackNameE = ReadWStrFromFile(hFile, &CurPos, TempLng);
    RetGD3Tag->strTrackNameJ = ReadWStrFromFile(hFile, &CurPos, TempLng);
    RetGD3Tag->strGameNameE = ReadWStrFromFile(hFile, &CurPos, TempLng);
    RetGD3Tag->strGameNameJ = ReadWStrFromFile(hFile, &CurPos, TempLng);
    RetGD3Tag->strSystemNameE = ReadWStrFromFile(hFile, &CurPos, TempLng);
    RetGD3Tag->strSystemNameJ = ReadWStrFromFile(hFile, &CurPos, TempLng);
    RetGD3Tag->strAuthorNameE = ReadWStrFromFile(hFile, &CurPos, TempLng);
    RetGD3Tag->strAuthorNameJ = ReadWStrFromFile(hFile, &CurPos, TempLng);
    RetGD3Tag->strReleaseDate = ReadWStrFromFile(hFile, &CurPos, TempLng);
  }

  return ResVal;
}

static void ReadChipExtraData32(UINT32 StartOffset,
                                VGMX_CHP_EXTRA32 *ChpExtra) {
  UINT32 CurPos;
  UINT8 CurChp;
  VGMX_CHIP_DATA32 *TempCD;

  if (!StartOffset || StartOffset >= VGMDataLen) {
    ChpExtra->ChipCnt = 0x00;
    ChpExtra->CCData = NULL;
    return;
  }

  CurPos = StartOffset;
  ChpExtra->ChipCnt = VGMData[CurPos];
  if (ChpExtra->ChipCnt)
    ChpExtra->CCData = (VGMX_CHIP_DATA32 *)malloc(sizeof(VGMX_CHIP_DATA32) *
                                                  ChpExtra->ChipCnt);
  else
    ChpExtra->CCData = NULL;
  CurPos++;

  for (CurChp = 0x00; CurChp < ChpExtra->ChipCnt; CurChp++) {
    TempCD = &ChpExtra->CCData[CurChp];
    TempCD->Type = VGMData[CurPos + 0x00];
    TempCD->Data = ReadLE32(&VGMData[CurPos + 0x01]);
    CurPos += 0x05;
  }

  return;
}

static void ReadChipExtraData16(UINT32 StartOffset,
                                VGMX_CHP_EXTRA16 *ChpExtra) {
  UINT32 CurPos;
  UINT8 CurChp;
  VGMX_CHIP_DATA16 *TempCD;

  if (!StartOffset || StartOffset >= VGMDataLen) {
    ChpExtra->ChipCnt = 0x00;
    ChpExtra->CCData = NULL;
    return;
  }

  CurPos = StartOffset;
  ChpExtra->ChipCnt = VGMData[CurPos];
  if (ChpExtra->ChipCnt)
    ChpExtra->CCData = (VGMX_CHIP_DATA16 *)malloc(sizeof(VGMX_CHIP_DATA16) *
                                                  ChpExtra->ChipCnt);
  else
    ChpExtra->CCData = NULL;
  CurPos++;

  for (CurChp = 0x00; CurChp < ChpExtra->ChipCnt; CurChp++) {
    TempCD = &ChpExtra->CCData[CurChp];
    TempCD->Type = VGMData[CurPos + 0x00];
    TempCD->Flags = VGMData[CurPos + 0x01];
    TempCD->Data = ReadLE16(&VGMData[CurPos + 0x02]);
    CurPos += 0x04;
  }

  return;
}

void CloseVGMFile(void) {
  if (FileMode == 0xFF)
    return;

  VGMHead.fccVGM = 0x00;
  free(VGMH_Extra.Clocks.CCData);
  VGMH_Extra.Clocks.CCData = NULL;
  free(VGMH_Extra.Volumes.CCData);
  VGMH_Extra.Volumes.CCData = NULL;
  free(VGMData);
  VGMData = NULL;

  if (FileMode == 0x00)
    FreeGD3Tag(&VGMTag);

  FileMode = 0xFF;

  return;
}

void FreeGD3Tag(GD3_TAG *TagData) {
  if (TagData == NULL)
    return;

  TagData->fccGD3 = 0x00;
  free(TagData->strTrackNameE);
  TagData->strTrackNameE = NULL;
  free(TagData->strTrackNameJ);
  TagData->strTrackNameJ = NULL;
  free(TagData->strGameNameE);
  TagData->strGameNameE = NULL;
  free(TagData->strGameNameJ);
  TagData->strGameNameJ = NULL;
  free(TagData->strSystemNameE);
  TagData->strSystemNameE = NULL;
  free(TagData->strSystemNameJ);
  TagData->strSystemNameJ = NULL;
  free(TagData->strAuthorNameE);
  TagData->strAuthorNameE = NULL;
  free(TagData->strAuthorNameJ);
  TagData->strAuthorNameJ = NULL;
  free(TagData->strReleaseDate);
  TagData->strReleaseDate = NULL;

  return;
}

static wchar_t *MakeEmptyWStr(void) {
  wchar_t *Str;

  Str = (wchar_t *)malloc(0x01 * sizeof(wchar_t));
  Str[0x00] = L'\0';

  return Str;
}

static wchar_t *ReadWStrFromFile(gzFile hFile, UINT32 *FilePos, UINT32 EOFPos) {
  UINT32 CurPos;
  wchar_t *TextStr;
  wchar_t *TempStr;
  UINT32 StrLen;
  UINT16 UnicodeChr;

  CurPos = *FilePos;
  if (CurPos >= EOFPos)
    return NULL;
  TextStr = (wchar_t *)malloc((EOFPos - CurPos) / 0x02 * sizeof(wchar_t));
  if (TextStr == NULL)
    return NULL;

  if ((UINT32)gztell(hFile) != CurPos)
    gzseek(hFile, CurPos, SEEK_SET);
  TempStr = TextStr - 1;
  StrLen = 0x00;
  do {
    TempStr++;
    gzgetLE16(hFile, &UnicodeChr);
    *TempStr = (wchar_t)UnicodeChr;
    CurPos += 0x02;
    StrLen++;
    if (CurPos >= EOFPos) {
      *TempStr = L'\0';
      break;
    }
  } while (*TempStr != L'\0');

  TextStr = (wchar_t *)realloc(TextStr, StrLen * sizeof(wchar_t));
  *FilePos = CurPos;

  return TextStr;
}

UINT32 GetVGMFileInfo(const char *FileName, VGM_HEADER *RetVGMHead,
                      GD3_TAG *RetGD3Tag) {
  gzFile hFile;
  UINT32 FileSize;
  UINT32 RetVal;

  FileSize = GetGZFileLength(FileName);

  hFile = gzopen(FileName, "rb");
  if (hFile == NULL)
    return 0x00;

  RetVal = GetVGMFileInfo_Internal(hFile, FileSize, RetVGMHead, RetGD3Tag);

  gzclose(hFile);
  return RetVal;
}

static UINT32 GetVGMFileInfo_Internal(gzFile hFile, UINT32 FileSize,
                                      VGM_HEADER *RetVGMHead,
                                      GD3_TAG *RetGD3Tag) {
  UINT32 fccHeader;
  UINT32 TempLng;
  VGM_HEADER TempHead;

  // gzseek(hFile, 0x00, SEEK_SET);
  gzrewind(hFile);
  gzgetLE32(hFile, &fccHeader);
  if (fccHeader != FCC_VGM)
    return 0x00;

  if (RetVGMHead == NULL && RetGD3Tag == NULL)
    return FileSize;

  // gzseek(hFile, 0x00, SEEK_SET);
  gzrewind(hFile);
  ReadVGMHeader(hFile, &TempHead);

  if (!TempHead.lngEOFOffset || TempHead.lngEOFOffset > FileSize)
    TempHead.lngEOFOffset = FileSize;
  if (TempHead.lngDataOffset < 0x00000040)
    TempHead.lngDataOffset = 0x00000040;


  if (RetVGMHead != NULL)
    *RetVGMHead = TempHead;

  // Read GD3 Tag
  if (RetGD3Tag != NULL)
    TempLng = ReadGD3Tag(hFile, TempHead.lngGD3Offset, RetGD3Tag);

  return FileSize;
}

INLINE UINT32 MulDivRound(UINT64 Number, UINT64 Numerator, UINT64 Denominator) {
  return (UINT32)((Number * Numerator + Denominator / 2) / Denominator);
}

UINT32 CalcSampleMSec(UINT64 Value, UINT8 Mode) {
  UINT32 SmplRate;
  UINT32 PbMul;
  UINT32 PbDiv;
  UINT32 RetVal;

  if (!(Mode & 0x02)) {
    SmplRate = SampleRate;
    PbMul = 1;
    PbDiv = 1;
  } else {
    SmplRate = VGMSampleRate;
    PbMul = VGMPbRateMul;
    PbDiv = VGMPbRateDiv;
  }

  switch (Mode & 0x01) {
  case 0x00:
    RetVal = MulDivRound(Value, (UINT64)1000 * PbMul, (UINT64)SmplRate * PbDiv);
    break;
  case 0x01:
    RetVal = MulDivRound(Value, (UINT64)SmplRate * PbDiv, (UINT64)1000 * PbMul);
    break;
  }

  return RetVal;
}

UINT32 CalcSampleMSecExt(UINT64 Value, UINT8 Mode, VGM_HEADER *FileHead) {
  UINT32 SmplRate;
  UINT32 PbMul;
  UINT32 PbDiv;
  UINT32 RetVal;

  if (!(Mode & 0x02)) {
    SmplRate = SampleRate;
    PbMul = 1;
    PbDiv = 1;
  } else {
    SmplRate = 44100;
    if (!VGMPbRate || !FileHead->lngRate) {
      PbMul = 1;
      PbDiv = 1;
    } else {
      PbMul = FileHead->lngRate;
      PbDiv = VGMPbRate;
    }
  }

  switch (Mode & 0x01) {
  case 0x00:
    RetVal = MulDivRound(Value, 1000 * PbMul, SmplRate * PbDiv);
    break;
  }

  return RetVal;
}

const char *GetChipName(UINT8 ChipID) {
  const char *CHIP_STRS[CHIP_COUNT] = {"SN76496", "YM2413", "YM2151", "YM3812",
                                       "YM3526",  "Y8950",  "YMF262", "YMF278B",
                                       "AY8910",  "K051649"};


  if (ChipID < CHIP_COUNT)
    return CHIP_STRS[ChipID];
  else
    return NULL;
}

const char *GetAccurateChipName(UINT8 ChipID, UINT8 SubType) {
  const char *RetStr;

  if ((ChipID & 0x7F) >= CHIP_COUNT)
    return NULL;

  RetStr = NULL;
  switch (ChipID & 0x7F) {
  case 0x00:
    if (!(ChipID & 0x80)) {
      switch (SubType) {
      case 0x01:
        RetStr = "SN76489";
        break;
      case 0x02:
        RetStr = "SN76489A";
        break;
      case 0x03:
        RetStr = "SN76494";
        break;
      case 0x04:
        RetStr = "SN76496";
        break;
      case 0x05:
        RetStr = "SN94624";
        break;
      case 0x06:
        RetStr = "SEGA PSG";
        break;
      case 0x07:
        RetStr = "NCR8496";
        break;
      case 0x08:
        RetStr = "PSSJ-3";
        break;
      default:
        RetStr = "SN76496";
        break;
      }
    } else {
      RetStr = "T6W28";
    }
    break;
  case 0x01: // YM2413
    if (ChipID & 0x80)
      RetStr = "VRC7";
    else
      RetStr = "YM2413";
    break;
  case 0x02: // YM2151
    RetStr = "YM2151";
    break;
  case 0x03: // YM3812
    RetStr = "YM3812";
    break;
  case 0x04: // YM3526
    RetStr = "YM3526";
    break;
  case 0x05: // Y8950
    RetStr = "Y8950";
    break;
  case 0x06: // YMF262
    RetStr = "YMF262";
    break;
  case 0x07: // YMF278B
    RetStr = "YMF278B";
    break;
  case 0x08: // AY8910
    switch (SubType) {
    case 0x00:
      RetStr = "AY-3-8910";
      break;
    case 0x01:
      RetStr = "AY-3-8912";
      break;
    case 0x02:
      RetStr = "AY-3-8913";
      break;
    case 0x03:
      RetStr = "AY8930";
      break;
    case 0x04:
      RetStr = "AY-3-8914";
      break;
    case 0x10:
      RetStr = "YM2149";
      break;
    case 0x11:
      RetStr = "YM3439";
      break;
    case 0x12:
      RetStr = "YMZ284";
      break;
    case 0x13:
      RetStr = "YMZ294";
      break;
    }
    break;
  case 0x09: // K051649 (SCC)
    if (!(ChipID & 0x80))
      RetStr = "K051649";
    else
      RetStr = "K052539";
    break;
  case 0x19:
    if (!(ChipID & 0x80))
      RetStr = "K051649";
    else
      RetStr = "K052539";
    break;
  }
  // catch all default-cases
  if (RetStr == NULL)
    RetStr = GetChipName(ChipID & 0x7F);

  return RetStr;
}

UINT32 GetChipClock(VGM_HEADER *FileHead, UINT8 ChipID, UINT8 *RetSubType) {
  UINT32 Clock;
  UINT8 SubType;
  UINT8 CurChp;
  bool AllowBit31;

  SubType = 0x00;
  AllowBit31 = 0x00;
  switch (ChipID & 0x7F) {
  case 0x00: // SN76496 (PSG)
    Clock = FileHead->lngHzPSG;
    break;
  case 0x01: // YM2413
    Clock = FileHead->lngHzYM2413;
    break;
  case 0x02: // YM2151
    Clock = FileHead->lngHzYM2151;
    break;
  case 0x03: // YM3812
    Clock = FileHead->lngHzYM3812;
    AllowBit31 = 0x01; // Dual OPL2, panned to the L/R speakers
    break;
  case 0x04: // YM3526
    Clock = FileHead->lngHzYM3526;
    break;
  case 0x05: // Y8950
    Clock = FileHead->lngHzY8950;
    break;
  case 0x06: // YMF262
    Clock = FileHead->lngHzYMF262;
    break;
  case 0x07: // YMF278B
    Clock = FileHead->lngHzYMF278B;
    break;
  case 0x08: // AY8910
    Clock = FileHead->lngHzAY8910;
    SubType = FileHead->bytAYType;
    break;
  case 0x09: // K051649 (SCC)
    Clock = FileHead->lngHzK051649;
    AllowBit31 = 0x01; // SCC/SCC+ Bit
    break;
  default:
    return 0;
  }
  if (ChipID & 0x80) {
    VGMX_CHP_EXTRA32 *TempCX;

    if (!(Clock & 0x40000000))
      return 0;

    const UINT8 INDEX_TO_ID[CHIP_COUNT] = {0x00, 0x01, 0x03, 0x09, 0x0A,
                                           0x0B, 0x0C, 0x0D, 0x12, 0x19};
    UINT8 OrigType = INDEX_TO_ID[ChipID & 0x7F];

    ChipID &= 0x7F;
    TempCX = &VGMH_Extra.Clocks;
    for (CurChp = 0x00; CurChp < TempCX->ChipCnt; CurChp++) {
      if (TempCX->CCData[CurChp].Type == OrigType) {
        if (TempCX->CCData[CurChp].Data)
          Clock = TempCX->CCData[CurChp].Data;
        break;
      }
    }
  }

  if (RetSubType != NULL)
    *RetSubType = SubType;
  if (AllowBit31)
    return Clock & 0xBFFFFFFF;
  else
    return Clock & 0x3FFFFFFF;
}

static UINT16 GetChipVolume(VGM_HEADER *FileHead, UINT8 ChipID, UINT8 ChipNum,
                            UINT8 ChipCnt) {
  const UINT16 CHIP_VOLS[CHIP_COUNT] = {0x80,  0x200, 0x100, 0x100,
                                        0x100, 0x100, 0x100, 0x100,
                                        0x100, 0xA0}; // MSX Chips 0-9
  UINT16 Volume;
  UINT8 CurChp;
  VGMX_CHP_EXTRA16 *TempCX;
  VGMX_CHIP_DATA16 *TempCD;

  Volume = CHIP_VOLS[ChipID & 0x7F];
  switch (ChipID & 0x7F) {
  case 0x00: // SN76496
    // if T6W28, set Volume Divider to 01
    // Correctly call GetChipClock with our 10-chip index
    if (GetChipClock(&VGMHead, (ChipID & 0x80) | 0x00, NULL) & 0x80000000) {
      // The T6W28 consists of 2 "half" chips.
      ChipNum = 0x01;
      ChipCnt = 0x01;
    }
    break;
  case 0x08: // AY8910 (bit 7 might be set if it's sub-chip of YM2203/etc, but
             // we don't support those anymore as MSX-only)
    if (ChipID & 0x80)
      Volume /= 2;
    break;
  }
  if (ChipCnt > 1)
    Volume /= ChipCnt;

  // Map our 0-9 index back to original chip ID for searching extra volumes
  const UINT8 INDEX_TO_ID[CHIP_COUNT] = {0x00, 0x01, 0x03, 0x09, 0x0A,
                                         0x0B, 0x0C, 0x0D, 0x12, 0x19};
  UINT8 OrigType = INDEX_TO_ID[ChipID & 0x7F];

  TempCX = &VGMH_Extra.Volumes;
  TempCD = TempCX->CCData;
  for (CurChp = 0x00; CurChp < TempCX->ChipCnt; CurChp++, TempCD++) {
    if (TempCD->Type == OrigType && (TempCD->Flags & 0x01) == ChipNum) {
      // Bit 15 - absolute/relative volume
      //	0 - absolute
      //	1 - relative (0x0100 = 1.0, 0x80 = 0.5, etc.)
      if (TempCD->Data & 0x8000)
        Volume = (Volume * (TempCD->Data & 0x7FFF) + 0x80) >> 8;
      else {
        Volume = TempCD->Data;
        if ((ChipID & 0x80) && DoubleSSGVol)
          Volume *= 2;
      }
      break;
    }
  }

  return Volume;
}

static void RestartPlaying(void) {
  Interpreting = true; // Avoid any Thread-Call

  VGMPos = VGMHead.lngDataOffset;
  VGMSmplPos = 0;
  VGMSmplPlayed = 0;
  VGMEnd = false;
  EndPlay = false;
  VGMCurLoop = 0x00;
  PauseSmpls = (PauseTime * SampleRate + 500) / 1000;

  Chips_GeneralActions(0x01); // Reset Chips
  // also does Muting Mask (0x10) and Panning (0x20)



  // Last95 vars removed
  Interpreting = false;
  ForceVGMExec = true;
  IsVGMInit = true;
  InterpretFile(0);
  IsVGMInit = false;
  ForceVGMExec = false;
#ifndef CONSOLE_MODE
  FadePlay = false;
  MasterVol = 1.0f;
  FadeStart = 0;
  FinalVol = VolumeLevelM;
  PlayingTime = 0;
#endif

  return;
}

static void Chips_GeneralActions(UINT8 Mode) {
  UINT32 AbsVol;
  // UINT16 ChipVol;
  CAUD_ATTR *CAA;
  CHIP_OPTS *COpt;
  UINT8 ChipCnt;
  UINT8 CurChip;
  UINT8 CurCSet; // Chip Set
  UINT32 MaskVal;
  UINT32 ChipClk;

  switch (Mode) {
  case 0x00: // Start Chips
    for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
      CAA = (CAUD_ATTR *)&ChipAudio[CurCSet];
      for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++, CAA++) {
        CAA->SmpRate = 0x00;
        CAA->Volume = 0x00;
        CAA->ChipType = 0xFF;
        CAA->ChipID = CurCSet;
        CAA->Resampler = 0x00;
        CAA->StreamUpdate = &null_update;
        CAA->Paired = NULL;
      }
      CAA = CA_Paired[CurCSet];
      for (CurChip = 0x00; CurChip < 0x03; CurChip++, CAA++) {
        CAA->SmpRate = 0x00;
        CAA->Volume = 0x00;
        CAA->ChipType = 0xFF;
        CAA->ChipID = CurCSet;
        CAA->Resampler = 0x00;
        CAA->StreamUpdate = &null_update;
        CAA->Paired = NULL;
      }
    }

    // Initialize Sound Chips
    AbsVol = 0x00;
    if (VGMHead.lngHzPSG) {
      // ChipVol = UseFM ? 0x00 : 0x80;
      sn764xx_set_emu_core(ChipOpts[0x00].SN76496.EmuCore);
      ChipOpts[0x01].SN76496.EmuCore = ChipOpts[0x00].SN76496.EmuCore;

      ChipCnt = (VGMHead.lngHzPSG & 0x40000000) ? 0x02 : 0x01;
      for (CurChip = 0x00; CurChip < ChipCnt; CurChip++) {
        CAA = &ChipAudio[CurChip].SN76496;
        CAA->ChipType = 0x00;

        ChipClk = GetChipClock(&VGMHead, (CurChip << 7) | CAA->ChipType, NULL);
        ChipClk &= ~0x80000000;
        ChipClk |= VGMHead.lngHzPSG & ((CurChip & 0x01) << 31);
        CAA->SmpRate = device_start_sn764xx(
            CurChip, ChipClk, VGMHead.bytPSG_SRWidth, VGMHead.shtPSG_Feedback,
            (VGMHead.bytPSG_Flags & 0x02) >> 1,
            (VGMHead.bytPSG_Flags & 0x04) >> 2,
            (VGMHead.bytPSG_Flags & 0x08) >> 3,
            (VGMHead.bytPSG_Flags & 0x01) >> 0);
        CAA->StreamUpdate = &sn764xx_stream_update;

        CAA->Volume =
            GetChipVolume(&VGMHead, CAA->ChipType, CurChip, ChipCnt);
        if (!CurChip || !(ChipClk & 0x80000000))
          AbsVol += CAA->Volume;
      }
      if (VGMHead.lngHzPSG & 0x80000000)
        ChipCnt = 0x01;
    }
    if (VGMHead.lngHzYM2413) {
      // ChipVol = UseFM ? 0x00 : 0x200/*0x155*/;
      ym2413_set_emu_core(ChipOpts[0x00].YM2413.EmuCore);
      ChipOpts[0x01].YM2413.EmuCore = ChipOpts[0x00].YM2413.EmuCore;

      ChipCnt = (VGMHead.lngHzYM2413 & 0x40000000) ? 0x02 : 0x01;
      for (CurChip = 0x00; CurChip < ChipCnt; CurChip++) {
        CAA = &ChipAudio[CurChip].YM2413;
        CAA->ChipType = 0x01;

        ChipClk = GetChipClock(&VGMHead, (CurChip << 7) | CAA->ChipType, NULL);
        CAA->SmpRate = device_start_ym2413(CurChip, ChipClk);
        CAA->StreamUpdate = &ym2413_stream_update;

        CAA->Volume =
            GetChipVolume(&VGMHead, CAA->ChipType, CurChip, ChipCnt);
        // WHY has this chip such a low volume???
        // AbsVol += (CAA->Volume + 1) * 3 / 4;
        AbsVol += CAA->Volume / 2;
      }
    }

    if (VGMHead.lngHzYM2151) {
      // ChipVol = 0x100;
      ym2151_set_emu_core(ChipOpts[0x00].YM2151.EmuCore);
      ChipCnt = (VGMHead.lngHzYM2151 & 0x40000000) ? 0x02 : 0x01;
      for (CurChip = 0x00; CurChip < ChipCnt; CurChip++) {
        CAA = &ChipAudio[CurChip].YM2151;
        CAA->ChipType = 0x02;

        ChipClk = GetChipClock(&VGMHead, (CurChip << 7) | CAA->ChipType, NULL);
        CAA->SmpRate = device_start_ym2151(CurChip, ChipClk);
        CAA->StreamUpdate = &ym2151_update;

        CAA->Volume = GetChipVolume(&VGMHead, CAA->ChipType, CurChip, ChipCnt);
        AbsVol += CAA->Volume;
      }
    }

    if (VGMHead.lngHzYM3812) {
      // ChipVol = UseFM ? 0x00 : 0x100;
      ym3812_set_emu_core(ChipOpts[0x00].YM3812.EmuCore);
      ChipOpts[0x01].YM3812.EmuCore = ChipOpts[0x00].YM3812.EmuCore;

      ChipCnt = (VGMHead.lngHzYM3812 & 0x40000000) ? 0x02 : 0x01;
      for (CurChip = 0x00; CurChip < ChipCnt; CurChip++) {
        CAA = &ChipAudio[CurChip].YM3812;
        CAA->ChipType = 0x03;

        ChipClk = GetChipClock(&VGMHead, (CurChip << 7) | CAA->ChipType, NULL);
        CAA->SmpRate = device_start_ym3812(CurChip, ChipClk);
        CAA->StreamUpdate =
            (ChipClk & 0x80000000) ? dual_opl2_stereo : ym3812_stream_update;

        CAA->Volume =
            GetChipVolume(&VGMHead, CAA->ChipType, CurChip, ChipCnt);
        if (!CurChip || !(ChipClk & 0x80000000))
          AbsVol += CAA->Volume * 2;
      }
    }
    if (VGMHead.lngHzYM3526) {
      // ChipVol = UseFM ? 0x00 : 0x100;
      ChipCnt = (VGMHead.lngHzYM3526 & 0x40000000) ? 0x02 : 0x01;
      for (CurChip = 0x00; CurChip < ChipCnt; CurChip++) {
        CAA = &ChipAudio[CurChip].YM3526;
        CAA->ChipType = 0x04;

        ChipClk = GetChipClock(&VGMHead, (CurChip << 7) | CAA->ChipType, NULL);
        CAA->SmpRate = device_start_ym3526(CurChip, ChipClk);
        CAA->StreamUpdate = &ym3526_stream_update;

        CAA->Volume =
            GetChipVolume(&VGMHead, CAA->ChipType, CurChip, ChipCnt);
        AbsVol += CAA->Volume * 2;
      }
    }
    if (VGMHead.lngHzY8950) {
      // ChipVol = UseFM ? 0x00 : 0x100;
      ChipCnt = (VGMHead.lngHzY8950 & 0x40000000) ? 0x02 : 0x01;
      for (CurChip = 0x00; CurChip < ChipCnt; CurChip++) {
        CAA = &ChipAudio[CurChip].Y8950;
        CAA->ChipType = 0x05;

        ChipClk = GetChipClock(&VGMHead, (CurChip << 7) | CAA->ChipType, NULL);
        CAA->SmpRate = device_start_y8950(CurChip, ChipClk);
        CAA->StreamUpdate = &y8950_stream_update;

        CAA->Volume =
            GetChipVolume(&VGMHead, CAA->ChipType, CurChip, ChipCnt);
        AbsVol += CAA->Volume * 2;
      }
    }
    if (VGMHead.lngHzYMF262) {
      // ChipVol = UseFM ? 0x00 : 0x100;
      ymf262_set_emu_core(ChipOpts[0x00].YMF262.EmuCore);
      ChipOpts[0x01].YMF262.EmuCore = ChipOpts[0x00].YMF262.EmuCore;

      ChipCnt = (VGMHead.lngHzYMF262 & 0x40000000) ? 0x02 : 0x01;
      for (CurChip = 0x00; CurChip < ChipCnt; CurChip++) {
        CAA = &ChipAudio[CurChip].YMF262;
        CAA->ChipType = 0x06;

        ChipClk = GetChipClock(&VGMHead, (CurChip << 7) | CAA->ChipType, NULL);
        CAA->SmpRate = device_start_ymf262(CurChip, ChipClk);
        CAA->StreamUpdate = &ymf262_stream_update;

        CAA->Volume =
            GetChipVolume(&VGMHead, CAA->ChipType, CurChip, ChipCnt);
        AbsVol += CAA->Volume * 2;
      }
    }
    if (VGMHead.lngHzYMF278B) {
      // ChipVol = 0x100;
      ChipCnt = (VGMHead.lngHzYMF278B & 0x40000000) ? 0x02 : 0x01;
      for (CurChip = 0x00; CurChip < ChipCnt; CurChip++) {
        CAA = &ChipAudio[CurChip].YMF278B;
        CAA->ChipType = 0x07;

        ChipClk = GetChipClock(&VGMHead, (CurChip << 7) | CAA->ChipType, NULL);
        CAA->SmpRate = device_start_ymf278b(CurChip, ChipClk);
        CAA->StreamUpdate = &ymf278b_pcm_update;

        CAA->Volume = GetChipVolume(&VGMHead, CAA->ChipType, CurChip, ChipCnt);
        AbsVol += CAA->Volume; // good as long as it only uses WaveTable Synth
      }
    }

    if (VGMHead.lngHzAY8910) {
      // ChipVol = 0x100;
      ayxx_set_emu_core(ChipOpts[0x00].AY8910.EmuCore);
      ChipOpts[0x01].AY8910.EmuCore = ChipOpts[0x00].AY8910.EmuCore;

      ChipCnt = (VGMHead.lngHzAY8910 & 0x40000000) ? 0x02 : 0x01;
      for (CurChip = 0x00; CurChip < ChipCnt; CurChip++) {
        CAA = &ChipAudio[CurChip].AY8910;
        CAA->ChipType = 0x08;

        ChipClk = GetChipClock(&VGMHead, (CurChip << 7) | CAA->ChipType, NULL);
        CAA->SmpRate = device_start_ayxx(CurChip, ChipClk, VGMHead.bytAYType,
                                         VGMHead.bytAYFlag);
        CAA->StreamUpdate = &ayxx_stream_update;

        CAA->Volume =
            GetChipVolume(&VGMHead, CAA->ChipType, CurChip, ChipCnt);
        AbsVol += CAA->Volume * 2;
      }
    }

    if (VGMHead.lngHzK051649) {
      // ChipVol = 0xA0;
      ChipCnt = (VGMHead.lngHzK051649 & 0x40000000) ? 0x02 : 0x01;
      for (CurChip = 0x00; CurChip < ChipCnt; CurChip++) {
        CAA = &ChipAudio[CurChip].K051649;
        CAA->ChipType = 0x09;

        ChipClk = GetChipClock(&VGMHead, (CurChip << 7) | CAA->ChipType, NULL);
        CAA->SmpRate = device_start_k051649(CurChip, ChipClk);
        CAA->StreamUpdate = &k051649_update;

        CAA->Volume = GetChipVolume(&VGMHead, CAA->ChipType, CurChip, ChipCnt);
        AbsVol += CAA->Volume;
      }
    }

    // Initialize DAC Control and PCM Bank
    DacCtrlUsed = 0x00;
    // memset(DacCtrlUsg, 0x00, 0x01 * 0xFF);
    for (CurChip = 0x00; CurChip < 0xFF; CurChip++) {
      DacCtrl[CurChip].Enable = false;
    }
    // memset(DacCtrl, 0x00, sizeof(DACCTRL_DATA) * 0xFF);

    memset(PCMBank, 0x00, sizeof(VGM_PCM_BANK) * PCM_BANK_COUNT);
    memset(&PCMTbl, 0x00, sizeof(PCMBANK_TBL));

    // Reset chips
    Chips_GeneralActions(0x01);

    while (AbsVol < 0x200 && AbsVol) {
      for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
        CAA = (CAUD_ATTR *)&ChipAudio[CurCSet];
        for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++, CAA++)
          CAA->Volume *= 2;
        CAA = CA_Paired[CurCSet];
        for (CurChip = 0x00; CurChip < 0x03; CurChip++, CAA++)
          CAA->Volume *= 2;
      }
      AbsVol *= 2;
    }
    while (AbsVol > 0x300) {
      for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
        CAA = (CAUD_ATTR *)&ChipAudio[CurCSet];
        for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++, CAA++)
          CAA->Volume /= 2;
        CAA = CA_Paired[CurCSet];
        for (CurChip = 0x00; CurChip < 0x03; CurChip++, CAA++)
          CAA->Volume /= 2;
      }
      AbsVol /= 2;
    }

    // Initialize Resampler
    for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
      CAA = (CAUD_ATTR *)&ChipAudio[CurCSet];
      for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++, CAA++)
        SetupResampler(CAA);

      CAA = CA_Paired[CurCSet];
      for (CurChip = 0x00; CurChip < 0x03; CurChip++, CAA++)
        SetupResampler(CAA);
    }

    GeneralChipLists();
    break;
  case 0x01: // Reset chips
    for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
      CAA = (CAUD_ATTR *)&ChipAudio[CurCSet];
      for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++, CAA++) {
        if (CAA->ChipType == 0xFF) // chip unused
          continue;
        else if (CAA->ChipType == 0x00)
          device_reset_sn764xx(CurCSet);
        else if (CAA->ChipType == 0x01)
          device_reset_ym2413(CurCSet);
        else if (CAA->ChipType == 0x02)
          device_reset_ym2151(CurCSet);
        else if (CAA->ChipType == 0x03)
          device_reset_ym3812(CurCSet);
        else if (CAA->ChipType == 0x04)
          device_reset_ym3526(CurCSet);
        else if (CAA->ChipType == 0x05)
          device_reset_y8950(CurCSet);
        else if (CAA->ChipType == 0x06)
          device_reset_ymf262(CurCSet);
        else if (CAA->ChipType == 0x07)
          device_reset_ymf278b(CurCSet);
        else if (CAA->ChipType == 0x08)
          device_reset_ayxx(CurCSet);
        else if (CAA->ChipType == 0x09)
          device_reset_k051649(CurCSet);

      } // end for CurChip

    } // end for CurCSet

    Chips_GeneralActions(0x10); // set muting mask
    Chips_GeneralActions(0x20); // set panning

    for (CurChip = 0x00; CurChip < DacCtrlUsed; CurChip++) {
      CurCSet = DacCtrlUsg[CurChip];
      device_reset_daccontrol(CurCSet);
      // DacCtrl[CurCSet].Enable = false;
    }
    // DacCtrlUsed = 0x00;
    // memset(DacCtrlUsg, 0x00, 0x01 * 0xFF);

    for (CurChip = 0x00; CurChip < PCM_BANK_COUNT; CurChip++) {
      // reset PCM Bank, but not the data
      // (this way I don't need to decompress the data again when restarting)
      PCMBank[CurChip].DataPos = 0x00000000;
      PCMBank[CurChip].BnkPos = 0x00000000;
    }
    PCMTbl.EntryCount = 0x00;
    break;
  case 0x02: // Stop chips
    for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
      CAA = (CAUD_ATTR *)&ChipAudio[CurCSet];
      for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++, CAA++) {
        if (CAA->ChipType == 0xFF) // chip unused
          continue;
        else if (CAA->ChipType == 0x00)
          device_stop_sn764xx(CurCSet);
        else if (CAA->ChipType == 0x01)
          device_stop_ym2413(CurCSet);
        else if (CAA->ChipType == 0x02)
          device_stop_ym2151(CurCSet);
        else if (CAA->ChipType == 0x03)
          device_stop_ym3812(CurCSet);
        else if (CAA->ChipType == 0x04)
          device_stop_ym3526(CurCSet);
        else if (CAA->ChipType == 0x05)
          device_stop_y8950(CurCSet);
        else if (CAA->ChipType == 0x06)
          device_stop_ymf262(CurCSet);
        else if (CAA->ChipType == 0x07)
          device_stop_ymf278b(CurCSet);
        else if (CAA->ChipType == 0x08)
          device_stop_ayxx(CurCSet);
        else if (CAA->ChipType == 0x09)
          device_stop_k051649(CurCSet);

        CAA->ChipType = 0xFF; // mark as "unused"
      } // end for CurChip

    } // end for CurCSet

    for (CurChip = 0x00; CurChip < DacCtrlUsed; CurChip++) {
      CurCSet = DacCtrlUsg[CurChip];
      device_stop_daccontrol(CurCSet);
      DacCtrl[CurCSet].Enable = false;
    }
    DacCtrlUsed = 0x00;

    for (CurChip = 0x00; CurChip < PCM_BANK_COUNT; CurChip++) {
      free(PCMBank[CurChip].Bank);
      free(PCMBank[CurChip].Data);
    }
    // memset(PCMBank, 0x00, sizeof(VGM_PCM_BANK) * PCM_BANK_COUNT);
    free(PCMTbl.Entries);
    // memset(&PCMTbl, 0x00, sizeof(PCMBANK_TBL));
    break;
  case 0x10: // Set Muting Mask
    for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
      CAA = (CAUD_ATTR *)&ChipAudio[CurCSet];
      for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++, CAA++) {
        if (CAA->ChipType == 0xFF) // chip unused
          continue;
        else if (CAA->ChipType == 0x00)
          sn764xx_set_mute_mask(CurCSet, ChipOpts[CurCSet].SN76496.ChnMute1);
        else if (CAA->ChipType == 0x01)
          ym2413_set_mute_mask(CurCSet, ChipOpts[CurCSet].YM2413.ChnMute1);
        else if (CAA->ChipType == 0x02)
          ym2151_set_mute_mask(CurCSet, ChipOpts[CurCSet].YM2151.ChnMute1);
        else if (CAA->ChipType == 0x03)
          ym3812_set_mute_mask(CurCSet, ChipOpts[CurCSet].YM3812.ChnMute1);
        else if (CAA->ChipType == 0x04)
          ym3526_set_mute_mask(CurCSet, ChipOpts[CurCSet].YM3526.ChnMute1);
        else if (CAA->ChipType == 0x05)
          y8950_set_mute_mask(CurCSet, ChipOpts[CurCSet].Y8950.ChnMute1);
        else if (CAA->ChipType == 0x06)
          ymf262_set_mute_mask(CurCSet, ChipOpts[CurCSet].YMF262.ChnMute1);
        else if (CAA->ChipType == 0x07)
          ymf278b_set_mute_mask(CurCSet, ChipOpts[CurCSet].YMF278B.ChnMute1,
                                ChipOpts[CurCSet].YMF278B.ChnMute2);
        else if (CAA->ChipType == 0x08)
          ayxx_set_mute_mask(CurCSet, ChipOpts[CurCSet].AY8910.ChnMute1);
        else if (CAA->ChipType == 0x09)
          k051649_set_mute_mask(CurCSet, ChipOpts[CurCSet].K051649.ChnMute1);

      } // end for CurChip

    } // end for CurCSet
    break;
  case 0x20: // Set Panning
    for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
      CAA = (CAUD_ATTR *)&ChipAudio[CurCSet];
      for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++, CAA++) {
        if (CAA->ChipType == 0xFF) // chip unused
          continue;
        else if (CAA->ChipType == 0x00)
          sn764xx_set_panning(CurCSet, ChipOpts[CurCSet].SN76496.Panning);
        else if (CAA->ChipType == 0x01)
          ym2413_set_panning(CurCSet, ChipOpts[CurCSet].YM2413.Panning);
      } // end for CurChip

    } // end for CurCSet
    break;
  }

  return;
}

INLINE INT32 SampleVGM2Pbk_I(INT32 SampleVal) {
  return (INT32)((INT64)SampleVal * VGMSmplRateMul / VGMSmplRateDiv);
}

INLINE INT32 SamplePbk2VGM_I(INT32 SampleVal) {
  return (INT32)((INT64)SampleVal * VGMSmplRateDiv / VGMSmplRateMul);
}

INT32 SampleVGM2Playback(INT32 SampleVal) {
  return (INT32)((INT64)SampleVal * VGMSmplRateMul / VGMSmplRateDiv);
}

INT32 SamplePlayback2VGM(INT32 SampleVal) {
  return (INT32)((INT64)SampleVal * VGMSmplRateDiv / VGMSmplRateMul);
}

// static bool SetMuteControl(HMIXEROBJ hmixer, MIXERCONTROL* mxc, bool mute)
static bool SetMuteControl(bool mute) {
#ifdef MIXER_MUTING

  UINT16 mix_vol;
  int RetVal;

  ioctl(hmixer, MIXER_READ(SOUND_MIXER_SYNTH), &mix_vol);
  if (mix_vol)
    mixer_vol = mix_vol;
  mix_vol = mute ? 0x0000 : mixer_vol;

  RetVal = ioctl(hmixer, MIXER_WRITE(SOUND_MIXER_SYNTH), &mix_vol);

  return !RetVal;

#else // #indef MIXER_MUTING
  float TempVol;

  TempVol = MasterVol;
  if (TempVol > 0.0f)
    VolumeBak = TempVol;

  MasterVol = mute ? 0.0f : VolumeBak;
  FinalVol = VolumeLevelM * MasterVol;
  RefreshVolume();

  return true;
#endif
}

static void InterpretFile(UINT32 SampleCount) {
  UINT32 TempLng;
  UINT8 CurChip;

  while (Interpreting)
    Sleep(1);

  if (DacCtrlUsed && SampleCount > 1) // handle skipping
  {
    for (CurChip = 0x00; CurChip < DacCtrlUsed; CurChip++) {
      daccontrol_update(DacCtrlUsg[CurChip], SampleCount - 1);
    }
  }

  Interpreting = true;
  if (!FileMode)
    InterpretVGM(SampleCount);


  if (DacCtrlUsed && SampleCount) {
    for (CurChip = 0x00; CurChip < DacCtrlUsed; CurChip++) {
      daccontrol_update(DacCtrlUsg[CurChip], 1);
    }
  }

  if (AutoStopSkip && SampleCount) {
    StopSkipping();
    AutoStopSkip = false;
  }

  if (!PausePlay || ForceVGMExec)
    VGMSmplPlayed += SampleCount;
  PlayingTime += SampleCount;



  Interpreting = false;

  return;
}

static void AddPCMData(UINT8 Type, UINT32 DataSize, const UINT8 *Data) {
  UINT32 CurBnk;
  VGM_PCM_BANK *TempPCM;
  VGM_PCM_DATA *TempBnk;
  UINT32 BankSize;
  bool RetVal;
  UINT8 BnkType;
  UINT8 CurDAC;

  BnkType = Type & 0x3F;
  if (BnkType >= PCM_BANK_COUNT || VGMCurLoop)
    return;

  if (Type == 0x7F) {
    ReadPCMTable(DataSize, Data);
    return;
  }

  TempPCM = &PCMBank[BnkType];
  TempPCM->BnkPos++;
  if (TempPCM->BnkPos <= TempPCM->BankCount)
    return;
  CurBnk = TempPCM->BankCount;
  TempPCM->BankCount++;
  TempPCM->Bank = (VGM_PCM_DATA *)realloc(
      TempPCM->Bank, sizeof(VGM_PCM_DATA) * TempPCM->BankCount);

  if (!(Type & 0x40))
    BankSize = DataSize;
  else
    BankSize = ReadLE32(&Data[0x01]);
  TempPCM->Data = realloc(TempPCM->Data, TempPCM->DataSize + BankSize);
  TempBnk = &TempPCM->Bank[CurBnk];
  TempBnk->DataStart = TempPCM->DataSize;
  if (!(Type & 0x40)) {
    TempBnk->DataSize = DataSize;
    TempBnk->Data = TempPCM->Data + TempBnk->DataStart;
    memcpy(TempBnk->Data, Data, DataSize);
  } else {
    TempBnk->Data = TempPCM->Data + TempBnk->DataStart;
    RetVal = DecompressDataBlk(TempBnk, DataSize, Data);
    if (!RetVal) {
      TempBnk->Data = NULL;
      TempBnk->DataSize = 0x00;
      for (CurDAC = 0x00; CurDAC < DacCtrlUsed; CurDAC++) {
        if (DacCtrl[DacCtrlUsg[CurDAC]].Bank == BnkType)
          daccontrol_refresh_data(DacCtrlUsg[CurDAC], TempPCM->Data,
                                  TempPCM->DataSize);
      }
      return;
    }
  }
  if (BankSize != TempBnk->DataSize)
    fprintf(stderr, "Error reading Data Block! Data Size conflict!\n");
  TempPCM->DataSize += BankSize;

  for (CurDAC = 0x00; CurDAC < DacCtrlUsed; CurDAC++) {
    if (DacCtrl[DacCtrlUsg[CurDAC]].Bank == BnkType)
      daccontrol_refresh_data(DacCtrlUsg[CurDAC], TempPCM->Data,
                              TempPCM->DataSize);
  }

  return;
}

static bool DecompressDataBlk(VGM_PCM_DATA *Bank, UINT32 DataSize,
                              const UINT8 *Data) {
  UINT8 ComprType;
  UINT8 BitDec;
  FUINT8 BitCmp;
  UINT8 CmpSubType;
  UINT16 AddVal;
  const UINT8 *InPos;
  const UINT8 *InDataEnd;
  UINT8 *OutPos;
  const UINT8 *OutDataEnd;
  FUINT16 InVal;
  FUINT16 OutVal;
  FUINT8 ValSize;
  FUINT8 InShift;
  FUINT8 OutShift;
  UINT8 *Ent1B;
  UINT16 *Ent2B;

  FUINT8 BitsToRead;
  FUINT8 BitReadVal;
  FUINT8 InValB;
  FUINT8 BitMask;
  FUINT8 OutBit;

  UINT16 OutMask;

  ComprType = Data[0x00];
  Bank->DataSize = ReadLE32(&Data[0x01]);

  switch (ComprType) {
  case 0x00: // n-Bit compression
    BitDec = Data[0x05];
    BitCmp = Data[0x06];
    CmpSubType = Data[0x07];
    AddVal = ReadLE16(&Data[0x08]);
    Ent1B = NULL;
    Ent2B = NULL;

    if (CmpSubType == 0x02) {
      Ent1B = (UINT8 *)PCMTbl.Entries;
      Ent2B = (UINT16 *)PCMTbl.Entries;
      if (!PCMTbl.EntryCount) {
        Bank->DataSize = 0x00;
        fprintf(
            stderr,
            "Error loading table-compressed data block! No table loaded!\n");
        return false;
      } else if (BitDec != PCMTbl.BitDec || BitCmp != PCMTbl.BitCmp) {
        Bank->DataSize = 0x00;
        fprintf(stderr,
                "Warning! Data block and loaded value table incompatible!\n");
        return false;
      }
    }

    ValSize = (BitDec + 7) / 8;
    InPos = Data + 0x0A;
    InDataEnd = Data + DataSize;
    InShift = 0;
    OutShift = BitDec - BitCmp;
    OutDataEnd = Bank->Data + Bank->DataSize;
    OutVal = 0x0000;

    for (OutPos = Bank->Data; OutPos < OutDataEnd && InPos < InDataEnd;
         OutPos += ValSize) {
      OutBit = 0x00;
      InVal = 0x0000;
      BitsToRead = BitCmp;
      while (BitsToRead) {
        BitReadVal = (BitsToRead >= 8) ? 8 : BitsToRead;
        BitsToRead -= BitReadVal;
        BitMask = (1 << BitReadVal) - 1;

        InShift += BitReadVal;
        InValB = (*InPos << InShift >> 8) & BitMask;
        if (InShift >= 8) {
          InShift -= 8;
          InPos++;
          if (InShift)
            InValB |= (*InPos << InShift >> 8) & BitMask;
        }

        InVal |= InValB << OutBit;
        OutBit += BitReadVal;
      }

      switch (CmpSubType) {
      case 0x00: // Copy
        OutVal = InVal + AddVal;
        break;
      case 0x01: // Shift Left
        OutVal = (InVal << OutShift) + AddVal;
        break;
      case 0x02: // Table
        switch (ValSize) {
        case 0x01:
          OutVal = Ent1B[InVal];
          break;
        case 0x02:
#ifdef VGM_LITTLE_ENDIAN
          OutVal = Ent2B[InVal];
#else
          OutVal = ReadLE16((UINT8 *)&Ent2B[InVal]);
#endif
          break;
        }
        break;
      }

#ifdef VGM_LITTLE_ENDIAN
      // memcpy(OutPos, &OutVal, ValSize);
      if (ValSize == 0x01)
        *((UINT8 *)OutPos) = (UINT8)OutVal;
      else // if (ValSize == 0x02)
        *((UINT16 *)OutPos) = (UINT16)OutVal;
#else
      if (ValSize == 0x01) {
        *OutPos = (UINT8)OutVal;
      } else // if (ValSize == 0x02)
      {
        OutPos[0x00] = (UINT8)((OutVal & 0x00FF) >> 0);
        OutPos[0x01] = (UINT8)((OutVal & 0xFF00) >> 8);
      }
#endif
    }
    break;
  case 0x01: // Delta-PCM
    BitDec = Data[0x05];
    BitCmp = Data[0x06];
    OutVal = ReadLE16(&Data[0x08]);

    Ent1B = (UINT8 *)PCMTbl.Entries;
    Ent2B = (UINT16 *)PCMTbl.Entries;
    if (!PCMTbl.EntryCount) {
      Bank->DataSize = 0x00;
      fprintf(stderr,
              "Error loading table-compressed data block! No table loaded!\n");
      return false;
    } else if (BitDec != PCMTbl.BitDec || BitCmp != PCMTbl.BitCmp) {
      Bank->DataSize = 0x00;
      fprintf(stderr,
              "Warning! Data block and loaded value table incompatible!\n");
      return false;
    }

    ValSize = (BitDec + 7) / 8;
    OutMask = (1 << BitDec) - 1;
    InPos = Data + 0x0A;
    InDataEnd = Data + DataSize;
    InShift = 0;
    OutShift = BitDec - BitCmp;
    OutDataEnd = Bank->Data + Bank->DataSize;
    AddVal = 0x0000;

    for (OutPos = Bank->Data; OutPos < OutDataEnd && InPos < InDataEnd;
         OutPos += ValSize) {
      OutBit = 0x00;
      InVal = 0x0000;
      BitsToRead = BitCmp;
      while (BitsToRead) {
        BitReadVal = (BitsToRead >= 8) ? 8 : BitsToRead;
        BitsToRead -= BitReadVal;
        BitMask = (1 << BitReadVal) - 1;

        InShift += BitReadVal;
        InValB = (*InPos << InShift >> 8) & BitMask;
        if (InShift >= 8) {
          InShift -= 8;
          InPos++;
          if (InShift)
            InValB |= (*InPos << InShift >> 8) & BitMask;
        }

        InVal |= InValB << OutBit;
        OutBit += BitReadVal;
      }

      switch (ValSize) {
      case 0x01:
        AddVal = Ent1B[InVal];
        OutVal += AddVal;
        OutVal &= OutMask;
        *((UINT8 *)OutPos) = (UINT8)OutVal;
        break;
      case 0x02:
#ifdef VGM_LITTLE_ENDIAN
        AddVal = Ent2B[InVal];
        OutVal += AddVal;
        OutVal &= OutMask;
        *((UINT16 *)OutPos) = (UINT16)OutVal;
#else
        AddVal = ReadLE16((UINT8 *)&Ent2B[InVal]);
        OutVal += AddVal;
        OutVal &= OutMask;
        OutPos[0x00] = (UINT8)((OutVal & 0x00FF) >> 0);
        OutPos[0x01] = (UINT8)((OutVal & 0xFF00) >> 8);
#endif
        break;
      }
    }
    break;
  default:
    fprintf(stderr, "Error: Unknown data block compression!\n");
    return false;
  }

  return true;
}

static UINT8 GetDACFromPCMBank(void) {

  UINT32 DataPos;


  DataPos = PCMBank[0x00].DataPos;
  if (DataPos >= PCMBank[0x00].DataSize)
    return 0x80;

  PCMBank[0x00].DataPos++;
  return PCMBank[0x00].Data[DataPos];
}

static UINT8 *GetPointerFromPCMBank(UINT8 Type, UINT32 DataPos) {
  if (Type >= PCM_BANK_COUNT)
    return NULL;

  if (DataPos >= PCMBank[Type].DataSize)
    return NULL;

  return &PCMBank[Type].Data[DataPos];
}

static void ReadPCMTable(UINT32 DataSize, const UINT8 *Data) {
  UINT8 ValSize;
  UINT32 TblSize;

  PCMTbl.ComprType = Data[0x00];
  PCMTbl.CmpSubType = Data[0x01];
  PCMTbl.BitDec = Data[0x02];
  PCMTbl.BitCmp = Data[0x03];
  PCMTbl.EntryCount = ReadLE16(&Data[0x04]);

  ValSize = (PCMTbl.BitDec + 7) / 8;
  TblSize = PCMTbl.EntryCount * ValSize;

  PCMTbl.Entries = realloc(PCMTbl.Entries, TblSize);
  memcpy(PCMTbl.Entries, &Data[0x06], TblSize);

  if (DataSize < 0x06 + TblSize)
    fprintf(stderr, "Warning! Bad PCM Table Length!\n");

  return;
}

#define CHIP_CHECK(name) (ChipAudio[CurChip].name.ChipType != 0xFF)
static void InterpretVGM(UINT32 SampleCount) {
  INT32 SmplPlayed;
  UINT8 Command;
  UINT8 TempByt;
  UINT16 TempSht;
  UINT32 TempLng;
  VGM_PCM_BANK *TempPCM;
  VGM_PCM_DATA *TempBnk;
  UINT32 ROMSize;
  UINT32 DataStart;
  UINT32 DataLen;
  const UINT8 *ROMData;
  UINT8 CurChip;
  const UINT8 *VGMPnt;

  if (VGMEnd)
    return;
  if (PausePlay && !ForceVGMExec)
    return;

  SmplPlayed = SamplePbk2VGM_I(VGMSmplPlayed + SampleCount);
  while (VGMSmplPos <= SmplPlayed) {
    Command = VGMData[VGMPos + 0x00];
    if (Command >= 0x70 && Command <= 0x8F) {
      switch (Command & 0xF0) {
      case 0x70:
        VGMSmplPos += (Command & 0x0F) + 0x01;
        break;
      case 0x80:
        GetDACFromPCMBank();
        VGMSmplPos += (Command & 0x0F);
        break;
      }
      VGMPos += 0x01;
    } else {
      VGMPnt = &VGMData[VGMPos];

      CurChip = 0x00;
      switch (Command) {
      case 0x30:
        if (VGMHead.lngHzPSG & 0x40000000) {
          Command += 0x20;
          CurChip = 0x01;
        }
        break;
      case 0x3F:
        if (VGMHead.lngHzPSG & 0x40000000) {
          Command += 0x10;
          CurChip = 0x01;
        }
        break;
      case 0xA1:
        if (VGMHead.lngHzYM2413 & 0x40000000) {
          Command -= 0x50;
          CurChip = 0x01;
        }
        break;
      case 0xA4:
        if (VGMHead.lngHzYM2151 & 0x40000000) {
          Command -= 0x50;
          CurChip = 0x01;
        }
        break;
      case 0xAA:
        if (VGMHead.lngHzYM3812 & 0x40000000) {
          Command -= 0x50;
          CurChip = 0x01;
        }
        break;
      case 0xAB:
        if (VGMHead.lngHzYM3526 & 0x40000000) {
          Command -= 0x50;
          CurChip = 0x01;
        }
        break;
      case 0xAC:
        if (VGMHead.lngHzY8950 & 0x40000000) {
          Command -= 0x50;
          CurChip = 0x01;
        }
        break;
      case 0xAE:
      case 0xAF:
        if (VGMHead.lngHzYMF262 & 0x40000000) {
          Command -= 0x50;
          CurChip = 0x01;
        }
        break;
      }

      switch (Command) {
      case 0x66:
        if (VGMHead.lngLoopOffset) {
          VGMPos = VGMHead.lngLoopOffset;
          VGMSmplPos -= VGMHead.lngLoopSamples;
          VGMSmplPlayed -= SampleVGM2Pbk_I(VGMHead.lngLoopSamples);
          SmplPlayed = SamplePbk2VGM_I(VGMSmplPlayed + SampleCount);
          VGMCurLoop++;

          if (VGMMaxLoopM && VGMCurLoop >= VGMMaxLoopM) {
#ifndef CONSOLE_MODE
            if (FadePlay) {
              FadeStart =
                  SampleVGM2Pbk_I(VGMHead.lngTotalSamples +
                                  (VGMCurLoop - 1) * VGMHead.lngLoopSamples);
            }
#endif
            FadePlay = true;
          }
          if (FadePlay && !FadeTime)
            VGMEnd = true;
        } else {
          if (VGMHead.lngTotalSamples != (UINT32)VGMSmplPos) {

            VGMHead.lngTotalSamples = VGMSmplPos;
          }

          if (HardStopOldVGMs) {
            if (VGMHead.lngVersion < 0x150 ||
                (VGMHead.lngVersion == 0x150 && HardStopOldVGMs == 0x02))
              Chips_GeneralActions(
                  0x01); // reset all chips, for instant silence
          }
          VGMEnd = true;
          break;
        }
        break;
      case 0x62: // 1/60s delay
        VGMSmplPos += 735;
        VGMPos += 0x01;
        break;
      case 0x63: // 1/50s delay
        VGMSmplPos += 882;
        VGMPos += 0x01;
        break;
      case 0x61: // xx Sample Delay
        TempSht = ReadLE16(&VGMPnt[0x01]);
        VGMSmplPos += TempSht;
        VGMPos += 0x03;
        break;
      case 0x50: // SN76496 write
        if (CHIP_CHECK(SN76496)) {
          chip_reg_write(0x00, CurChip, 0x00, 0x00, VGMPnt[0x01]);
        }
        VGMPos += 0x02;
        break;
      case 0x51: // YM2413 write
        if (CHIP_CHECK(YM2413)) {
          chip_reg_write(0x01, CurChip, 0x00, VGMPnt[0x01], VGMPnt[0x02]);
        }
        VGMPos += 0x03;
        break;
      case 0x67: // PCM Data Stream
        TempByt = VGMPnt[0x02];
        TempLng = ReadLE32(&VGMPnt[0x03]);
        if (TempLng & 0x80000000) {
          TempLng &= 0x7FFFFFFF;
          CurChip = 0x01;
        }

        switch (TempByt & 0xC0) {
        case 0x00: // Database Block
        case 0x40:
          AddPCMData(TempByt, TempLng, &VGMPnt[0x07]);
          break;
        case 0x80: // ROM/RAM Dump
          if (VGMCurLoop)
            break;

          ROMSize = ReadLE32(&VGMPnt[0x07]);
          DataStart = ReadLE32(&VGMPnt[0x0B]);
          DataLen = TempLng - 0x08;
          ROMData = &VGMPnt[0x0F];
          switch (TempByt) {
          case 0x84: // YMF278B ROM Image
            if (!CHIP_CHECK(YMF278B))
              break;
            ymf278b_write_rom(CurChip, ROMSize, DataStart, DataLen, ROMData);
            break;
          case 0x87: // YMF278B RAM Image
            if (!CHIP_CHECK(YMF278B))
              break;
            ymf278b_write_ram(CurChip, DataStart, DataLen, ROMData);
            break;
          case 0x88: // Y8950 DELTA-T ROM Image
            if (!CHIP_CHECK(Y8950))
              break;
            y8950_write_data_pcmrom(CurChip, ROMSize, DataStart, DataLen,
                                    ROMData);
            break;
          }
          break;
        case 0xC0: // RAM Write
          if (!(TempByt & 0x20)) {
            DataStart = ReadLE16(&VGMPnt[0x07]);
            DataLen = TempLng - 0x02;
            ROMData = &VGMPnt[0x09];
          } else {
            DataStart = ReadLE32(&VGMPnt[0x07]);
            DataLen = TempLng - 0x04;
            ROMData = &VGMPnt[0x0B];
          }
          break;
        }
        VGMPos += 0x07 + TempLng;
        break;
      case 0xE0: // Seek to PCM Data Bank Pos
        PCMBank[0x00].DataPos = ReadLE32(&VGMPnt[0x01]);
        VGMPos += 0x05;
        break;
      case 0x31: // Set AY8910 stereo mask
        TempByt = VGMPnt[0x01];
        CurChip = (TempByt & 0x80) >> 7;
        if (CHIP_CHECK(AY8910)) {
          ayxx_set_stereo_mask(CurChip, TempByt & 0x3F);
        }
        VGMPos += 0x02;
        break;
      case 0x4F: // GG Stereo
        if (CHIP_CHECK(SN76496)) {
          chip_reg_write(0x00, CurChip, 0x01, 0x00, VGMPnt[0x01]);
        }
        VGMPos += 0x02;
        break;
      case 0x54: // YM2151 write
        if (CHIP_CHECK(YM2151)) {
          chip_reg_write(0x03, CurChip, 0x01, VGMPnt[0x01], VGMPnt[0x02]);
        }
        VGMPos += 0x03;
        break;
      case 0x5A: // YM3812 write
        if (CHIP_CHECK(YM3812)) {
          chip_reg_write(0x09, CurChip, 0x00, VGMPnt[0x01], VGMPnt[0x02]);
        }
        VGMPos += 0x03;
        break;
      case 0x5B: // YM3526 write
        if (CHIP_CHECK(YM3526)) {
          chip_reg_write(0x04, CurChip, 0x00, VGMPnt[0x01], VGMPnt[0x02]);
        }
        VGMPos += 0x03;
        break;
      case 0x5C: // Y8950 write
        if (CHIP_CHECK(Y8950)) {
          chip_reg_write(0x05, CurChip, 0x00, VGMPnt[0x01], VGMPnt[0x02]);
        }
        VGMPos += 0x03;
        break;
      case 0x5E: // YMF262 write port 0
      case 0x5F: // YMF262 write port 1
        if (CHIP_CHECK(YMF262)) {
          chip_reg_write(0x06, CurChip, Command & 0x01, VGMPnt[0x01],
                         VGMPnt[0x02]);
        }
        VGMPos += 0x03;
        break;
      case 0xD0: // YMF278B write
        if (CHIP_CHECK(YMF278B)) {
          CurChip = (VGMPnt[0x01] & 0x80) >> 7;
          chip_reg_write(0x07, CurChip, VGMPnt[0x01] & 0x7F, VGMPnt[0x02],
                         VGMPnt[0x03]);
        }
        VGMPos += 0x04;
        break;
      case 0xA0: // AY8910 write
        CurChip = (VGMPnt[0x01] & 0x80) >> 7;
        if (CHIP_CHECK(AY8910)) {
          chip_reg_write(0x08, CurChip, 0x00, VGMPnt[0x01] & 0x7F,
                         VGMPnt[0x02]);
        }
        VGMPos += 0x03;
        break;
      case 0xD2: // SCC1 write
        CurChip = (VGMPnt[0x01] & 0x80) >> 7;
        if (CHIP_CHECK(K051649)) {
          chip_reg_write(0x09, CurChip, VGMPnt[0x01] & 0x7F, VGMPnt[0x02],
                         VGMPnt[0x03]);
        }
        VGMPos += 0x04;
        break;
      case 0x90: // DAC Ctrl: Setup Chip
        CurChip = VGMPnt[0x01];
        if (CurChip == 0xFF) {
          VGMPos += 0x05;
          break;
        }
        if (!DacCtrl[CurChip].Enable) {
          device_start_daccontrol(CurChip);
          device_reset_daccontrol(CurChip);
          DacCtrl[CurChip].Enable = true;
          DacCtrlUsg[DacCtrlUsed] = CurChip;
          DacCtrlUsed++;
        }
        TempByt = VGMPnt[0x02]; // Chip Type
        TempSht = ReadBE16(&VGMPnt[0x03]);
        daccontrol_setup_chip(CurChip, TempByt & 0x7F, (TempByt & 0x80) >> 7,
                              TempSht);
        VGMPos += 0x05;
        break;
      case 0x91: // DAC Ctrl: Set Data
        CurChip = VGMPnt[0x01];
        if (CurChip == 0xFF || !DacCtrl[CurChip].Enable) {
          VGMPos += 0x05;
          break;
        }
        DacCtrl[CurChip].Bank = VGMPnt[0x02];
        if (DacCtrl[CurChip].Bank >= PCM_BANK_COUNT)
          DacCtrl[CurChip].Bank = 0x00;

        TempPCM = &PCMBank[DacCtrl[CurChip].Bank];
        daccontrol_set_data(CurChip, TempPCM->Data, TempPCM->DataSize,
                            VGMPnt[0x03], VGMPnt[0x04]);
        VGMPos += 0x05;
        break;
      case 0x92: // DAC Ctrl: Set Freq
        CurChip = VGMPnt[0x01];
        if (CurChip == 0xFF || !DacCtrl[CurChip].Enable) {
          VGMPos += 0x06;
          break;
        }
        TempLng = ReadLE32(&VGMPnt[0x02]);
        daccontrol_set_frequency(CurChip, TempLng);
        VGMPos += 0x06;
        break;
      case 0x93: // DAC Ctrl: Play from Start Pos
        CurChip = VGMPnt[0x01];
        if (CurChip == 0xFF || !DacCtrl[CurChip].Enable ||
            !PCMBank[DacCtrl[CurChip].Bank].BankCount) {
          VGMPos += 0x0B;
          break;
        }
        DataStart = ReadLE32(&VGMPnt[0x02]);
        TempByt = VGMPnt[0x06];
        DataLen = ReadLE32(&VGMPnt[0x07]);
        daccontrol_start(CurChip, DataStart, TempByt, DataLen);
        VGMPos += 0x0B;
        break;
      case 0x94: // DAC Ctrl: Stop immediately
        CurChip = VGMPnt[0x01];
        if (!DacCtrl[CurChip].Enable) {
          VGMPos += 0x02;
          break;
        }
        if (CurChip < 0xFF) {
          daccontrol_stop(CurChip);
        } else {
          for (CurChip = 0x00; CurChip < 0xFF; CurChip++)
            daccontrol_stop(CurChip);
        }
        VGMPos += 0x02;
        break;
      case 0x95: // DAC Ctrl: Play Block (small)
        CurChip = VGMPnt[0x01];
        if (CurChip == 0xFF || !DacCtrl[CurChip].Enable ||
            !PCMBank[DacCtrl[CurChip].Bank].BankCount) {
          VGMPos += 0x05;
          break;
        }
        TempPCM = &PCMBank[DacCtrl[CurChip].Bank];
        TempSht = ReadLE16(&VGMPnt[0x02]);
        if (TempSht >= TempPCM->BankCount)
          TempSht = 0x00;
        TempBnk = &TempPCM->Bank[TempSht];

        TempByt = DCTRL_LMODE_BYTES | (VGMPnt[0x04] & 0x10) | // Reverse Mode
                  ((VGMPnt[0x04] & 0x01) << 7);               // Looping
        daccontrol_start(CurChip, TempBnk->DataStart, TempByt,
                         TempBnk->DataSize);
        VGMPos += 0x05;
        break;
      default:
#ifdef CONSOLE_MODE
        if (!CmdList[Command]) {
          fprintf(stderr, "Unknown command: %02hhX\n", Command);
          CmdList[Command] = true;
        }
#endif

        switch (Command & 0xF0) {
        case 0x00:
        case 0x10:
        case 0x20:
          VGMPos += 0x01;
          break;
        case 0x30:
          VGMPos += 0x02;
          break;
        case 0x40:
        case 0x50:
        case 0xA0:
        case 0xB0:
          VGMPos += 0x03;
          break;
        case 0xC0:
        case 0xD0:
          VGMPos += 0x04;
          break;
        case 0xE0:
        case 0xF0:
          VGMPos += 0x05;
          break;
        default:
          VGMEnd = true;
          EndPlay = true;
          break;
        }
        break;
      }
    }

    if (VGMPos >= VGMHead.lngEOFOffset)
      VGMEnd = true;

    if (VGMEnd)
      break;
  }

  return;
}

static void GeneralChipLists(void) {
  UINT16 CurBufIdx;
  CA_LIST *CLstOld;
  CA_LIST *CLst;
  CA_LIST *CurLst;
  UINT8 CurChip;
  UINT8 CurCSet;
  CAUD_ATTR *CAA;

  ChipListAll = NULL;
  ChipListPause = NULL;

  CurBufIdx = 0x00;
  CLstOld = NULL;
  for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++) {
    for (CurCSet = 0x00; CurCSet < 0x02; CurCSet++) {
      CAA = (CAUD_ATTR *)&ChipAudio[CurCSet] + CurChip;
      if (CAA->ChipType != 0xFF) {
        CLst = &ChipListBuffer[CurBufIdx];
        CurBufIdx++;
        if (CLstOld == NULL)
          ChipListAll = CLst;
        else
          CLstOld->next = CLst;

        CLst->CAud = CAA;
        CLst->COpts = (CHIP_OPTS *)&ChipOpts[CurCSet] + CurChip;
        CLstOld = CLst;
      }
    }
  }
  if (CLstOld != NULL)
    CLstOld->next = NULL;

  CLstOld = NULL;
  CurLst = ChipListAll;
  while (CurLst != NULL) {
    if (CurLst->CAud->ChipType != 0x05 && CurLst->CAud->ChipType != 0x10) {
      CLst = &ChipListBuffer[CurBufIdx];
      CurBufIdx++;
      if (CLstOld == NULL)
        ChipListPause = CLst;
      else
        CLstOld->next = CLst;

      *CLst = *CurLst;
      CLstOld = CLst;
    }
    CurLst = CurLst->next;
  }
  if (CLstOld != NULL)
    CLstOld->next = NULL;

  return;
}

static void SetupResampler(CAUD_ATTR *CAA) {
  if (!CAA->SmpRate) {
    CAA->Resampler = 0xFF;
    return;
  }

  if (CAA->SmpRate < SampleRate)
    CAA->Resampler = 0x01;
  else if (CAA->SmpRate == SampleRate)
    CAA->Resampler = 0x02;
  else if (CAA->SmpRate > SampleRate)
    CAA->Resampler = 0x03;
  if (CAA->Resampler == 0x01 || CAA->Resampler == 0x03) {
    if (ResampleMode == 0x02 ||
        (ResampleMode == 0x01 && CAA->Resampler == 0x03))
      CAA->Resampler = 0x00;
  }

  CAA->SmpP = 0x00;
  CAA->SmpLast = 0x00;
  CAA->SmpNext = 0x00;
  CAA->LSmpl.Left = 0x00;
  CAA->LSmpl.Right = 0x00;
  if (CAA->Resampler == 0x01) {
    CAA->StreamUpdate(CAA->ChipID, StreamBufs, 1);
    CAA->NSmpl.Left = StreamBufs[0x00][0x00];
    CAA->NSmpl.Right = StreamBufs[0x01][0x00];
  } else {
    CAA->NSmpl.Left = 0x00;
    CAA->NSmpl.Right = 0x00;
  }

  return;
}


INLINE INT16 Limit2Short(INT32 Value) {
  return (Value > 32767) ? 32767 : ((Value < -32768) ? -32768 : (INT16)Value);
}

static void null_update(UINT8 ChipID, stream_sample_t **outputs, int samples) {
  memset(outputs[0x00], 0x00, sizeof(stream_sample_t) * samples);
  memset(outputs[0x01], 0x00, sizeof(stream_sample_t) * samples);

  return;
}

static void dual_opl2_stereo(UINT8 ChipID, stream_sample_t **outputs,
                             int samples) {
  ym3812_stream_update(ChipID, outputs, samples);
  if (ChipID & 0x01)
    memset(outputs[0x00], 0x00,
           sizeof(stream_sample_t) * samples); 
  else
    memset(outputs[0x01], 0x00,
           sizeof(stream_sample_t) * samples); 

  return;
}

#define FIXPNT_BITS 11
#define FIXPNT_FACT (1 << FIXPNT_BITS)
#if (FIXPNT_BITS <= 11)
typedef UINT32 SLINT;
#else
typedef UINT64 SLINT;
#endif
#define FIXPNT_MASK (FIXPNT_FACT - 1)

#define getfriction(x) ((x) & FIXPNT_MASK)
#define getnfriction(x) ((FIXPNT_FACT - (x)) & FIXPNT_MASK)
#define fpi_floor(x) ((x) & ~FIXPNT_MASK)
#define fpi_ceil(x) ((x + FIXPNT_MASK) & ~FIXPNT_MASK)
#define fp2i_floor(x) ((x) / FIXPNT_FACT)
#define fp2i_ceil(x) ((x + FIXPNT_MASK) / FIXPNT_FACT)

static void ResampleChipStream(CA_LIST *CLst, WAVE_32BS *RetSample,
                               UINT32 Length) {
  CAUD_ATTR *CAA;
  INT32 *CurBufL;
  INT32 *CurBufR;
  INT32 *StreamPnt[0x02];
  UINT32 InBase;
  UINT32 InPos;
  UINT32 InPosNext;
  UINT32 OutPos;
  UINT32 SmpFrc;
  UINT32 InPre;
  UINT32 InNow;
  SLINT InPosL;
  INT64 TempSmpL;
  INT64 TempSmpR;
  INT32 TempS32L;
  INT32 TempS32R;
  INT32 SmpCnt;
  INT32 CurSmpl;
  UINT64 ChipSmpRate;

  CAA = CLst->CAud;
  CurBufL = StreamBufs[0x00];
  CurBufR = StreamBufs[0x01];

  do {
    switch (CAA->Resampler) {
    case 0x00: // old, but very fast resampler
      CAA->SmpLast = CAA->SmpNext;
      CAA->SmpP += Length;
      CAA->SmpNext = (UINT32)((UINT64)CAA->SmpP * CAA->SmpRate / SampleRate);
      if (CAA->SmpLast >= CAA->SmpNext) {
        for (OutPos = 0x00; OutPos < Length; OutPos++) {
          RetSample[OutPos].Left += CAA->LSmpl.Left * CAA->Volume;
          RetSample[OutPos].Right += CAA->LSmpl.Right * CAA->Volume;
        }
      } else {
        SmpCnt = CAA->SmpNext - CAA->SmpLast;
        CAA->StreamUpdate(CAA->ChipID, StreamBufs, SmpCnt);

        if (SmpCnt == 1) {
          for (OutPos = 0x00; OutPos < Length; OutPos++) {
            RetSample[OutPos].Left += CurBufL[0x00] * CAA->Volume;
            RetSample[OutPos].Right += CurBufR[0x00] * CAA->Volume;
          }
          CAA->LSmpl.Left = CurBufL[0x00];
          CAA->LSmpl.Right = CurBufR[0x00];
        } else {
          TempS32L = CurBufL[0x00];
          TempS32R = CurBufR[0x00];
          for (CurSmpl = 0x01; CurSmpl < SmpCnt; CurSmpl++) {
            TempS32L += CurBufL[CurSmpl];
            TempS32R += CurBufR[CurSmpl];
          }
          for (OutPos = 0x00; OutPos < Length; OutPos++) {
            RetSample[OutPos].Left += (TempS32L * CAA->Volume / SmpCnt);
            RetSample[OutPos].Right += (TempS32R * CAA->Volume / SmpCnt);
          }
          CAA->LSmpl.Left = CurBufL[SmpCnt - 1];
          CAA->LSmpl.Right = CurBufR[SmpCnt - 1];
        }
      }
      break;
    case 0x01: // Upsampling
      ChipSmpRate = CAA->SmpRate;
      InPosL = (SLINT)(FIXPNT_FACT * CAA->SmpP * ChipSmpRate / SampleRate);
      InPre = (UINT32)fp2i_floor(InPosL);
      InNow = (UINT32)fp2i_ceil(InPosL);

      CurBufL[0x00] = CAA->LSmpl.Left;
      CurBufR[0x00] = CAA->LSmpl.Right;
      CurBufL[0x01] = CAA->NSmpl.Left;
      CurBufR[0x01] = CAA->NSmpl.Right;
      StreamPnt[0x00] = &CurBufL[0x02];
      StreamPnt[0x01] = &CurBufR[0x02];
      CAA->StreamUpdate(CAA->ChipID, StreamPnt, InNow - CAA->SmpNext);

      InBase =
          FIXPNT_FACT + (UINT32)(InPosL - (SLINT)CAA->SmpNext * FIXPNT_FACT);
      SmpCnt = FIXPNT_FACT;
      CAA->SmpLast = InPre;
      CAA->SmpNext = InNow;
      for (OutPos = 0x00; OutPos < Length; OutPos++) {
        InPos =
            InBase + (UINT32)(FIXPNT_FACT * OutPos * ChipSmpRate / SampleRate);

        InPre = fp2i_floor(InPos);
        InNow = fp2i_ceil(InPos);
        SmpFrc = getfriction(InPos);

        // Linear interpolation
        TempSmpL = ((INT64)CurBufL[InPre] * (FIXPNT_FACT - SmpFrc)) +
                   ((INT64)CurBufL[InNow] * SmpFrc);
        TempSmpR = ((INT64)CurBufR[InPre] * (FIXPNT_FACT - SmpFrc)) +
                   ((INT64)CurBufR[InNow] * SmpFrc);
        RetSample[OutPos].Left += (INT32)(TempSmpL * CAA->Volume / SmpCnt);
        RetSample[OutPos].Right += (INT32)(TempSmpR * CAA->Volume / SmpCnt);
      }
      CAA->LSmpl.Left = CurBufL[InPre];
      CAA->LSmpl.Right = CurBufR[InPre];
      CAA->NSmpl.Left = CurBufL[InNow];
      CAA->NSmpl.Right = CurBufR[InNow];
      CAA->SmpP += Length;
      break;
    case 0x02: // Copying
      CAA->SmpNext = CAA->SmpP * CAA->SmpRate / SampleRate;
      CAA->StreamUpdate(CAA->ChipID, StreamBufs, Length);

      for (OutPos = 0x00; OutPos < Length; OutPos++) {
        RetSample[OutPos].Left += CurBufL[OutPos] * CAA->Volume;
        RetSample[OutPos].Right += CurBufR[OutPos] * CAA->Volume;
      }
      CAA->SmpP += Length;
      CAA->SmpLast = CAA->SmpNext;
      break;
    case 0x03: // Downsampling
      ChipSmpRate = CAA->SmpRate;
      InPosL = (SLINT)(FIXPNT_FACT * (CAA->SmpP + Length) * ChipSmpRate /
                       SampleRate);
      CAA->SmpNext = (UINT32)fp2i_ceil(InPosL);

      CurBufL[0x00] = CAA->LSmpl.Left;
      CurBufR[0x00] = CAA->LSmpl.Right;
      StreamPnt[0x00] = &CurBufL[0x01];
      StreamPnt[0x01] = &CurBufR[0x01];
      CAA->StreamUpdate(CAA->ChipID, StreamPnt, CAA->SmpNext - CAA->SmpLast);

      InPosL = (SLINT)(FIXPNT_FACT * CAA->SmpP * ChipSmpRate / SampleRate);
      InBase =
          FIXPNT_FACT + (UINT32)(InPosL - (SLINT)CAA->SmpLast * FIXPNT_FACT);
      InPosNext = InBase;
      for (OutPos = 0x00; OutPos < Length; OutPos++) {
        InPos = InPosNext;
        InPosNext = InBase + (UINT32)(FIXPNT_FACT * (OutPos + 1) * ChipSmpRate /
                                      SampleRate);


        SmpFrc = getnfriction(InPos);
        if (SmpFrc) {
          InPre = fp2i_floor(InPos);
          TempSmpL = (INT64)CurBufL[InPre] * SmpFrc;
          TempSmpR = (INT64)CurBufR[InPre] * SmpFrc;
        } else {
          TempSmpL = TempSmpR = 0x00;
        }
        SmpCnt = SmpFrc;

        SmpFrc = getfriction(InPosNext);
        InPre = fp2i_floor(InPosNext);
        if (SmpFrc) {
          TempSmpL += (INT64)CurBufL[InPre] * SmpFrc;
          TempSmpR += (INT64)CurBufR[InPre] * SmpFrc;
          SmpCnt += SmpFrc;
        }

        InNow = fp2i_ceil(InPos);
        SmpCnt += (InPre - InNow) * FIXPNT_FACT;
        while (InNow < InPre) {
          TempSmpL += (INT64)CurBufL[InNow] * FIXPNT_FACT;
          TempSmpR += (INT64)CurBufR[InNow] * FIXPNT_FACT;
          InNow++;
        }

        RetSample[OutPos].Left += (INT32)(TempSmpL * CAA->Volume / SmpCnt);
        RetSample[OutPos].Right += (INT32)(TempSmpR * CAA->Volume / SmpCnt);
      }

      CAA->LSmpl.Left = CurBufL[InPre];
      CAA->LSmpl.Right = CurBufR[InPre];
      CAA->SmpP += Length;
      CAA->SmpLast = CAA->SmpNext;
      break;
    default:
      CAA->SmpP += SampleRate;
      break;
    }

    if (CAA->SmpLast >= CAA->SmpRate) {
      CAA->SmpLast -= CAA->SmpRate;
      CAA->SmpNext -= CAA->SmpRate;
      CAA->SmpP -= SampleRate;
    }

    CAA = CAA->Paired;
  } while (CAA != NULL);

  return;
}

static INT32 RecalcFadeVolume(void) {
  if (FadePlay) {
    if (!FadeStart)
      FadeStart = PlayingTime;

    UINT64 SamplesElapsed = (UINT64)(PlayingTime - FadeStart);
    UINT64 FadeSamples = (UINT64)FadeTime * SampleRate / 1000;

    if (FadeSamples == 0 || SamplesElapsed >= FadeSamples) {
      MasterVol = 0.0f;
      FinalVol = 0.0f;
      VGMEnd = true;
      return 0;
    }

    UINT32 VolFactorQ16 = (UINT32)(((FadeSamples - SamplesElapsed) << 16) / FadeSamples);
    
    MasterVol = (float)VolFactorQ16 / 65536.0f;
    FinalVol = VolumeLevelM * MasterVol;
  }

  return (INT32)(0x100 * FinalVol + 0.5f);
}

UINT32 FillBuffer(WAVE_16BS *Buffer, UINT32 BufferSize) {
  UINT32 CurSmpl;
  WAVE_32BS TempBuf;
  INT32 CurMstVol;
  UINT32 RecalcStep;
  CA_LIST *CurCLst;


  RecalcStep = FadePlay ? SampleRate / 44100 : 0;
  CurMstVol = RecalcFadeVolume();

  if (Buffer == NULL) {

    InterpretFile(BufferSize);

    if (FadePlay && !FadeStart) {
      FadeStart = PlayingTime;
      RecalcStep = FadePlay ? SampleRate / 100 : 0;
    }
    if (RecalcStep)
      CurMstVol = RecalcFadeVolume();

    if (VGMEnd) {
      if (PauseSmpls <= BufferSize) {
        PauseSmpls = 0;
        EndPlay = true;
      } else {
        PauseSmpls -= BufferSize;
      }
    }

    return BufferSize;
  }

  CurChipList = (VGMEnd || PausePlay) ? ChipListPause : ChipListAll;

  for (CurSmpl = 0x00; CurSmpl < BufferSize; CurSmpl++) {
    InterpretFile(1);

    // Sample Structures
    //	00 - SN76496
    //	01 - YM2413
    //	02 - YM2612
    //	03 - YM2151
    //	04 - SegaPCM
    //	05 - RF5C68
    //	06 - YM2203
    //	07 - YM2608
    //	08 - YM2610/YM2610B
    //	09 - YM3812
    //	0A - YM3526
    //	0B - Y8950
    //	0C - YMF262
    //	0D - YMF278B
    //	0E - YMF271
    //	0F - YMZ280B
    //	10 - RF5C164
    //	11 - PWM
    //	12 - AY8910
    //	13 - GameBoy
    //	14 - NES APU
    //	15 - MultiPCM
    //	16 - UPD7759
    //	17 - OKIM6258
    //	18 - OKIM6295
    //	19 - K051649
    //	1A - K054539
    //	1B - HuC6280
    //	1C - C140
    //	1D - K053260
    //	1E - Pokey
    //	1F - QSound
    //	20 - YMF292/SCSP
    //	21 - WonderSwan
    //	22 - VSU
    //	23 - SAA1099
    //	24 - ES5503
    //	25 - ES5506
    //	26 - X1-010
    //	27 - C352
    //	28 - GA20
    TempBuf.Left = 0x00;
    TempBuf.Right = 0x00;
    CurCLst = CurChipList;
    while (CurCLst != NULL) {
      if (!CurCLst->COpts->Disabled) {
        ResampleChipStream(CurCLst, &TempBuf, 1);
      }
      CurCLst = CurCLst->next;
    }

    TempBuf.Left = ((TempBuf.Left >> 5) * CurMstVol) >> 11;
    TempBuf.Right = ((TempBuf.Right >> 5) * CurMstVol) >> 11;
    if (SurroundSound)
      TempBuf.Right *= -1;
    Buffer[CurSmpl].Left = Limit2Short(TempBuf.Left);
    Buffer[CurSmpl].Right = Limit2Short(TempBuf.Right);

    if (FadePlay && !FadeStart) {
      FadeStart = PlayingTime;
      RecalcStep = FadePlay ? SampleRate / 100 : 0;
    }
    if (RecalcStep && !(CurSmpl % RecalcStep))
      CurMstVol = RecalcFadeVolume();

    if (VGMEnd) {
      if (!PauseSmpls) {
        if (!EndPlay) {
          EndPlay = true;
          break;
        }
      } else
      {
        PauseSmpls--;
      }
    }
  }

  return CurSmpl;
}

UINT64 TimeSpec2Int64(const struct timespec *ts) {
  return (UINT64)ts->tv_sec * 1000000000 + ts->tv_nsec;
}
// --- Merged from Stream.c ---

typedef struct {
  UINT16 wFormatTag;
  UINT16 nChannels;
  UINT32 nSamplesPerSec;
  UINT32 nAvgBytesPerSec;
  UINT16 nBlockAlign;
  UINT16 wBitsPerSample;
  UINT16 cbSize;
} WAVEFORMATEX;

static snd_pcm_t *hAlsaOut = NULL;
static volatile bool WaveOutOpen = false;
static pthread_t hThread;

WAVEFORMATEX WaveFmt;
UINT32 BUFFERSIZE;
UINT32 SMPL_P_BUFFER;
volatile bool StreamPause = false;
UINT16 AUDIOBUFFERU = 10;
UINT32 BlocksSent = 0;
UINT32 BlocksPlayed = 0;
bool SoundLog = false;
char SoundLogFile[PATH_MAX] = {0};


void WaveOutLinuxCallBack(UINT32 WrtSmpls) {
  if (!hAlsaOut)
    return;
  static WAVE_16BS TempBuf[8192];
  FillBuffer(TempBuf, WrtSmpls);
  if (snd_pcm_writei(hAlsaOut, TempBuf, WrtSmpls) < 0)
    snd_pcm_prepare(hAlsaOut);
  BlocksSent++;
  BlocksPlayed++;
}

static void *PlaybackThread(void *arg) {
  while (WaveOutOpen) {
    if (!StreamPause && hAlsaOut)
      WaveOutLinuxCallBack(SMPL_P_BUFFER);
    else
      usleep(10000);
  }
  return NULL;
}

// --- Audio Warmup
static pthread_t hWarmupThread = 0;
static bool WarmupStarted = false;

static void* AudioWarmupThread(void* arg) {
  int retries = 0;
  int err;
  
  // Try to pre-open the device
  while (retries < 3) {
    if (hAlsaOut) break; // Already open?
    
    err = snd_pcm_open(&hAlsaOut, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err >= 0) break;
    
    retries++;
    if (retries < 3) {
      usleep(1000000); // 1s
    }
  }
  return (void*)(long)err;
}

void StartAudioWarmup(void) {
  if (!WarmupStarted) {
    WarmupStarted = true;
    pthread_create(&hWarmupThread, NULL, AudioWarmupThread, NULL);
  }
}

UINT8 StartStream(UINT8 DeviceID) {
  if (WaveOutOpen)
    return 0x01;
    
  SMPL_P_BUFFER = SampleRate / 100;

  // Synchronize with warmup thread if it was started
  if (WarmupStarted) {
    void* thread_ret;
    pthread_join(hWarmupThread, &thread_ret);
    WarmupStarted = false; // Reset for next time if needed
    hWarmupThread = 0;
    
    // Check if open succeeded
    if (!hAlsaOut) {
       return 0xC0;
    }
  } else {
    // Fallback if warmup wasn't called (legacy path or restart)
    if (!hAlsaOut) {
        if (snd_pcm_open(&hAlsaOut, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
            return 0xC0;
    }
  }

  snd_pcm_set_params(hAlsaOut, SND_PCM_FORMAT_S16_LE,
                     SND_PCM_ACCESS_RW_INTERLEAVED, 2, SampleRate, 1, 50000);
  WaveOutOpen = true;
  pthread_create(&hThread, NULL, PlaybackThread, NULL);
  return 0x00;
}

UINT8 StopStream(void) {
  if (!WaveOutOpen)
    return 0xD8;
  WaveOutOpen = false;
  pthread_join(hThread, NULL);
  snd_pcm_close(hAlsaOut);
  hAlsaOut = NULL;
  return 0x00;
}

void PauseStream(bool PauseOn) {
  StreamPause = PauseOn;

}

void WaveOutCallbackFnc(void) {}
