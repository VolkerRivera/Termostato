  #include <WiFi.h>
  #include <WiFiClient.h>
  #include <WiFiUDP.h>
  #include <WebServer.h>
  #include <NTPClient.h>
  #include <SPIFFS.h>
  #include "hardware.h"
  #include <TimerEvent.h>
  #include <ESP32Time.h>
  #include <DHT.h>
  #include <Wire.h> 
  #include <LiquidCrystal_I2C.h>
  #include <ezButton.h>
  #include <esp_sleep.h>
  #include <esp_pm.h>
  #include <esp_wifi.h>
  #include <esp_wifi_types.h>
  #include <esp_bt.h>
  

  //Definicion de funciones de callback del web server
  void envioInicial();
  void actualizarUmbral();
  void actualizarGrafico();
  void callback_server();
  void conectarWiFi();
  boolean isServerOn = false;
  
  //Definicion de funciones del apartado de temperatura
  void callback_t_h();

  //Definicion de funciones para activar la caldera
  void caldera(boolean On);
  void caldera_cb();

  //Funciones para hardware
  void actualizarLCD();
  void callbackMuestreo();
  void callbackInactividad();
  void cambiarUmbral(float umb);

  //Constantes de wifi
  //const char* ssid = "RedmiNote11";
  //const char* password = "lucas2001";
  const char* ssid = "XiaomiPad5";
  const char* password = "12345678";

  //Variables para gestionar el tiempo dentro del cual debe despertar el micro cuando se despierta por pulsacion entre medida y medida
  uint64_t time_aux = 0; // momento en el que se ha hecho la ultima medida.
  uint64_t wakeup_time = 0; //momento en el que estando LCD OFF, despertamos al micro
  uint64_t next_wakeup = 0; //momento en el que ha de hacerse la siguiente medida
  
  WebServer server(80);
  WiFiUDP ntp;
  NTPClient ntpClient(ntp, "0.es.pool.ntp.org", 3600,  1000);
  TimerEvent timerMedida;
  TimerEvent timerRele;
  TimerEvent timerMuestreo;
  TimerEvent timerInactividad;
  TimerEvent timerServerEncendido;
  ESP32Time rtc;
  DHT dht(DHTPIN, DHTTYPE);
  LiquidCrystal_I2C lcd(0x27,16,2);
  ezButton button(JOY_SW_PIN);
  
  String pWeb;
  float ultimaTemp = 25.00;
  float ultimaHumedad = 40.00;
  float umbral = 20.00;
  int diaMes = 0;
  int timeH = 0;
  int contOn = 0;
  boolean calderaOn = false;
  int estado = 0;
  char cadena[50];
  int datosGraf[7] = {12,12,12,12,12,12,12};
  uint8_t numMedidas = 0;

  const int centro = 1800;

  void setup(void) {
    
    //Configuracion del pin de salida del relé y buzzer
    pinMode(PIN_RELE,OUTPUT);
    digitalWrite(PIN_RELE,LOW);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN,LOW);
    
    //Inicio de la comunicación en serie para depuracion 
    Serial.begin(9600);
    delay(500);

    randomSeed( (unsigned long)( micros()%millis() ) );

    //Configuracion del LCD y DHT
    dht.begin();
    lcd.init(); 

    //Lectura de la página web de la memoria flash
    if(!SPIFFS.begin()){
      Serial.println("Error al abrir el sistema de ficheros");
      return;
    }
    Serial.println("Sistema de archivos abierto correctamente");
    File f = SPIFFS.open("/PaginaWeb.html", "r");
    if(!f){
      Serial.println("Error al abrir el archivo pagina web");
      return;
    }
    Serial.println("Pagina Web abierto correctamente.");
    while((boolean)f.available()){
      pWeb+=(char)f.read();
    }
    f.close();
    Serial.println("Archivo cerrado correctamente");

    //Establecer conexion con la red WiFi e iniciar el servidor
    conectarWiFi();

    //Conexion con el servidor SNTP para obtener la hora, configuracion del RTC y desconexion del SNTP
    ntpClient.begin();
    ntpClient.update();
    while(!ntpClient.isTimeSet()){
      ntpClient.update();
      delay(100);
    }
    rtc.setTime(ntpClient.getEpochTime());
    Serial.println("Hora fijada en el RTC:" + rtc.getDateTime(true));
    ntpClient.end();

    
    diaMes = rtc.getDayofWeek();//Uso real
    //diaMes = rtc.getMinute()%7;//Solo test

    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * s_us_factor);//segundos * factor_seg_to_micro
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_27, LOW);
    Serial.println("Se ha configurado el WakeUp Source");
    
    // Habilitar el modo de sueño del módem
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    callback_t_h();//realizamos la primera medida
    btStop();
  }
  
  void loop(void) {
    //Actualizaciones del servidor, contadores y boton
    server.handleClient();
    timerMedida.update();
    timerRele.update();
    timerInactividad.update();
    timerMuestreo.update();
    timerServerEncendido.update();
    button.loop();

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    //Si salta el timer del sleep mode
    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
      Serial.println("El timer ha despertado al micro.");
      if(!isServerOn){//servidor apagado
        callback_t_h();
        if(numMedidas == 4){
          Serial.print("t_actual:");
          Serial.println((String)(esp_timer_get_time()/s_us_factor));
          numMedidas = 0;
          conectarWiFi();//Una vez conectado lo dejamos encendido durante TIME_SERVER_ON y luego lo apagamos
        }else{
          Serial.println("ESP32 despertara dentro de " + (String)TIME_TO_SLEEP + " segundos.");
          delay(10);
          esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * s_us_factor);
          esp_light_sleep_start();
        }
      }
    }
    
    if(wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || isServerOn){
      /*Si ponemos el print aqui nos saldran muchos mensajes repetidos puesto que es antes de conformar el pulso y, por tanto,
      leera varios niveles bajos ya que durante una pulsacion pasan varios ciclos de reloj.*/ 
      if (button.isPressed()) {
        
        Serial.println("El pulsador ha despertado al micro.");
        
        if(timerInactividad.isEnabled() == false){
          wakeup_time = esp_timer_get_time();//tiempo que ha pasado desde que lo dormi
          uint64_t timeUntilNextMeasure = next_wakeup - wakeup_time;//timepo en us
          timerMedida.set((timeUntilNextMeasure)/1000,callback_t_h);//tiempo que falta hasta la asiguiente medida en ms
          Serial.println("-------------------");
          Serial.print("time_aux:");
          Serial.println((String)(time_aux/s_us_factor));
          Serial.print("next_wakeup:");
          Serial.println((String)(next_wakeup/s_us_factor));
          Serial.print("wakeup_time:");
          Serial.println((String)(wakeup_time/s_us_factor));
          Serial.println("Primera pulsacion ha despertado al micro. Se ha activado timer normal para tomar medida dentro de " + (String)(timeUntilNextMeasure/s_us_factor) + " segundos.");
          Serial.println("-------------------");
        }
        
        timerInactividad.set(10000,callbackInactividad);//Cada vez que pulsamos el switch se resetea el timer de t_retroiluminacion
        if(estado==ENCENDIDO){
          timerMuestreo.set(250,callbackMuestreo);
          noTone(BUZZER_PIN);
          estado=CONF;
        }else if(estado==CONF){
          timerMuestreo.disable();
          if (ultimaTemp < umbral){
            tone(BUZZER_PIN, 1000, 1000); //Buzzer es un modo de aviso de que se ha activado la caldera
          }else{
            noTone(BUZZER_PIN);
          } 
          estado=ENCENDIDO;
        }else if(estado==APAGADO){
          estado=ENCENDIDO;
          Serial.println("Timer muestreo encendido");
        }
        actualizarLCD();
        }
      }
  }

  //Callback de envio de la página web
  void envioInicial(){
    Serial.println("Pagina inicial");
    server.send(200,"text/html",pWeb);
    
  }

  //Callback de actualizacion del umbral en la página web
  void actualizarUmbral(){
    if(!server.hasArg("Vumbral") || server.arg("Vumbral") == NULL){
      server.send(400,"text/plain", "400:Invalid Request");
    }else{
      cambiarUmbral(atof(server.arg("Vumbral").c_str()));
      Serial.print(" Nuevo umbral: ");
      Serial.println(umbral);
    }
    server.send(200,"text/html",pWeb);
    
  }

  //Callback del timer para realizar la medida de temperatura y humedad
  void callback_t_h(){

    //Obtenemos las medidas de temperatura y humedad
    float hum = dht.readHumidity();
    float temp = dht.readTemperature();

    numMedidas++;//en la sexta medida (cada media hora) encendemos el servidor durante TIME_SERVER_ON min
    
    char auxHum1[6];
    char auxHum2[6]; 
    char auxTemp1[6];
    char auxTemp2[6];

    //dtostrf float->String con index 0-3 con 2 digitos despues del punto decimal. float(ultimaTemp) -> char [] auxTemp1
    dtostrf(ultimaTemp,5,2,auxTemp1);
    dtostrf(temp,5,2,auxTemp2);
    dtostrf(ultimaHumedad,5,2,auxHum1);
    dtostrf(hum,5,2,auxHum2);

    Serial.println(auxHum1);
    Serial.println(auxHum2);
    Serial.println(auxTemp1);
    Serial.println(auxTemp2);

    //Actualizamos los datos del html
    pWeb.replace("<p> TEMPERATURA: " + String(auxTemp1), "<p> TEMPERATURA: " + String(auxTemp2));
    pWeb.replace(String("<p> HUMEDAD: ") + auxHum1, String("<p> HUMEDAD: ") + auxHum2);
    ultimaTemp = temp;
    ultimaHumedad = hum;

    Serial.println("Temperatura y Humedad leidas: "+ String(ultimaTemp) + "ºC  "+ String(ultimaHumedad) + "%");

    //Si no superamos el umbral encendemos la temperatura
    String aux;
    if(temp <= umbral){//cada 5 min
      caldera(true);//Se envia flag de que se debe encender la caldera  
      if(diaMes == rtc.getDayofWeek()){//Uso real
      //if(diaMes == (rtc.getMinute()%7)){//Solo test
        contOn++;
      }else{
        //timeH = contOn/4; //Solo pruebas
        timeH = contOn/12;
        
        //Se guardan las horas de caldera usadas durante el dia anterior
        datosGraf[diaMes] = timeH;
        File datafile = SPIFFS.open("/UsageData.txt", "w");
        if(!datafile){
          Serial.println("No ha podido abrirse el fichero de datos de uso de la caldera.");
        }
        for(int i = 0; i<7; i++){
          aux = (String)i + " "+ datosGraf[i] + "\n";
          Serial.println(aux);
          datafile.print(aux);
        }
        Serial.println("Horas de uso del dia " + (String)diaMes + " registradas correctamente. Contador ON: " + String(contOn));
        datafile.flush();
        datafile.close();
        //Se actualiza el valor del dia en el que estamos y se reinicia el contador de tiempo calderaOn
        diaMes = rtc.getDayofWeek();//Uso real
        //diaMes = rtc.getMinute()%7;//Solo test
        contOn = 0;
      }
    }else{
      caldera(false);
    }
    
    /*Tanto si se esta midiendo con la pantalla encendida como con la pantalla apagada (micro dormido y despierta para medir cada TIME_TO_SLEEP)
    tenemos que registrar el tiempo en el que acabamos de medir y cuando sera el proximo*/
    time_aux = esp_timer_get_time();
    next_wakeup = time_aux + (TIME_TO_SLEEP*s_us_factor);

    /*Solo dormiremos el micro cuando el timerMedida (habilitado cuando la pantalla esta encendida) este desabilitado*/
    if(!timerMedida.isEnabled()){
      //Siempre que hace la medida estando "apagado" pone el despertador para el time_to_sleep completo
      //esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * s_us_factor);
      //esp_light_sleep_start();
    }else{
      timerMedida.set(TIME_TO_SLEEP*1000,callback_t_h) ;
      Serial.println("---------------------------------------------");
      Serial.println("Se ha hecho una medida con LCD ON y se ha programado otra con timer normal para dentro de " + (String)TIME_TO_SLEEP + " segundo.");
      Serial.println("---------------------------------------------");
    }

    /*Si se han hecho N medidas y toca encender el servidor para permitir mostrar y cambiar umbral:
    - Proceso de conexion
    - Una vez conectado, hacer el primer envio*/
    
    Serial.println("Medida " + (String)(numMedidas) + "/6 hecha");
  }

  void actualizarGrafico(){
    String resp;

    File datafile = SPIFFS.open("/UsageData.txt","r");
    if(!datafile){
      Serial.println("No se ha podido abrir el fichero UsageData para leerlo.");
      return;
     }
      
    while (datafile.available()) {
      String linea = datafile.readStringUntil('\n');

      // Divide la línea en palabras usando el espacio como delimitador
      int espacioIndex = linea.indexOf(' ');//posicion del espacio
      if (espacioIndex != -1) {
        // Obtiene la segunda palabra que debería ser el valor numérico
        String valor = linea.substring(espacioIndex + 1);//la posicion siguiente del espacio son las horas calderaOn

        // Agrega el valor al string final
        resp += valor + "\n";//las concatenamos separadas por un \n
      }
    }

    // Cierra el archivo
    datafile.close();
    server.send(200,"text/plain",resp);
    Serial.println("Enviado:");
    Serial.println(resp);
    
  }
  
  void caldera(boolean On){//activar. Se llama en callback_t_h y cambiar_umbral
    if(On != calderaOn){//activar true - caldera apagada;;;; activar false - caldera encendida
      digitalWrite(PIN_RELE,HIGH);//se acciona el rele para que apague encienda o apague
      calderaOn= not calderaOn; //se cambia el estado de la caldera apagada <-> encendida
      if(calderaOn){ //si la caldera esta encendida
        pWeb.replace("alt=\"Off\"", "alt=\"On\""); //se envia pagina web con el cambio para la imagen
      }else{
        pWeb.replace("alt=\"On\"", "alt=\"Off\"");
      }
      timerRele.set(100,caldera_cb);
    }
  }
  void caldera_cb(){
    digitalWrite(PIN_RELE,LOW);
  }

  void actualizarLCD(){
    lcd.clear();
    if(estado==CONF){
      snprintf(cadena, sizeof(cadena), "Umbral Temp.");
      Serial.println(cadena);
      lcd.setCursor(2, 0);
      lcd.print(cadena);
      Serial.println(cadena);
      snprintf(cadena, sizeof(cadena), "%.2f", umbral);
      Serial.println(cadena);
      lcd.setCursor(5, 1);
      lcd.print(cadena);
      Serial.println(cadena);
    }else if(estado==ENCENDIDO){
      snprintf(cadena, sizeof(cadena), "Temp.: %.2f ", ultimaTemp);
      lcd.setCursor(0, 0);
      lcd.print(cadena);
      lcd.setCursor(13, 0);
      lcd.print("C");
      Serial.println(cadena);
      snprintf(cadena, sizeof(cadena), "Humedad: %.2f", ultimaHumedad);
      lcd.setCursor(0, 1);
      lcd.print(cadena);
      lcd.setCursor(14, 1);
      lcd.print("%");
      lcd.backlight();
      Serial.println(cadena);
    }else if(estado==APAGADO){
      lcd.clear();
      lcd.noBacklight();
    }
    
  }
  
  void callbackMuestreo(){
    int x_adc_val; //Valor X potenciómetro
    int y_adc_val; //Valor Y potenciómetro
  
    x_adc_val = analogRead(JOY_X_PIN);
    y_adc_val = analogRead(JOY_Y_PIN);
  
    if (y_adc_val > (centro + 1000) && ((centro-400) < x_adc_val) && ((centro+400) > x_adc_val)) {
      timerInactividad.reset();
      cambiarUmbral(umbral+1);
      actualizarLCD();
    } else if (y_adc_val < (centro - 1000) && ((centro-400) < x_adc_val) && ((centro+400) > x_adc_val)) {
      timerInactividad.reset();
      cambiarUmbral(umbral-1);
      actualizarLCD();
    }
  }
  
  void callbackInactividad(){
    estado = APAGADO;
    timerInactividad.disable();
    timerMuestreo.disable();
    timerMedida.disable();
    actualizarLCD();
    
    //Si ha llegado aqui es porque ha pasado por los 10 seg inactivo

    if(!isServerOn){//si el server esta apagado podemos mandar a dormir el micro
      uint64_t t_pantallaoff = esp_timer_get_time();
      uint64_t time_to_sleep = next_wakeup - t_pantallaoff;
      if(time_to_sleep == 0){
        time_to_sleep = TIME_TO_SLEEP;
      }
      Serial.println("------------------------------------------------------------------------------------------");
      Serial.print("time_aux:");
      Serial.println((String)(time_aux/s_us_factor));
      Serial.print("next_wakeup:");
      Serial.println((String)(next_wakeup/s_us_factor));
      Serial.print("t_pantallaoff: ");
      Serial.println((String)(t_pantallaoff/s_us_factor));
      Serial.print("time_to_sleep: ");
      Serial.println((String)(time_to_sleep/s_us_factor));
      Serial.println("LCD OFF, ESP32 va a dormit y se despertara dentro de " + (String)(time_to_sleep/s_us_factor) + " segundos.");
      Serial.println("------------------------------------------------------------------------------------------");
      esp_sleep_enable_timer_wakeup(time_to_sleep);
      esp_light_sleep_start();
    }
    //Si el server coincide que esta encendido, simplemente apagamos la pantalla pero el micro sigue encendido porque si no el server se apagaria
  }
  void cambiarUmbral(float umb){
    char auxUmb1[6];
    char auxUmb2[6];
    dtostrf(umbral,5,2,auxUmb1);
    dtostrf(umb,5,2,auxUmb2);
    pWeb.replace(String("<p id=\"umbralActual\"> ACTUAL: ") + auxUmb1, String("<p id=\"umbralActual\"> ACTUAL: ") + auxUmb2);
    umbral = umb;
    caldera((umbral>ultimaTemp));
  }

  void conectarWiFi(){
    //Activacion del Wifi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("");
    Serial.print("t_actual:");
    Serial.println((String)(esp_timer_get_time()/s_us_factor));
    
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  
    //Configuracion del servidor web
    server.on("/", envioInicial);
    
    server.on("/umbral", HTTP_POST, actualizarUmbral);

    server.on("/actualizar",HTTP_GET, actualizarGrafico);

    //Activacion del servidor
    server.begin();
    isServerOn = true;
    timerServerEncendido.set(TIME_SERVER_ON*1000, callback_server);
    Serial.println("HTTP server started");
    Serial.println("Se apagará dentro de " + (String)(TIME_SERVER_ON) + " segundos.");
    Serial.println("................................");  
    }

  void callback_server(){
    WiFi.mode(WIFI_OFF);
    isServerOn = false;
    timerServerEncendido.disable();
    
    uint64_t time_to_sleep_aux = next_wakeup - esp_timer_get_time();//directamente en microsegundos
   
    Serial.println("----------------------"); 
    Serial.print("time_aux:");
    Serial.println((String)(time_aux/s_us_factor));
    Serial.print("t_actual:");
    Serial.println((String)(esp_timer_get_time()/s_us_factor));
    Serial.print("next_wakeup:");
    Serial.println((String)(next_wakeup/s_us_factor));

    Serial.println("Servidor apagado. Proxima medida dentro de " + (String)(time_to_sleep_aux/s_us_factor) + " segundos. ");
    Serial.println("----------------------");

    /*Se mandara a dormir para que se despierte dentro de next_wake_up - t_ServerOff*/
    esp_sleep_enable_timer_wakeup(time_to_sleep_aux);
    esp_light_sleep_start();//El esp32 se despertara en los segundos 0 y 30
    }
