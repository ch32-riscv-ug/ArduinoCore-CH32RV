#include "before-policy.hpp"
#include "../../../../../tests/unit/core_redesign/connection_x0.hpp"
template<unsigned C,bool Touch> using ExerciseApplication=experiment::ConnectionX0<C,disconnect_policy::Policy,Touch>;
