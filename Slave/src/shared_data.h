#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <Arduino.h>

// משתנים שכל ה-Tasks יכולים לגשת אליהם
extern float current_flow;    // קצב זרימה (מחושב ע"י Flow Task)
extern float temps[3];        // טמפרטורות (מחושב ע"י Temp Task)

// אובייקט לנעילת פסיקות (קריטי לזרימה)
extern portMUX_TYPE timerMux;

#endif