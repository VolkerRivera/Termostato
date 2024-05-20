#ifndef HARDWARE_H 
#define HARDWARE_H
//Definición pines ESP32
#define DHTPIN 26
#define DHTTYPE DHT11

#define TIME_TO_SLEEP 300 //tiempo en segundos
#define s_us_factor 1000000 // factor de conversion s->us / us ->s
#define TIME_SERVER_ON 120 //tiempo en segundos
#define PERIODO_SERVER 6 //

/*El servidor se encendera durante TIME_SERVER_ON (s) cada TIME_TO_SLEEP*PERIODO_SERVER*/

#define PIN_RELE 23

#define JOY_X_PIN 34
#define JOY_Y_PIN 35
#define JOY_SW_PIN 27

#define BUZZER_PIN 13

//Definición estados Termostato
#define APAGADO 0
#define ENCENDIDO 1
#define CONF 2

#endif
