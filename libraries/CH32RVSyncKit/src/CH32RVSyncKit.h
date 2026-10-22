/* CH32RVSyncKit: the small parts that connect interrupts and loop() on one
 * core - queues, notifications, byte buffers, a block pool, short locks,
 * critical sections, deadlines and periods.
 *
 * Nothing here blocks, allocates or runs on its own. Every operation tries
 * once and returns, and the sketch decides when to look again. That is how a
 * small CH32 does several things at once without an RTOS - and the same
 * habits (an ISR only signals, the work happens in normal code, a full queue
 * is an answer, not a wait) carry over to ESP32SyncKit and FreeRTOS later.
 *
 * Parts that are not used cost nothing: each is a header-only template, and
 * an object you do not declare is not linked. */
#pragma once

#include "CH32RVSyncKitCommon.h"
#include "CH32RVSyncKitNotify.h"
#include "CH32RVSyncKitPool.h"
#include "CH32RVSyncKitQueue.h"
#include "CH32RVSyncKitRing.h"
#include "CH32RVSyncKitTime.h"
