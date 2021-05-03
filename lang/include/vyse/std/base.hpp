#pragma once
#include "../forward.hpp"

namespace vyse {
namespace stdlib {

Value print(VM& vm, int argc);
Value require(VM& vm, int argc);
Value setproto(VM& vm, int argc);

}
}; // namespace snap