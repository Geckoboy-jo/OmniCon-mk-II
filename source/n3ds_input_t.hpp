#pragma once

#include <cstdint>

struct n3ds_input_t {
  bool a;
  bool b;
  bool x;
  bool y;
  bool start;
  bool select;
  bool up;
  bool down;
  bool left;
  bool right;
  bool l;
  bool r;
  bool zl;
  bool zr;
  bool touchPressed;
  int16_t touchX; // libretro pointer range: -0x7fff to +0x7fff
  int16_t touchY;
};
