/* The platform version, in the shape of arduino-esp32's esp_arduino_version.h:
 *
 *   #if CH32RV_VERSION >= CH32RV_VERSION_VAL(0, 1, 0)
 *   ...
 *   #endif
 *   Serial.println(CH32RV_VERSION_STR);          // "0.0.5"
 *
 * The three numbers arrive from platform.txt (compiler.version_defines, written
 * from its version= line when a release is built), so a library can test the
 * core it is compiled against. */
#pragma once

#if !defined(CH32RV_VERSION_MAJOR) || !defined(CH32RV_VERSION_MINOR) || !defined(CH32RV_VERSION_PATCH)
#error "CH32RV_VERSION_MAJOR/MINOR/PATCH come from platform.txt (compiler.version_defines)"
#endif

#define CH32RV_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define CH32RV_VERSION \
    CH32RV_VERSION_VAL(CH32RV_VERSION_MAJOR, CH32RV_VERSION_MINOR, CH32RV_VERSION_PATCH)

#define CH32RV_VERSION_XSTR_(x) #x
#define CH32RV_VERSION_STR_(a, b, c) CH32RV_VERSION_XSTR_(a) "." CH32RV_VERSION_XSTR_(b) "." CH32RV_VERSION_XSTR_(c)
#define CH32RV_VERSION_STR \
    CH32RV_VERSION_STR_(CH32RV_VERSION_MAJOR, CH32RV_VERSION_MINOR, CH32RV_VERSION_PATCH)
