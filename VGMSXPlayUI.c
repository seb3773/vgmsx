// VGMSXPlayUI.c: C Source File for vgMSX User Interface


// #define _GNU_SOURCE
#include <ctype.h> // for toupper
#include <dirent.h>
#include <locale.h> // for setlocale
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wchar.h>

#include <fcntl.h>      // Added
#include <limits.h>     // for PATH_MAX
#include <signal.h>     // for signal()
#include <sys/select.h> // for select()
#include <sys/time.h>   // for struct timeval in _kbhit()
#include <sys/types.h>  // Added
#include <sys/wait.h>   // Added
#include <termios.h>
#include <unistd.h> // for STDIN_FILENO and usleep()

#define Sleep(msec) usleep(msec * 1000)
#define _vsnwprintf vswprintf
// #endif

#define VGMSX_VERSION "1.0"

#define printerr(x) fprintf(stderr, x)

// #include "chips/mamedef.h"

#include "VGMSXPlay.h"
// #include "dbus.h"

#define DIR_CHR '/'
#define DIR_STR "/"
#define QMARK_CHR '\''

#ifndef SHARE_PREFIX
#define SHARE_PREFIX "/usr/local"
#endif

// #endif

#define APP_NAME "vgMSX player"
#define APP_NAME_L L"vgMSX player"

int main(int argc, char *argv[]);
static void RemoveNewLines(char *String);
static void RemoveQuotationMarks(char *String);
char *GetLastDirSeparator(const char *FilePath);
static bool IsAbsolutePath(const char *FilePath);
static char *GetFileExtension(const char *FilePath);
static void StandardizeDirSeparators(char *FilePath);
static char *GetAppFileName(void);
static void cls(void);
static void changemode(bool);
static int _kbhit(void);
static int _getch(void);
// #endif

#define BOX_INNER_WIDTH 67

// --- UI Coordinate Tracking System ---
static int _g_UI_Line = 0; // Internal tracker. DO NOT ACCESS DIRECTLY.
static bool IsPlaylistMode = false; // Playlist View State

// Debug logging helper
static void LogUI(const char *action, int val) {
    FILE *f = fopen("ui_debug.log", "a");
    if (f) {
        fprintf(f, "[UI] %-15s | Val: %3d | Cur: %3d\n", action, val, _g_UI_Line);
        fclose(f);
    }
}

static int UI_GetLine(void) {
    return _g_UI_Line;
}

static void UI_SetLine(int y) {
    if (y < 0) {
        LogUI("PANIC_SET_NEG", y);
        fprintf(stderr, "\n\nCRITICAL UI ERROR: Attempt to set negative line %d\n", y);
        exit(99);
    }
    // LogUI("SetLine", y);
    _g_UI_Line = y;
}

static void UI_IncLine(void) {
    _g_UI_Line++;
    // LogUI("IncLine", _g_UI_Line);
}

static void UI_GoToLine(int y) {
  // Absolute Positioning Strategy
  // y = 0-indexed line number from top of application frame.
  // ANSI uses 1-indexed rows. So Row = y + 1.
  
  if (y < 0) y = 0; // Safety clamp
  
  if (y == _g_UI_Line) return; // Trusting tracker for now to save bandwidth

  // RELATIVE POSITIONING ONLY (Inline UI)
  if (y < _g_UI_Line) {
    // Moving UP
    int delta = _g_UI_Line - y;
    printf("\x1B[%dA", delta);
  } else {
    // Moving DOWN
    int delta = y - _g_UI_Line;
    printf("\x1B[%dB", delta);
  }
  _g_UI_Line = y;
}

static void PrintBoxTop(void) {
  printf("╭");
  for (int i = 0; i < BOX_INNER_WIDTH; i++)
    printf("─");
  printf("╮\n");
  UI_IncLine();
}

static void PrintBoxBottom(void) {
  printf("╰");
  for (int i = 0; i < BOX_INNER_WIDTH; i++)
    printf("─");
  printf("╯\n");
  UI_IncLine();
}

static void PrintBoxSeparator(void) {
 printf("│");
 
  printf("\x1B[44m"); // Blue background
  for (int i = 0; i < BOX_INNER_WIDTH; i++)
    printf("─");
  printf("\x1B[0m"); // Reset
 printf("│\n");
 UI_IncLine();
}

// Duplicate removed


INLINE int GetU8Width(const char *s) {
  int width = 0;
  while (*s) {
    if (*s == 0x1B) { // ANSI Escape Sequence
      s++;
      if (*s == '[') {
        s++;
        while (*s && *s != 'm') s++;
        if (*s == 'm') s++;
      }
      continue;
    }
    if ((*s & 0xC0) != 0x80)
      width++;
    s++;
  }
  return width;
}

static void PrintBoxLine(const char *fmt, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  
  // Sanitize: Remove newlines to prevent layout breakage
  for (int i=0; buffer[i]; i++) {
      if (buffer[i] == '\n' || buffer[i] == '\r') buffer[i] = ' ';
  }

  int width = GetU8Width(buffer);
  if (width > BOX_INNER_WIDTH) {
    int w = 0;
    char *p = buffer;
    int limit = BOX_INNER_WIDTH - 3; // Space for "..."

    while (*p) {
      if ((*p & 0xC0) != 0x80) { // Start of a character
        if (w >= limit) {
          break;
        }
        w++;
      }
      p++;
    }
    strcpy(p, "...");
    width = GetU8Width(buffer); // Recalculate width (should be BOX_INNER_WIDTH)
  }

  // Standard Prefix
  printf("│"); // Left Border
  
  printf("\x1B[97;44m"); // Bright White text on Blue background

  printf("%s", buffer);
  
  // Padding Logic: Inner Width - Content Width
  int pad = BOX_INNER_WIDTH - width;
  if (pad < 0) pad = 0;
  for (int i = 0; i < pad; i++)
    printf(" ");
    
  printf("\x1B[0m"); // Reset attributes
  printf("│\n"); // Right Border
  UI_IncLine();
}

static void PrintBoxLineW(const wchar_t *fmt, ...) {
  wchar_t buffer[1024];
  char u8buf[2048];
  va_list args;
  va_start(args, fmt);
  vswprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  wcstombs(u8buf, buffer, sizeof(u8buf));
  PrintBoxLine("%s", u8buf);
}

static const char *MSXBootLines[] = {
"                                                                  ",
"                                                                  ",
"                           vgMSX music                            ",
"                           version " VGMSX_VERSION "                            ",
"                                                                  ",
"                    Copyright 2026 by Seb3773                     ",
"                                                                  ",
"                                                                  "
};

static const char *MSXLogoLines[] = {
"                                                                  ",
"           \x1B[40m                                             \x1B[44m       ",
"           \x1B[40m             ▄▄▄      ▄▄▄  ▄▄▄▄▄▄▄ ▄▄▄   ▄▄▄ \x1B[44m          ",
"           \x1B[40m             ████▄  ▄████ █████▀▀▀ ████▄████ \x1B[44m          ",
"           \x1B[40m ▄▄ ▄▄  ▄▄▄▄ ███▀████▀███  ▀████▄   ▀█████▀  \x1B[44m          ",
"           \x1B[40m ██▄██ ██ ▄▄ ███  ▀▀  ███    ▀████ ▄███████▄ \x1B[44m          ",
"           \x1B[40m  ▀█▀  ▀███▀ ███      ███ ███████▀ ███▀ ▀███ \x1B[44m          ",
"           \x1B[40m                                             \x1B[44m         "
};

#define LOGO_LINES_COUNT 8


static void PrintEmptyFrame(bool show_text) {
    // This is used ONLY during Logo Animation now.
    // Print Controls Line
    if (show_text) {
        if (PLFileCount > 1) {
            PrintBoxLine("[↑]NEXT [↓]PREV [←]REW [→]FF [␣]PLAY/PAUSE [⏎]PLAYLIST     [⎋]QUIT");
        } else {
            PrintBoxLine("       [←]REW        [→]FF       [␣]PLAY/PAUSE             [⎋]QUIT");
        }
    } else {
        // If hidden, we must still take up the line space!
        PrintBoxLine("                                                                  ");
    }
    PrintBoxSeparator(); // Line 2
    
     
    if (show_text) {
        PrintBoxLine("vgMSX PLAYER version " VGMSX_VERSION);
        PrintBoxLine("Copyright 2026 by Seb3773");
        PrintBoxLine("23431 Slots free");
        PrintBoxLine("Music ENGINE version 1.0");
        PrintBoxLine("Ok");
        PrintBoxLine("█");
    } else {
        for(int i=0; i<6; i++) PrintBoxLine("                                                                  ");
    }
    PrintBoxSeparator(); // Line 18.
    
     PrintBoxLine("                                                                  "); // Line 19
    PrintBoxBottom(); // Line 20
}

static void PrintMSXError(const char *filename, const char *err_msg) {
  cls();
  
  PrintBoxTop();

  for (int i = 0; i < LOGO_LINES_COUNT; i++) {
        PrintBoxLine("%s", MSXLogoLines[i]);
  }
  PrintBoxSeparator();

  PrintBoxLine("                                                                  ");
  PrintBoxSeparator();

  PrintBoxLine("Music ENGINE version 1.0");
  PrintBoxLine("Ok");
  
  char load_cmd[64];
  if (strlen(filename) > 30) {
      char short_name[31];
      strncpy(short_name, filename, 27);
      strcpy(short_name + 27, "...");
      snprintf(load_cmd, sizeof(load_cmd), "LOAD \"%s\"", short_name);
  } else {
      snprintf(load_cmd, sizeof(load_cmd), "LOAD \"%s\"", filename);
  }
  PrintBoxLine("%s", load_cmd);
  
  PrintBoxLine(err_msg);
  PrintBoxLine("Ok");
  PrintBoxLine("█");
  
  PrintBoxSeparator();
  PrintBoxLine("Error."); // Status line
  PrintBoxBottom();
}

static void PrintLogo(void) {
  static bool animated = false;
  
  PrintBoxTop();

  if (!animated) {
      // Inhibit Input during boot sequence
      tcflush(STDIN_FILENO, TCIFLUSH);
      
      printf("\x1B[?25l"); // Hide Cursor
      // --- PHASE 1: CENTERED BOOT SCREEN (Clean Frame) ---
      for (int i=0; i<5; i++) PrintBoxLine("                                                                  ");
      for (int i=0; i<LOGO_LINES_COUNT; i++) PrintBoxLine("%s", MSXBootLines[i]);
      for (int i=0; i<6; i++) PrintBoxLine("                                                                  ");
      PrintBoxBottom(); // Close frame
      

      usleep(500000); // 0.8s (Centered phase)
      
      UI_GoToLine(1); // Go back to Line 1 (Preserve Top Border)
      
      for (int i=0; i<LOGO_LINES_COUNT; i++) PrintBoxLine("                                                                  ");
      PrintBoxSeparator(); // The segment appears now
      
      PrintEmptyFrame(false);
      
      usleep(200000); 
      UI_GoToLine(10); // Move to Info Start (Controls Line)
      PrintEmptyFrame(true); // 
      
      UI_GoToLine(1); // Move to Line 1 for Anim (Skip Top Border)
      fflush(stdout);

      // 4. Animate Rising vgMSX Logo :-)
      for (int offset = 6; offset >= 0; offset--) {
          for (int i = 0; i < LOGO_LINES_COUNT; i++) {
              int line_idx = i - offset;
              if (line_idx >= 0 && line_idx < LOGO_LINES_COUNT) {
                  PrintBoxLine("%s", MSXLogoLines[line_idx]);
              } else {
                  PrintBoxLine("                                                                  ");
              }
          }
          // Move back to Top for next frame of animation
          if (offset > 0) UI_GoToLine(1); // Keep Line 1 (Skip Top) 
          
          usleep(100000); // Animation speed
          fflush(stdout);
      } // This brace was moved here
      
      // Animation Finished.

      
      animated = true;
      
      // Re-enable/Clear Input after boot
      tcflush(STDIN_FILENO, TCIFLUSH);
      


  } else {
      // Static Print
      for (int i = 0; i < LOGO_LINES_COUNT; i++) {
          PrintBoxLine("%s", MSXLogoLines[i]);
      }
  }

  PrintBoxSeparator();
  // g_CursorRelY += LOGO_LINES_COUNT + 2; // REMOVED (Handled by helpers)
}

// Archive support variables
static char *TempExtractDir = NULL;
static bool IsTempExtraction = false;

// Version header string
static const char VERSION_HEADER[] = 
  "\n         vgMSX v" VGMSX_VERSION " ❖ by seb3773 - MSX only VGM player\n"
  "        ☾═════════════════════════════════════════════☽\n"
  "               ○ based on vgmrips/vgmplay-legacy\n\n";

// Archive format types
typedef enum {
  FMT_ZIP,
  FMT_7Z,
  FMT_UNRAR,
  FMT_TAR_GZ,
  FMT_TAR_BZ2,
  FMT_TAR_XZ,
  FMT_TAR_LZMA,
  FMT_TAR_ZSTD,
  FMT_TAR_LZ4,
  FMT_ZELF,
  FMT_TAR_AUTO
} ArchiveType;

typedef struct {
  const char *ext;
  ArchiveType type;
} ArchiveFormat;

static const ArchiveFormat ARCHIVE_FORMATS[] = {
  // ZIP formats
  {".zip", FMT_ZIP},
  {".jar", FMT_ZIP},
  
  // ZELF format
  {".tar.zlf", FMT_ZELF},
  
  // TAR-based formats
  {".tar", FMT_TAR_AUTO},
  {".tar.gz", FMT_TAR_GZ},
  {".tgz", FMT_TAR_GZ},
  {".tar.bz2", FMT_TAR_BZ2},
  {".tbz2", FMT_TAR_BZ2},
  {".tar.xz", FMT_TAR_XZ},
  {".txz", FMT_TAR_XZ},
  {".tar.lzma", FMT_TAR_LZMA},
  {".tar.zst", FMT_TAR_ZSTD},
  {".tar.zstd", FMT_TAR_ZSTD},
  {".tar.lz4", FMT_TAR_LZ4},
  
  // Standalone compressed formats (treated as TAR for now if they contain structure)
  {".gz", FMT_TAR_GZ},
  {".bz2", FMT_TAR_BZ2},
  {".xz", FMT_TAR_XZ},
  {".lzma", FMT_TAR_LZMA},
  {".zst", FMT_TAR_ZSTD},
  {".zstd", FMT_TAR_ZSTD},
  
  // 7-Zip
  {".7z", FMT_7Z},
  
  // RAR
  {".rar", FMT_UNRAR},
  
  {NULL, FMT_ZIP} 
};

// Archive support helper functions
static const ArchiveFormat* GetArchiveFormat(const char *filename) {
  const char *ext;
  int i;
  
  if (!filename) return NULL;
  for (i = 0; ARCHIVE_FORMATS[i].ext != NULL; i++) {
    size_t ext_len = strlen(ARCHIVE_FORMATS[i].ext);
    size_t fn_len = strlen(filename);
    
    if (fn_len >= ext_len) {
      const char *file_ext = filename + fn_len - ext_len;
      if (strcasecmp(file_ext, ARCHIVE_FORMATS[i].ext) == 0) {
        return &ARCHIVE_FORMATS[i];
      }
    }
  }
  
  return NULL;
}

static bool IsArchiveFile(const char *filename) {
  return GetArchiveFormat(filename) != NULL;
}

static int RunCommand(char *const argv[]) {
  pid_t pid;
  int status;
  
  pid = fork();
  if (pid < 0) return -1;
  
  if (pid == 0) {
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull > 0) {
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      close(devnull);
    }
    
    execvp(argv[0], argv);
    _exit(127);
  }
  
  // Parent
  if (waitpid(pid, &status, 0) == -1) {
    return -1;
  }
  
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return 0; // Success
  }
  
  return -1;
}

static int ExtractArchiveToTemp(const char *archive_path, char *output_dir) {
  char temp_template[] = "/tmp/vgmsx_XXXXXX";
  const ArchiveFormat *fmt;
  char *argv[16]; // 
  int argc = 0;
  char outarg[MAX_PATH + 8]; // For -oPath style args
  if (mkdtemp(temp_template) == NULL) return -1;
  strcpy(output_dir, temp_template);
  
  fmt = GetArchiveFormat(archive_path);
  if (!fmt) return -1;

  switch (fmt->type) {
    case FMT_ZIP:
      argv[argc++] = "unzip";
      argv[argc++] = "-q";
      argv[argc++] = (char*)archive_path;
      argv[argc++] = "-d";
      argv[argc++] = output_dir;
      argv[argc++] = NULL;
      break;
      
    case FMT_7Z:
      snprintf(outarg, sizeof(outarg), "-o%s", output_dir);
      argv[argc++] = "7z";
      argv[argc++] = "x";
      argv[argc++] = "-y";
      argv[argc++] = outarg;
      argv[argc++] = (char*)archive_path;
      argv[argc++] = NULL;
      break;
      
    case FMT_ZELF:
      argv[argc++] = "zelf";
      argv[argc++] = "--unpack";
      argv[argc++] = (char*)archive_path;
      argv[argc++] = "--output";
      argv[argc++] = output_dir;
      argv[argc++] = NULL;
      break;

    case FMT_UNRAR:
      argv[argc++] = "unrar";
      argv[argc++] = "x";
      argv[argc++] = "-o+";
      argv[argc++] = (char*)archive_path;
      argv[argc++] = output_dir;
      argv[argc++] = NULL;
      break;
      
    case FMT_TAR_AUTO:
    case FMT_TAR_GZ:
    case FMT_TAR_BZ2:
    case FMT_TAR_XZ:
    case FMT_TAR_LZMA:
    case FMT_TAR_ZSTD:
    case FMT_TAR_LZ4:
      argv[argc++] = "tar";
      argv[argc++] = "-x";
      
      if (fmt->type == FMT_TAR_GZ) argv[argc++] = "-z";
      else if (fmt->type == FMT_TAR_BZ2) argv[argc++] = "-j";
      else if (fmt->type == FMT_TAR_XZ) argv[argc++] = "-J";
      else if (fmt->type == FMT_TAR_LZMA) argv[argc++] = "--lzma";
      else if (fmt->type == FMT_TAR_ZSTD) {
        argv[argc++] = "-I";
        argv[argc++] = "zstd";
      }
      else if (fmt->type == FMT_TAR_LZ4) {
        argv[argc++] = "-I";
        argv[argc++] = "lz4";
      }
      
      argv[argc++] = "-f";
      argv[argc++] = (char*)archive_path;
      argv[argc++] = "-C";
      argv[argc++] = output_dir;
      argv[argc++] = NULL;
      break;
      
    default:
      return -1;
  }
  
  if (RunCommand(argv) != 0) {
    rmdir(temp_template);
    return -1;
  }
  
  return 0;
}

static void CleanupTempDirectory(const char *temp_dir) {
  char cmd[MAX_PATH + 32];
  if (temp_dir && strlen(temp_dir) > 0) {
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' 2>/dev/null", temp_dir);
    system(cmd);
  }
}

static void ShowHelp(void) {
  printf("%s", VERSION_HEADER);
  
  printf(" vgmsx [OPTIONS] <file|directory|archive>\n\n");
  printf("   <file>       Play a single .vgm/.vgz file\n");
  printf("   <directory>  Play all .vmg/.vgz files in directory\n");
  printf("   <archive>    play files from archive\n\n");
  printf(" *Playing from archive requires the appropriate system decompressor.\n\n");
}

static INT8 stricmp_u(const char *string1, const char *string2);
static INT8 strnicmp_u(const char *string1, const char *string2, size_t count);
static bool FindVGMDir(char *path) {
  while (true) {
    DIR *d = opendir(path);
    if (!d) return false;
    
    struct dirent *dir;
    char *first_subdir = NULL;
    int subdir_count = 0;
    bool vgm_found = false;
    
    while ((dir = readdir(d)) != NULL) {
      if (dir->d_type == DT_REG) {
         const char *ext = strrchr(dir->d_name, '.');
         if (ext && (!stricmp_u(ext, ".vgm") || !stricmp_u(ext, ".vgz"))) {
           vgm_found = true;
           break; // Found music!
         }
      } else if (dir->d_type == DT_DIR && dir->d_name[0] != '.') {
        // Count non-hidden subdirectories
        if (subdir_count == 0) first_subdir = strdup(dir->d_name);
        subdir_count++;
      }
    }
    closedir(d);
    
    if (vgm_found) {
      if (first_subdir) free(first_subdir);
      return true; // Found music here!
    }
    
    if (subdir_count == 1 && first_subdir) {
      if (strlen(path) + strlen(first_subdir) + 2 < MAX_PATH) {
        strcat(path, DIR_STR);
        strcat(path, first_subdir);
        free(first_subdir);
        continue; 
      }
      free(first_subdir);
      return false; // Path too long
    }
    
    if (first_subdir) free(first_subdir);
    return false; // No music and not exactly one subdir (dead end or ambiguity...)
  }
}

static bool OpenDirectoryAsPlaylist(const char *DirPath);
static bool OpenMusicFile(const char *FileName);
extern bool OpenVGMFile(const char *FileName);
static void wprintc(const wchar_t *format, ...);
static void PrintChipStr(UINT8 ChipID, UINT8 SubType, UINT32 Clock);
const wchar_t *GetTagStrEJ(const wchar_t *EngTag, const wchar_t *JapTag);
static void ShowVGMTag(void);
static void PlayVGM_UI(void);
INLINE INT8 sign(double Value);
INLINE long int Round(double Value);
static void PrintMinSec(UINT32 SamplePos, UINT32 SmplRate);

extern UINT32 SampleRate; 
extern UINT32 VGMPbRate;
extern UINT32 VGMMaxLoop;
extern UINT32 CMFMaxLoop;
UINT32 FadeTimeN;  // normal fade time
UINT32 FadeTimePL; // in-playlist fade time
extern UINT32 FadeTime;
UINT32 PauseTimeJ; // Pause Time for Jingles
UINT32 PauseTimeL; // Pause Time for Looping Songs
extern UINT32 PauseTime;
extern float VolumeLevel;
extern bool SurroundSound;
extern UINT8 HardStopOldVGMs;
extern bool FadeRAWLog;
extern bool PauseEmulate;
extern bool DoubleSSGVol;
static UINT16 ForceAudioBuf;
static UINT8 OutputDevID;
extern UINT8 ResampleMode; // 00 - HQ both, 01 - LQ downsampling, 02 - LQ both
extern UINT8 CHIP_SAMPLING_MODE;
extern INT32 CHIP_SAMPLE_RATE;
extern bool FMBreakFade;
extern float FMVol;
extern bool FMOPL2Pan;
extern CHIPS_OPTION ChipOpts[0x02];
extern UINT16 AUDIOBUFFERU;
extern UINT32 SMPL_P_BUFFER;
extern char SoundLogFile[MAX_PATH];
extern UINT8 OPL_MODE;
extern UINT8 OPL_CHIPS;
UINT8 NEED_LARGE_AUDIOBUFS;
extern char *AppPaths[8];
static char AppPathBuffer[MAX_PATH * 2];
static char PLFileBase[MAX_PATH];
char PLFileName[MAX_PATH];
UINT32 PLFileCount;
static char **PlayListFile;
UINT32 CurPLFile;
static UINT8 NextPLCmd;
UINT8 PLMode; // set to 1 to show Playlist text
static bool FirstInit;
extern bool AutoStopSkip;
char VgmFileName[MAX_PATH];
static UINT8 FileMode;
extern VGM_HEADER VGMHead;
extern UINT32 VGMDataLen;
extern UINT8 *VGMData;
extern GD3_TAG VGMTag;
static bool PreferJapTag;
static bool StreamStarted;
extern float MasterVol;
extern UINT32 VGMPos;
extern INT32 VGMSmplPos;
extern INT32 VGMSmplPlayed;
extern INT32 VGMSampleRate;
extern UINT32 BlocksSent;
extern UINT32 BlocksPlayed;
static bool IsRAWLog;
extern bool EndPlay;
extern bool PausePlay;
extern bool FadePlay;
extern bool ForceVGMExec;
// extern UINT8 PlayingMode; // Removed

extern UINT32 PlayingTime;

extern UINT32 FadeStart;
extern UINT32 VGMMaxLoopM;
extern UINT32 VGMCurLoop;
extern float VolumeLevelM;
bool ErrorHappened; // used by VGMPlay.c and VGMPlay_AddFmts.c
extern float FinalVol;
extern bool ResetPBTimer;

static struct termios oldterm;
static bool termmode;

static volatile bool sigint = false;

UINT8 CmdList[0x100];

extern UINT8 IsVGMInit;

static bool PrintMSHours;

static void signal_handler(int signal) {
  if (signal == SIGINT || signal == SIGTERM || signal == SIGHUP)
    sigint = true;
}

int main(int argc, char *argv[]) {
  int argbase;
  int ErrRet;
  char *AppName;
  char *AppPathPtr;
  const char *StrPtr;
  const char *FileExt;
  UINT8 CurPath;
  UINT32 ChrPos;
  char *DispFileName;
  setlocale(LC_CTYPE, "");
  tcgetattr(STDIN_FILENO, &oldterm);
  termmode = false;
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGHUP, signal_handler);

  if (argc > 1) {
    if (!stricmp_u(argv[1], "-v") || !stricmp_u(argv[1], "-version") ||
        !stricmp_u(argv[1], "--version")) {
      printf("%s", VERSION_HEADER);
      return 0;
    }
    if (!stricmp_u(argv[1], "-h") || !stricmp_u(argv[1], "--help")) {
      ShowHelp();
      return 0;
    }
  }


  VGMPlay_Init();

  CurPath = 0x00;
  AppPathPtr = AppPathBuffer;

  // Path 2: exe's directory
  AppName = GetAppFileName(); // "C:\VGMPlay\VGMPlay.exe"
  StrPtr = strrchr(AppName, DIR_CHR);
  if (StrPtr != NULL) {
    ChrPos = StrPtr + 1 - AppName;
    strncpy(AppPathPtr, AppName, ChrPos);
    AppPathPtr[ChrPos] = 0x00; // "C:\VGMPlay\"
    AppPaths[CurPath] = AppPathPtr;
    CurPath++;
    AppPathPtr += ChrPos + 1;
  }

  AppPathPtr[0] = '\0';
  AppPaths[CurPath] = AppPathPtr;
  CurPath++;

  VolumeLevel = 1.0f;
  FadeTimeN = 5000;
  FadeTimePL = 3000;
  PauseTimeJ = 500;
  HardStopOldVGMs = 0x01;
  FadeRAWLog = false;

  VGMMaxLoop = 0x02;
  ResampleMode = 0x01;
  CHIP_SAMPLING_MODE = 0x00;
  CHIP_SAMPLE_RATE = 44100;
  OutputDevID = 0x00;
  SurroundSound = false;
  PauseEmulate = false;

  FMVol = 1.0f;
  FMOPL2Pan = true;
  FMBreakFade = true;
  PreferJapTag = false;
  ForceAudioBuf = 0x00;
  if (CHIP_SAMPLE_RATE <= 0)
    CHIP_SAMPLE_RATE = SampleRate;
  VGMPlay_Init2();

  ErrRet = 0;
  argbase = 0x01;

  if (argc <= argbase) {
    if (termmode)
      tcsetattr(STDIN_FILENO, TCSANOW, &oldterm);
    printf("\n       vgMSX v0.1 ❖ by seb3773\n");
    printf("         MSX only VGM player\n");
    printf("       ☾═════════════════════☽\n");
    printf(" ○ based on vgmrips/vgmplay-legacy ○\n\n");
    printf("No input file/folder provided, exiting.\n\n");
    return 1;
  } else {
    struct stat statbuf;
 
    strcpy(VgmFileName, argv[argbase]);
    argbase++;

    if (IsArchiveFile(VgmFileName) && stat(VgmFileName, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
      char temp_dir[MAX_PATH];
      printf("Detected archive: %s\n", VgmFileName);
      printf("Extracting...");
      fflush(stdout);
      
      if (ExtractArchiveToTemp(VgmFileName, temp_dir) == 0) {
        printf(" done.\n");
        strcpy(VgmFileName, temp_dir);
        TempExtractDir = strdup(temp_dir);
        IsTempExtraction = true;

        if (!FindVGMDir(VgmFileName)) {
             cls();
             StartAudioWarmup();
             PrintLogo();
             PrintMSXError(VgmFileName, "Bad file name");
             ErrRet = 1;
             goto ExitProgram;
        }

        if (!OpenDirectoryAsPlaylist(VgmFileName)) {
          cls();
          StartAudioWarmup();
          PrintLogo();
          PrintMSXError(VgmFileName, "File not found");
          ErrRet = 1;
          goto ExitProgram;
        }
        PLMode = 0x01;
      } else {
        cls();
        StartAudioWarmup();
        PrintLogo();
        PrintMSXError(VgmFileName, "Device I/O error");
        ErrRet = 1;
        goto ExitProgram;
      }
    } else if (stat(VgmFileName, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
      if (!OpenDirectoryAsPlaylist(VgmFileName)) {
        cls();
        StartAudioWarmup();
        PrintLogo();
        PrintMSXError(VgmFileName, "Disk offline");
        ErrRet = 1;
        goto ExitProgram;
      }
      PLMode = 0x01;
    }
  }
  if (!strlen(VgmFileName))
    goto ExitProgram;
  StandardizeDirSeparators(VgmFileName);

  changemode(true);

  FirstInit = true;
  StreamStarted = false;


  if (PLMode == 0x00) {
    cls();
    StartAudioWarmup();
    PrintLogo();
    if (!OpenMusicFile(VgmFileName)) {
      PrintMSXError(VgmFileName, "File not found");
      if (argv[0][1] == ':')
        _getch();
      ErrRet = 1;
      goto ExitProgram;
    }

    ErrorHappened = false;
    FadeTime = FadeTimeN;
    PauseTime = PauseTimeL;
    PrintMSHours =
        (VGMHead.lngTotalSamples >= 158760000); 
    NextPLCmd = 0x80;
    PlayVGM_UI();

    CloseVGMFile();
  } else {

    CurPLFile = 0x00;
    CurPLFile = 0x00;
    UI_SetLine(0); // Initial State
    
    bool NeedRewind = false;

    while (CurPLFile < PLFileCount) {
      if (NeedRewind) {
          UI_GoToLine(0);
      }
      NeedRewind = true;
      
      fflush(stdout);
      StartAudioWarmup();
      UI_SetLine(0); 
      PrintLogo();
      
      if (!PlayListFile || !PlayListFile[CurPLFile]) {
          CurPLFile++;
          continue;
      }

      if (IsAbsolutePath(PlayListFile[CurPLFile])) {
        strcpy(VgmFileName, PlayListFile[CurPLFile]);
      } else {
        strcpy(VgmFileName, PLFileBase);
        strcat(VgmFileName, PlayListFile[CurPLFile]);
      }

      if (!OpenMusicFile(VgmFileName)) {
        printf("Error opening the file: %s\n", VgmFileName);
        _getch();
        while (_kbhit())
          _getch();
        CurPLFile++; // Skip failed file
        continue;
      }

      ErrorHappened = false;
      if (CurPLFile < PLFileCount - 1)
        FadeTime = FadeTimePL;
      else
        FadeTime = FadeTimeN;
      PauseTime = VGMHead.lngLoopOffset ? PauseTimeL : PauseTimeJ;
      PrintMSHours = (VGMHead.lngTotalSamples >= 158760000);
      NextPLCmd = 0x00;
      PlayVGM_UI();

      CloseVGMFile();

      if (NextPLCmd == 0x01) { // Previous Track
        if (CurPLFile > 0)
          CurPLFile--;
        // else stay at 0
      } else if (NextPLCmd == 0xFF) { // Quit
        break;
      } else {
        CurPLFile++;
      }
    }
    UI_GoToLine(20);
    PrintBoxBottom();
    printf("\x1B[?25h"); // Show Cursor
    StopStream();
    StreamStarted = false;
    StopVGM();
    if (IsTempExtraction && TempExtractDir) {
        CleanupTempDirectory(TempExtractDir);
        free(TempExtractDir);
    }
    
    changemode(false);   // Restore Term
    exit(0);

  } 
  
  if (ErrorHappened && argv[0][1] == ':') {
    if (_kbhit())
      _getch();
    _getch();
  }

#ifdef _DEBUG
  printf("Press any key ...");
  _getch();
#endif

ExitProgram:
  if (IsTempExtraction && TempExtractDir) {
    CleanupTempDirectory(TempExtractDir);
    free(TempExtractDir);
    TempExtractDir = NULL;
  }

  printf("\x1B[23;1H");

  printf("\x1B[?25h"); // Show Cursor
  changemode(false);
  VGMPlay_Deinit();
  free(AppName);
  return ErrRet;
}

static void RemoveNewLines(char *String) {
  char *StrPtr;

  StrPtr = String + strlen(String) - 1;
  while (StrPtr >= String && (UINT8)*StrPtr < 0x20) {
    *StrPtr = '\0';
    StrPtr--;
  }

  return;
}

static void RemoveQuotationMarks(char *String) {
  UINT32 StrLen;
  char *EndQMark;

  if (String[0x00] != QMARK_CHR)
    return;

  StrLen = strlen(String);
  memmove(String, String + 0x01, StrLen);
  EndQMark = strrchr(String, QMARK_CHR);
  if (EndQMark != NULL)
    *EndQMark = 0x00; 

  return;
}

char *GetLastDirSeparator(const char *FilePath) {
  char *SepPos1;
  char *SepPos2;

  SepPos1 = strrchr(FilePath, '/');
  SepPos2 = strrchr(FilePath, '\\');
  if (SepPos1 < SepPos2)
    return SepPos2;
  else
    return SepPos1;
}

static bool IsAbsolutePath(const char *FilePath) {
  if (!FilePath) return false;
  if (FilePath[0] == '/')
    return true; // absolute UNIX path
  return false;
}

static char *GetFileExtension(const char *FilePath) {
  char *DirSepPos;
  char *ExtDotPos;

  DirSepPos = GetLastDirSeparator(FilePath);
  if (DirSepPos == NULL)
    DirSepPos = (char *)FilePath;
  ExtDotPos = strrchr(DirSepPos, '.');
  if (ExtDotPos == NULL)
    return NULL;
  else
    return ExtDotPos + 1;
}

static void StandardizeDirSeparators(char *FilePath) {
  char *CurChr;

  CurChr = FilePath;
  while (*CurChr != '\0') {
    if (*CurChr == '\\' || *CurChr == '/')
      *CurChr = DIR_CHR;
    CurChr++;
  }

  return;
}

static char *GetAppFileName(void) {
  char *AppPath;

  ssize_t len;
  AppPath = calloc(MAX_PATH, sizeof(char));
  if (AppPath == NULL)
    return NULL;

  len = readlink("/proc/self/exe", AppPath, MAX_PATH - 1);
  if (len != -1)
    AppPath[len] = '\0';

  return AppPath;
}

static void cls(void) {
  int retVal;

  retVal = system("clear");

  return;
}

static void changemode(bool dir) {
  static struct termios newterm;

  if (termmode == dir)
    return;

  if (dir) {
    newterm = oldterm;
    newterm.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOCTL | ISIG);
    newterm.c_cc[VMIN] = 0;
    newterm.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &newterm);
  } else {
    tcsetattr(STDIN_FILENO, TCSANOW, &oldterm);
  }
  termmode = dir;

  return;
}

static int _kbhit(void) {
  struct timeval tv;
  fd_set rdfs;
  int kbret;
  bool needchg;

  needchg = (!termmode);
  if (needchg)
    changemode(true);
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  FD_ZERO(&rdfs);
  FD_SET(STDIN_FILENO, &rdfs);

  select(STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
  kbret = FD_ISSET(STDIN_FILENO, &rdfs);
  if (needchg)
    changemode(false);

  return kbret;
}

static int _getch(void) {
  int ch;
  bool needchg;

  needchg = (!termmode);
  if (needchg)
    changemode(true);
  ch = getchar();
  if (needchg)
    changemode(false);

  return ch;
}

static INT8 stricmp_u(const char *string1, const char *string2) {
  const char *StrPnt1;
  const char *StrPnt2;
  char StrChr1;
  char StrChr2;

  StrPnt1 = string1;
  StrPnt2 = string2;
  while (true) {
    StrChr1 = toupper(*StrPnt1);
    StrChr2 = toupper(*StrPnt2);

    if (StrChr1 < StrChr2)
      return -1;
    else if (StrChr1 > StrChr2)
      return +1;
    if (StrChr1 == 0x00)
      return 0;

    StrPnt1++;
    StrPnt2++;
  }

  return 0;
}

static INT8 strnicmp_u(const char *string1, const char *string2, size_t count) {
  const char *StrPnt1;
  const char *StrPnt2;
  char StrChr1;
  char StrChr2;
  size_t CurChr;

  StrPnt1 = string1;
  StrPnt2 = string2;
  CurChr = 0x00;
  while (CurChr < count) {
    StrChr1 = toupper(*StrPnt1);
    StrChr2 = toupper(*StrPnt2);

    if (StrChr1 < StrChr2)
      return -1;
    else if (StrChr1 > StrChr2)
      return +1;
    if (StrChr1 == 0x00)
      return 0;

    StrPnt1++;
    StrPnt2++;
    CurChr++;
  }

  return 0;
}

static int compare_strings(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

static bool OpenDirectoryAsPlaylist(const char *DirPath) {
  DIR *d;
  struct dirent *dir;
  size_t PLAlloc;
  char DirPathFull[MAX_PATH];
  char *ext;

  strcpy(DirPathFull, DirPath);
  StandardizeDirSeparators(DirPathFull);
  if (DirPathFull[0] != '\0' &&
      DirPathFull[strlen(DirPathFull) - 1] != DIR_CHR) {
    strcat(DirPathFull, DIR_STR);
  }
  strcpy(PLFileBase, DirPathFull);

  d = opendir(DirPathFull);
  if (!d)
    return false;

  PLAlloc = 0x0100;
  PLFileCount = 0x00;
  PlayListFile = (char **)malloc(PLAlloc * sizeof(char *));

  while ((dir = readdir(d)) != NULL) {
    if (dir->d_type == DT_REG) {
      ext = strrchr(dir->d_name, '.');
      if (ext && (stricmp_u(ext, ".vgm") == 0 || stricmp_u(ext, ".vgz") == 0)) {
        if (PLFileCount >= PLAlloc) {
          PLAlloc += 0x0100;
          PlayListFile =
              (char **)realloc(PlayListFile, PLAlloc * sizeof(char *));
        }
        PlayListFile[PLFileCount] = strdup(dir->d_name);
        PLFileCount++;
      }
    }
  }
  closedir(d);

  if (PLFileCount == 0) {
    free(PlayListFile);
    PlayListFile = NULL;
    return false;
  }

  qsort(PlayListFile, PLFileCount, sizeof(char *), compare_strings);
  CurPLFile = 0x00;
  return true;
}

static bool OpenMusicFile(const char *FileName) {
  if (OpenVGMFile(FileName))
    return true;
  return false;
}

static void wprintc(const wchar_t *format, ...) {
  va_list arg_list;
  int RetVal;
  UINT32 BufSize;
  wchar_t *printbuf;

  BufSize = 0x00;
  printbuf = NULL;
  do {
    BufSize += 0x100;
    printbuf = (wchar_t *)realloc(printbuf, BufSize * sizeof(wchar_t));
    va_start(arg_list, format);
    RetVal = _vsnwprintf(printbuf, BufSize - 0x01, format, arg_list);
    va_end(arg_list);
  } while (RetVal == -1 && BufSize < 0x1000);
  printf("%ls", printbuf);

  free(printbuf);

  return;
}

static void PrintChipStr(UINT8 ChipID, UINT8 SubType, UINT32 Clock) {
  if (!Clock)
    return;

  if (ChipID == 0x00 && (Clock & 0x80000000))
    Clock &= ~0x40000000;
  if (Clock & 0x80000000) {
    Clock &= ~0x80000000;
    ChipID |= 0x80;
  }

  if (Clock & 0x40000000)
    printf("2x");
  printf("%s, ", GetAccurateChipName(ChipID, SubType));

  return;
}

const wchar_t *GetTagStrEJ(const wchar_t *EngTag, const wchar_t *JapTag) {
  const wchar_t *RetTag;

  if (EngTag == NULL || !wcslen(EngTag)) {
    RetTag = JapTag;
  } else if (JapTag == NULL || !wcslen(JapTag)) {
    RetTag = EngTag;
  } else {
    if (!PreferJapTag)
      RetTag = EngTag;
    else
      RetTag = JapTag;
  }

  if (RetTag == NULL)
    return L"";
  else
    return RetTag;
}

static void DrawPlaylist(void) {
  
  if (!PLFileCount) {
    UI_GoToLine(12);
    PrintBoxLine("Error: No Playlist Loaded");
    return;
  }

  int startIdx = (int)CurPLFile - 2;
  if (startIdx < 0) startIdx = 0;
  


  for (int i = 0; i < 6; i++) {
    UI_GoToLine(12 + i);
    
    int trackIdx = startIdx + i;
    
    if (trackIdx < PLFileCount) {
       char lineBuf[128];
       const char* fname = strrchr(PlayListFile[trackIdx], '/');
       fname = fname ? fname + 1 : PlayListFile[trackIdx];
       bool isCurrent = (trackIdx == CurPLFile);
       
       if (isCurrent) {
           snprintf(lineBuf, 128, "%3d│ %s", trackIdx+1, fname);
           
           printf("│");
           
           printf("\x1B[30;107m");
           
           printf("%s", lineBuf);
           
           int len = GetU8Width(lineBuf);
           int pad = BOX_INNER_WIDTH - len;
           if (pad < 0) pad = 0;
           for(int k=0; k<pad; k++) printf(" ");
           
           printf("\x1B[0m");
           
           printf("│\n");
           UI_IncLine();
           
       } else {
           snprintf(lineBuf, 128, "%3d│ %s", trackIdx+1, fname);
           PrintBoxLine(lineBuf);
       }
    } else {
       PrintBoxLine(" ");
    }
  }
}

static void ShowVGMTag(void) {
  const wchar_t *TitleTag;
  const wchar_t *GameTag;
  const wchar_t *AuthorTag;
  const wchar_t *SystemTag;
  UINT8 CurChip;
  UINT32 ChpClk;
  UINT8 ChpType;
  INT16 VolMod;
#ifdef SET_CONSOLE_TITLE
  wchar_t TitleStr[0x80];
  UINT32 StrLen;
#endif

  TitleTag = GetTagStrEJ(VGMTag.strTrackNameE, VGMTag.strTrackNameJ);
  GameTag = GetTagStrEJ(VGMTag.strGameNameE, VGMTag.strGameNameJ);
  AuthorTag = GetTagStrEJ(VGMTag.strAuthorNameE, VGMTag.strAuthorNameJ);
  SystemTag = GetTagStrEJ(VGMTag.strSystemNameE, VGMTag.strSystemNameJ);


  UI_GoToLine(10);

  if (PLFileCount > 1) {
    PrintBoxLine("[↑]NEXT [↓]PREV [←]REW [→]FF [␣]PLAY/PAUSE [⏎]PLAYLIST     [⎋]QUIT");
  } else {
    PrintBoxLine("       [←]REW        [→]FF       [␣]PLAY/PAUSE             [⎋]QUIT");
  }
  PrintBoxSeparator();

  {
    const char *FileNamePtr = strrchr(VgmFileName, '/');
    if (FileNamePtr == NULL)
      FileNamePtr = VgmFileName;
    else
      FileNamePtr++;

    if (PLFileCount > 1) {
      PrintBoxLine("Track %02u/%02u: %s", CurPLFile + 1, PLFileCount,
                   FileNamePtr);
    } else {
      PrintBoxLine("%s", FileNamePtr);
    }
    PrintBoxLineW(L"\"%ls\"", TitleTag);
  }
  PrintBoxLineW(L"Game: %ls (%ls)", GameTag, VGMTag.strReleaseDate);
  PrintBoxLineW(L"Composer: %ls", AuthorTag);
  PrintBoxLine("Loop: %s", VGMHead.lngLoopOffset ? "Yes" : "No");

  {
    char chips_buf[512] = "Used chips: ";
    char chip_name[64];
    bool first = true;
    for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip++) {
      ChpClk = GetChipClock(&VGMHead, CurChip, &ChpType);
      if (ChpClk && GetChipClock(&VGMHead, 0x80 | CurChip, NULL))
        ChpClk |= 0x40000000;

      if (ChpClk) {
        if (!first)
          strcat(chips_buf, ", ");
        const char *name = "Unknown";
        switch (CurChip) {
        case 0x00:
          name = "SN76489";
          break;
        case 0x01:
          name = "YM2413";
          break;
        case 0x02:
          name = "YM2612";
          break;
        case 0x03:
          name = "YM2151";
          break;
        case 0x04:
          name = "YM2203";
          break;
        case 0x05:
          name = "YM2608";
          break;
        case 0x06:
          name = "YM2610";
          break;
        case 0x07:
          name = "YM3812";
          break;
        case 0x08:
          name = "YM3526";
          break;
        case 0x09:
          name = "Y8950";
          break;
        case 0x0A:
          name = "YMF262";
          break;
        case 0x0D:
          name = "AY-3-8910";
          break;
        case 0x11:
          name = "K051649";
          break;
        }
        strcat(chips_buf, name);
        first = false;
      }
    }
    PrintBoxLine("%s", chips_buf);
  }

  PrintBoxSeparator();

  return;
}

#define LOG_SAMPLES (SampleRate / 5)
static void PlayVGM_UI(void) {
  INT32 VGMPbSmplCount;
  INT32 PlaySmpl;
  UINT8 KeyCode;
  UINT32 VGMPlaySt;
  UINT32 VGMPlayEnd;
  char WavFileName[MAX_PATH];
  char *TempStr;
  WAVE_16BS *TempBuf;
  UINT8 RetVal;
  UINT32 TempLng;
  bool PosPrint;
  bool LastUninit;
  bool QuitPlay;
  UINT32 PlayTimeEnd;

  printf("\x1B[?25l");
  fflush(stdout);

  PlayVGM();
  DBus_EmitSignal(SIGNAL_SEEK | SIGNAL_METADATA | SIGNAL_PLAYSTATUS |
                  SIGNAL_CONTROLS);

  AUDIOBUFFERU = 10;
  if (AUDIOBUFFERU < NEED_LARGE_AUDIOBUFS)
    AUDIOBUFFERU = NEED_LARGE_AUDIOBUFS;
  if (ForceAudioBuf && AUDIOBUFFERU)
    AUDIOBUFFERU = ForceAudioBuf;

  switch (FileMode) {
  case 0x00: // VGM
    IsRAWLog = (!VGMHead.lngLoopOffset && (wcslen(VGMTag.strSystemNameE) ||
                                           wcslen(VGMTag.strSystemNameJ)));
    break;
  case 0x01: // CMF
    IsRAWLog = false;
    break;
  case 0x02: // DRO
    IsRAWLog = true;
    break;
  }
  if (!VGMHead.lngTotalSamples)
    IsRAWLog = false;

    if (FirstInit || !StreamStarted) {
      
      int attempts = 0;
      UINT8 RetVal;
      
      while (attempts < 3) {
          RetVal = StartStream(OutputDevID);
          
          if (!RetVal) break;
          
          // FAILURE HANDLING
          attempts++;

          UI_GoToLine(12);
          PrintBoxLine("23431 Slots free");
          PrintBoxLine("Music ENGINE version 1.0");
          PrintBoxLine("Ok");
          
          char LoadStr[128];
          const char* fname = strrchr(VgmFileName, '/');
          fname = fname ? fname + 1 : VgmFileName;
          snprintf(LoadStr, 128, "LOAD \"%s\"", fname);
          PrintBoxLine(LoadStr); 

          if (attempts == 1) {
              PrintBoxLine("Device I/O error");
              PrintBoxLine("Retrying...");     
          }
          
          if (attempts == 2) {
              UI_GoToLine(12);
              PrintBoxLine("Ok");
              
              char LoadStr[128];
              const char* fname = strrchr(VgmFileName, '/');
              fname = fname ? fname + 1 : VgmFileName;
              snprintf(LoadStr, 128, "LOAD \"%s\"", fname);
              PrintBoxLine(LoadStr); 
              
              PrintBoxLine("Device I/O error"); 
              PrintBoxLine("Retrying...");      
              
              PrintBoxLine("Device I/O error"); 
              PrintBoxLine("Retrying...");      
          }
          
          if (attempts == 3) {
              UI_GoToLine(12);
              PrintBoxLine("Retrying...");
              
              PrintBoxLine("Device I/O error"); 
              PrintBoxLine("Retrying...");      
              
              PrintBoxLine("Device I/O error"); 
              PrintBoxLine("Ok");               
              PrintBoxLine("█");                
          }
          
          fflush(stdout);
       
          usleep(1500000); 
      }

      if (RetVal) {
        UI_GoToLine(19);
        PrintBoxLine("Error: can't open sound device");
          
        UI_GoToLine(20);
        PrintBoxBottom();
        UI_GoToLine(20);
        PrintBoxBottom();
        
        printf("\x1B[?25h"); 
        changemode(false);   
        exit(1);
      }
      StreamStarted = true;
    }

    if (IsPlaylistMode) {
        DrawPlaylist();
    } else {
        ShowVGMTag();
    }

    PauseStream(PausePlay);
  FirstInit = false;

  VGMPlaySt = VGMPos;
  if (VGMHead.lngGD3Offset)
    VGMPlayEnd = VGMHead.lngGD3Offset;
  else
    VGMPlayEnd = VGMHead.lngEOFOffset;
  VGMPlayEnd -= VGMPlaySt;
  if (!FileMode)
    VGMPlayEnd--; 
  PosPrint = true;

  PlayTimeEnd = 0;
  QuitPlay = false;
  while (!QuitPlay) {
    DBus_ReadWriteDispatch();
    if (sigint) {
      QuitPlay = true;
      NextPLCmd = 0xFF;
    }

    if (!PausePlay || PosPrint) {
      UINT32 CurSec;
      static UINT32 LastSec = 0xFFFFFFFF;

      PlaySmpl = (BlocksSent - BlocksPlayed) * SMPL_P_BUFFER;
      PlaySmpl = VGMSmplPlayed - PlaySmpl;
      if (!VGMCurLoop) {
        if (PlaySmpl < 0)
          PlaySmpl = 0;
      } else {
        while (PlaySmpl < SampleVGM2Playback(VGMHead.lngTotalSamples -
                                             VGMHead.lngLoopSamples))
          PlaySmpl += SampleVGM2Playback(VGMHead.lngLoopSamples);
      }

      CurSec = PlaySmpl / SampleRate;
      if (CurSec != LastSec || PosPrint) {
        PosPrint = false;
        LastSec = CurSec;

        VGMPbSmplCount = SampleVGM2Playback(VGMHead.lngTotalSamples);

        char status_buf[512]; 
        char time_cur[32];
        char time_tot[32];

        UINT32 TempInt = (UINT32)((double)PlaySmpl / SampleRate + 0.5);
        UINT32 Min = TempInt / 60;
        UINT8 Sec = TempInt % 60;
        if (PrintMSHours && Min >= 60)
          sprintf(time_cur, "%u:%02u:%02u", Min / 60, Min % 60, Sec);
        else
          sprintf(time_cur, "%02u:%02u", Min, Sec);

        TempInt = (UINT32)((double)VGMPbSmplCount / SampleRate + 0.5);
        Min = TempInt / 60;
        Sec = TempInt % 60;
        if (PrintMSHours && Min >= 60)
          sprintf(time_tot, "%u:%02u:%02u", Min / 60, Min % 60, Sec);
        else
          sprintf(time_tot, "%02u:%02u", Min, Sec);

        UINT8 PausePlay = 0x00;
        snprintf(status_buf, sizeof(status_buf), "%s - %s / %s",
                 PausePlay ? "Paused " : "Playing", time_cur, time_tot);

        int text_len = GetU8Width(status_buf);
        int bar_space = BOX_INNER_WIDTH - text_len - 2; 

        if (bar_space > 0) {
            strcat(status_buf, "  "); 
            
            int filled_len = 0;
            if (VGMPbSmplCount > 0) {
                filled_len = (int)((long long)PlaySmpl * bar_space / VGMPbSmplCount);
            }
            if (filled_len > bar_space) filled_len = bar_space;
            if (filled_len < 0) filled_len = 0;
            
            for (int k=0; k<filled_len; k++) {
                strcat(status_buf, "▬");
            }
        }

        UI_GoToLine(19); 
        
        printf("│");
        printf("\x1B[97;44m"); // Colors
        printf("%s", status_buf);

        text_len = GetU8Width(status_buf); 
        int pad = BOX_INNER_WIDTH - text_len;
        if (pad < 0) pad = 0;
        for (int i = 0; i < pad; i++) printf(" ");

        printf("\x1B[0m"); // Reset
        printf("│\n"); 
        
        UI_IncLine(); 
        
        fflush(stdout);
      }

    } else {
    }

    if (EndPlay) {
      if (!PlayTimeEnd) {
        PlayTimeEnd = PlayingTime;
        if (!PLFileCount || CurPLFile >= PLFileCount - 0x01) {
          if (FileMode == 0x01)
            PlayTimeEnd += SampleRate << 1; 
          PlayTimeEnd += AUDIOBUFFERU * SMPL_P_BUFFER;
        }
      }

      if (PlayingTime >= PlayTimeEnd)
        QuitPlay = true;
    }
#define KEY_UP 0x1000
#define KEY_DOWN 0x1001
#define KEY_LEFT 0x1002
#define KEY_RIGHT 0x1003

    {
      int KeyCode = 0;
      char kbuf[16];
      ssize_t klen = 0;

      if (_kbhit()) { 
        klen = read(STDIN_FILENO, kbuf, sizeof(kbuf));
        if (klen > 0) {
          if (kbuf[0] == 0x03) { 
             KeyCode = 0x1B; 
          } else if (kbuf[0] == 0x0A || kbuf[0] == 0x0D) {
             if (PLFileCount > 0) {
                 KeyCode = 0x0A; 
             }
          } else if (kbuf[0] == 0x1B) {
            if (klen == 1) {
              KeyCode = 0x1B; 
            } else if (klen >= 3 && kbuf[1] == '[') {
              switch (kbuf[2]) {
              case 'A':
                KeyCode = KEY_UP;
                break;
              case 'B':
                KeyCode = KEY_DOWN;
                break;
              case 'C':
                KeyCode = KEY_RIGHT;
                break;
              case 'D':
                KeyCode = KEY_LEFT;
                break;
              default:
                KeyCode = 0;
                break;
              }
            }
          } else if (kbuf[0] == ' ') {
            KeyCode = ' ';
          } else {
             KeyCode = 0; 
          }
        }
      }

      if (KeyCode) {
        INT32 SeekOffset = 0;
        
        if (KeyCode == 0x0A) {
            IsPlaylistMode = !IsPlaylistMode;
            if (IsPlaylistMode) {
                DrawPlaylist();
            } else {
                ShowVGMTag();
            }
        }

        switch (KeyCode) {
        case 0x1B: 
          QuitPlay = true;
          NextPLCmd = 0xFF; 
          break;
        case KEY_LEFT:
          SeekOffset = -5;
          break;
        case KEY_RIGHT:
          SeekOffset = 5;
          break;
        case KEY_UP: // Next Track
          if (PLFileCount && CurPLFile < PLFileCount - 0x01) {
            NextPLCmd = 0x00;
            QuitPlay = true;
          }
          SeekOffset = 0;
          break;
        case KEY_DOWN: // Prev Track
          if (PLFileCount && CurPLFile) {
            NextPLCmd = 0x01;
            QuitPlay = true;
          }
          SeekOffset = 0;
          break;
        case ' ': // Space
          PauseVGM(!PausePlay);
          PosPrint = true;
          DBus_EmitSignal(SIGNAL_PLAYSTATUS);
          break;
        default:
          break;
        }

        if (SeekOffset) {
          SeekVGM(true, SeekOffset * SampleRate);
          PosPrint = true;
          DBus_EmitSignal(SIGNAL_SEEK);
        }
      }
    }

    if (FadeRAWLog && IsRAWLog && !PausePlay && !FadePlay && FadeTimeN) {
      PlaySmpl =
          (INT32)VGMHead.lngTotalSamples - FadeTimeN * VGMSampleRate / 1500;
      if (VGMSmplPos >= PlaySmpl) {
        FadeTime = FadeTimeN;
        FadePlay = true; 
      }
    }
    usleep(20000);
  }

  // --- Final "Finished." Status Line ---
  UI_GoToLine(19);
  
  printf("│");
  printf("\x1B[97;44m"); 
  const char* final_msg = "Finished.";
  printf("%s", final_msg);
  int pad = BOX_INNER_WIDTH - GetU8Width(final_msg);
  for(int i=0; i<pad; i++) printf(" ");
  printf("\x1B[0m"); 
  printf("│\n"); 
  
  UI_SetLine(20); 
  

  UI_GoToLine(21);
  
  fflush(stdout); 
  LastUninit = (NextPLCmd & 0x80) || !PLFileCount ||
               (NextPLCmd == 0x00 && CurPLFile >= PLFileCount - 0x01);
  if (LastUninit) {
    StopStream();
    StreamStarted = false;
  }

  StopStream();
  StreamStarted = false;
  StopVGM();
  printf("\x1B[?25h"); 



  return;
}

INLINE INT8 sign(double Value) {
  if (Value > 0.0)
    return 1;
  else if (Value < 0.0)
    return -1;
  else
    return 0;
}

INLINE long int Round(double Value) {
  if (Value > 0.0)
    return (long int)(Value + 0.5);
  else
    return (long int)(Value - 0.5);
}

static void PrintMinSec(UINT32 SamplePos, UINT32 SmplRate) {
  UINT32 TempInt;
  UINT32 Min;
  UINT8 Sec;

  TempInt = (UINT32)((double)SamplePos / SmplRate + 0.5);
  Min = TempInt / 60;
  Sec = TempInt % 60;

  if (PrintMSHours && Min >= 60)
    printf("%u:%02u:%02u", Min / 60, Min % 60, Sec);
  else
    printf("%02u:%02u", Min, Sec);

  return;
}

// --- DBus Stubs ---
void DBus_ReadWriteDispatch(void) {}
void DBus_EmitSignal(UINT8 type) {}
