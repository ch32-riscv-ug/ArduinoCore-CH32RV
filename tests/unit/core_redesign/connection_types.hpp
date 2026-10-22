#pragma once
#include <stdint.h>
namespace connection_experiment {
enum class Attach : uint8_t { Accepted,Already,Unsupported,Exhausted };
struct Command {int16_t kind=0,value=0;};
}
