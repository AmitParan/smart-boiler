#ifndef UI_MANAGER_H
#define UI_MANAGER_H

void UI_Init();
void UI_UpdateWaterTemp(float value);
void UI_UpdateWeather(float temp, const char* condition);
void UI_SetBoilerState(bool is_on);
void UI_SetHeatingStatus(bool is_heating);
void UI_UpdateTime(int hour, int minute);
void UI_UpdateDate(const char* date_str);
void UI_UpdateWiFiStatus();
void fetchWeather();
void checkScreensaver();

// Extended home screen updates (all live packet fields)
void UI_UpdateBoostTemp(float value);
void UI_UpdateFlowRate(float value);
void UI_UpdateSystemMode(const char* mode);
void UI_UpdateSSRStatus(bool internal_on, bool boost_on);
void UI_UpdateSensorData(float t_internal, float t_boost, float flow);

#endif