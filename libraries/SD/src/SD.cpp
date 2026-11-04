#include "SD.h"

namespace fs {

void SDFS::end()
{
    FatFS::end();
    _card.end();
}

} // namespace fs

fs::SDFS SD;
