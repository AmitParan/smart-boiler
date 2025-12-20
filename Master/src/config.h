#ifndef CONFIG_H
#define CONFIG_H

// פינים לתקשורת UART עם ה-Slave (במחבר J2)
// TX (שולח) מחובר ל-RX של הסלייב
// RX (מקבל) מחובר ל-TX של הסלייב
#define MASTER_RX_PIN 10  
#define MASTER_TX_PIN 17  

// קצב רענון מסך (במילי-שניות) לנעילת LVGL
#define UI_REFRESH_RATE 500

#endif