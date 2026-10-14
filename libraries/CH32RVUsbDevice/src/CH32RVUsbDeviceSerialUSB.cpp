/* SerialUSB on its own: the linker takes this file only when a sketch names
 * it. Declaring it attaches a CDC port to USBDevice. */
#include "CH32RVUsbDevice.h"

CH32RVUsbDeviceCdcSerial SerialUSB(USBDevice);
