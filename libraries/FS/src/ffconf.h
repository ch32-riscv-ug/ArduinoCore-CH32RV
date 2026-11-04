/* FatFs configuration for the FS library (ours; FatFs itself is fatfs/,
 * vendored). Every value can be overridden from the sketch's build_opt.h
 * (-DFF_FS_EXFAT=0 and the like, docs/adr/0007). The defaults split the
 * series by flash: on the 16-64 KB parts exFAT, GPT, timestamps and mkfs
 * are off and one volume is mounted at a time; the rest have them. Sizes
 * are in docs/filesystem.ja.md. */
#pragma once
#define FFCONF_DEF 80386

#include "FSConfig.h"

#ifndef FF_FS_READONLY
#define FF_FS_READONLY 0
#endif
#define FF_FS_MINIMIZE 0
#define FF_USE_FIND 0
#ifndef FF_USE_MKFS
#define FF_USE_MKFS (!FS_SMALL_FLASH && !FF_FS_READONLY)
#endif
#define FF_USE_FASTSEEK 0
#define FF_USE_EXPAND 0
#define FF_USE_CHMOD 0
#define FF_USE_LABEL 0
#define FF_USE_FORWARD 0
#define FF_USE_STRFUNC 0
#define FF_PRINT_LLI 0
#define FF_PRINT_FLOAT 0
#define FF_STRF_ENCODE 0

/* Names: long names on, the API in UTF-8; short names in code page 437. */
#ifndef FF_CODE_PAGE
#define FF_CODE_PAGE 437
#endif
/* Long names need a 512-byte work buffer: not on the 2 KB V003, which sees
 * 8.3 names (a long-named file is reached by its short name, FILENA~1.TXT). */
#ifndef FF_USE_LFN
#if defined(CH32RV_SERIES_CH32V003)
#define FF_USE_LFN 0
#else
#define FF_USE_LFN 1
#endif
#endif
#define FF_MAX_LFN 255
#ifndef FF_LFN_UNICODE
#define FF_LFN_UNICODE 2
#endif
#define FF_LFN_BUF 255
#define FF_SFN_BUF 12
#define FF_FS_RPATH 0
#define FF_PATH_DEPTH 10

/* Volumes mounted at once ("0:", "1:"): an SD card and a USB stick. */
#ifndef FF_VOLUMES
#define FF_VOLUMES (FS_SMALL_FLASH ? 1 : 2)
#endif
#define FF_STR_VOLUME_ID 0
#define FF_MULTI_PARTITION 0

#define FF_MIN_SS 512
#define FF_MAX_SS 512
#define FF_USE_TRIM 0

/* Files share the volume's sector buffer on the small series (a file is
 * 50 B instead of 600; reading two files by turns rereads sectors). */
#ifndef FF_FS_TINY
#define FF_FS_TINY FS_SMALL_FLASH
#endif
#ifndef FF_FS_EXFAT
#define FF_FS_EXFAT (!FS_SMALL_FLASH)
#endif
/* GPT (drives formatted on a Mac, or over 2 TB); FatFs ties it to exFAT. */
#ifndef FF_LBA64
#define FF_LBA64 FF_FS_EXFAT
#endif
#define FF_MIN_GPT 0x10000000

/* Timestamps from time() (get_fattime() in FatFS.cpp); fixed without. */
#ifndef FF_FS_NORTC
#define FF_FS_NORTC FS_SMALL_FLASH
#endif
#define FF_NORTC_MON 1
#define FF_NORTC_MDAY 1
#define FF_NORTC_YEAR 2026
#define FF_FS_CRTIME 0
#define FF_FS_NOFSINFO 0

/* Refuse a second open of a file being written (and its removal). */
#ifndef FF_FS_LOCK
#define FF_FS_LOCK (FS_SMALL_FLASH ? 0 : FS_MAX_OPEN_FILES)
#endif
#define FF_FS_REENTRANT 0
#define FF_FS_TIMEOUT 1000
