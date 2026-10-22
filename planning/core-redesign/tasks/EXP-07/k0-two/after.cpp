#include "../../../../../tests/unit/core_redesign/k0_slave_register.hpp"
constexpr unsigned ExerciseCapacity=2;
template<unsigned C,class Gate> using ExerciseApplication=experiment::K0Shared<C,ExerciseCapacity,Gate>;
template<unsigned C,class Gate,class Registers,unsigned Admission>
using ExercisePort=experiment::K0SlaveRegister<ExerciseApplication<C,Gate>,Registers,Admission,ExerciseCapacity>;
