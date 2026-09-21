#pragma once

#include <cstdint>

namespace patronus::utils
{

  inline double TicksToMilliseconds(uint64_t start_ticks, uint64_t end_ticks, uint64_t ticks_per_second)
  {
    if (ticks_per_second == 0 || end_ticks < start_ticks)
      return 0.;
    
    return static_cast<double>(end_ticks - start_ticks) * 1000. / static_cast<double>(ticks_per_second);
  }

}  // namespace patronus::utils