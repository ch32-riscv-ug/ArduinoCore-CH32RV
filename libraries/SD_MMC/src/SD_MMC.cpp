#include "SD_MMC.h"

namespace fs {

void SDMMCFS::end()
{
    FatFS::end();
    _card.end();
}

} // namespace fs

fs::SDMMCFS SD_MMC;
