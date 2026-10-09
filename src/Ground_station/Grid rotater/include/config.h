#pragma once
#include <Arduino.h>

double tracker_lat = 47.501142;
double tracker_lon = 18.016608;
float tracker_alt = 180;

const float YAW_OFFSET = 90;
const float PITCH_OFFSET = 0;
const bool  INVERT_PITCH = true;

const float TOLERANCE_YAW = 5.0;   
const float TOLERANCE_PITCH = 3.0;

const double R_EARTH = 6371000.0;

const uint8_t LED_COUNT = 8;