#ifndef CONFIG_H
#define CONFIG_H

// Pins for UART communication with the Slave (on connector J2)
// TX (transmit) connected to RX of the slave
// RX (receive) connected to TX of the slave
#define MASTER_RX_PIN 10  
#define MASTER_TX_PIN 17  

// Display refresh rate (in milliseconds) for LVGL lock
#define UI_REFRESH_RATE 500

#endif