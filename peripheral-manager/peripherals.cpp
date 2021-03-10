#include "peripherals.hpp"

namespace phosphor
{
namespace nic
{

void Nic::setSensorValueToDbus(const int8_t value)
{
    ValueIface::value(value);
}

} // namespace nic
} // namespace phosphor
