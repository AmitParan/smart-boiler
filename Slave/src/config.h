#ifndef CONFIG_H
#define CONFIG_H

// חיישנים
#define PIN_TEMP_BUS    4    // חיישני טמפרטורה
#define PIN_FLOW_SENSOR 13   // חיישן זרימה (הזזנו מ-16 כי 16 זה RX)
#define PIN_SSR         26   // ממסר

// תקשורת (UART2 Default)
#define SLAVE_RX        16   // מתחבר ל-TX של המאסטר
#define SLAVE_TX        17   // מתחבר ל-RX של המאסטר

#endif