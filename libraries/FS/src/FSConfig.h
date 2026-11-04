/* Build-time sizes of the FS library, shared by the API and FatFs's
 * configuration (ffconf.h). Each can be set from the sketch's build_opt.h. */
#pragma once

#if defined(CH32RV_SERIES_CH32V002) || defined(CH32RV_SERIES_CH32V003) || defined(CH32RV_SERIES_CH32V004) || \
    defined(CH32RV_SERIES_CH32V005) || defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || \
    defined(CH32RV_SERIES_CH32M007) || defined(CH32RV_SERIES_CH32X033) || defined(CH32RV_SERIES_CH32X035) || \
    defined(CH32RV_SERIES_CH32M030)
#define FS_SMALL_FLASH 1
#else
#define FS_SMALL_FLASH 0
#endif

/* Files and directories open at once, over all volumes. A directory being
 * listed holds one, and `e = dir.openNextFile()` opens the next entry before
 * the previous one is let go: listing takes three. A FatFs file is about
 * 600 B of RAM, or 50 B with FF_FS_TINY (the small series' default, where
 * the files share the volume's sector buffer). */
#ifndef FS_MAX_OPEN_FILES
#if defined(CH32RV_SERIES_CH32V003)
#define FS_MAX_OPEN_FILES 2   /* 2 KB of RAM: one file and its directory */
#else
#define FS_MAX_OPEN_FILES 4
#endif
#endif
/* The longest path a File keeps (path(), name()), with the terminator;
 * opening a longer one fails. */
#ifndef FS_PATH_MAX
#if defined(CH32RV_SERIES_CH32V003)
#define FS_PATH_MAX 40
#else
#define FS_PATH_MAX (FS_SMALL_FLASH ? 64 : 128)
#endif
#endif
