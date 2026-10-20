
#pragma once
#include <stdint.h>

namespace battery {

constexpr uint16_t FULL_MV = 4200;
constexpr uint16_t EMPTY_MV = 3500;

struct Point {
  uint16_t mv;
  uint8_t percent;
};

// 일반적인 4.2V 완충형 Li-Po의 예시 추정 곡선
constexpr Point CURVE[] = {
  {3500,   0},
  {3600,   8},
  {3700,  20},
  {3800,  40},
  {3900,  60},
  {4000,  75},
  {4100,  90},
  {4200, 100}
};

inline uint8_t percent(uint32_t mv) {
  if (mv <= EMPTY_MV) return 0;
  if (mv >= FULL_MV) return 100;

  for (unsigned i = 1; i < sizeof(CURVE) / sizeof(CURVE[0]); ++i) {
    if (mv <= CURVE[i].mv) {
      const Point& a = CURVE[i - 1];
      const Point& b = CURVE[i];

      return a.percent +
        (mv - a.mv) * (b.percent - a.percent) /
        (b.mv - a.mv);
    }
  }

  return 100;
}

struct Tracker {
  uint32_t minMv = UINT32_MAX;
  uint32_t maxMv = 0;

  void observe(uint32_t mv) {
    if (mv < 2500 || mv > 5500) return;

    if (mv < minMv) minMv = mv;
    if (mv > maxMv) maxMv = mv;
  }
};

} // namespace battery
