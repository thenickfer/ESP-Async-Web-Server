#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "FS.h"
#include "SPIFFS.h"
#include <DHT.h>
#include <Adafruit_MPL3115A2.h>

const char *ssid = "iPhone (7)";
const char *password = "12345678";
AsyncWebServer server(80);
const gpio_num_t led = GPIO_NUM_2;
const gpio_num_t RELAY_PIN = led;
const gpio_num_t FAN_PIN = GPIO_NUM_32;
const gpio_num_t DHT_PIN = GPIO_NUM_4;
const uint8_t DHT_TYPE = DHT22;
const gpio_num_t MQ07_PIN = GPIO_NUM_34;
const gpio_num_t I2C_SDA = GPIO_NUM_21;
const gpio_num_t I2C_SCL = GPIO_NUM_22;

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_MPL3115A2 baro = Adafruit_MPL3115A2();

float temp = 0.0f;
float umid = 0.0f;
float co = 0.0f;
float alt = 0.0f;
float press = 0.0f;
bool fan_status = 0;
bool relay_status = 0;

SemaphoreHandle_t tempUmidMutex = nullptr; // semaforo para evitar conflitos em updates
SemaphoreHandle_t coMutex = nullptr;
SemaphoreHandle_t altPressMutex = nullptr;
SemaphoreHandle_t relayMutex = nullptr;

void fan_setStatus(bool newStatus)
{
  fan_status = newStatus;
  gpio_set_level(FAN_PIN, fan_status);
}

void lerCO()
{
  float voltage = analogRead(MQ07_PIN) * (3.3 / 4095.0); // Formula GPT para converter leitura do sensor para ppm
  float RL = 10.0;                                       // load resistor in kΩ (check your module!)
  float Rs = ((3.3 - voltage) / voltage) * RL;
  float R0 = Rs / 27.0;
  float ratio = Rs / R0;
  co = pow(10, ((-1.497 * log10(ratio)) + 1.487));
}

void alternarRele()
{
  xSemaphoreTake(relayMutex, portMAX_DELAY);
  relay_status = !relay_status;
  gpio_set_level(RELAY_PIN, relay_status);
  xSemaphoreGive(relayMutex);
}

void taskDHT22(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    xSemaphoreTake(tempUmidMutex, portMAX_DELAY);
    temp = dht.readTemperature();
    umid = dht.readHumidity();
    xSemaphoreGive(tempUmidMutex);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void taskMQ07(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    xSemaphoreTake(coMutex, portMAX_DELAY);
    lerCO();
    xSemaphoreGive(coMutex);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void taskMPL3115A2(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    xSemaphoreTake(altPressMutex, portMAX_DELAY);
    alt = baro.getAltitude();
    press = baro.getPressure() / 100.0f;
    xSemaphoreGive(altPressMutex);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void TaskVentila(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    xSemaphoreTake(tempUmidMutex, portMAX_DELAY);
    if (temp > 30.0f)
    {
      fan_setStatus(1);
    }
    else if (temp < 25.0f)
    {
      fan_setStatus(0);
    }
    xSemaphoreGive(tempUmidMutex);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

String processor(const String &var)
{

  float t, h, c, a, p;
  bool f, r;

  xSemaphoreTake(tempUmidMutex, portMAX_DELAY);
  xSemaphoreTake(relayMutex, portMAX_DELAY);
  xSemaphoreTake(coMutex, portMAX_DELAY);
  xSemaphoreTake(altPressMutex, portMAX_DELAY);
  t = temp;
  h = umid;
  c = co;
  a = alt;
  p = press;
  f = fan_status;
  r = relay_status;
  xSemaphoreGive(altPressMutex);
  xSemaphoreGive(coMutex);
  xSemaphoreGive(relayMutex);
  xSemaphoreGive(tempUmidMutex);

  if (var == "RELAY_ACTION")
    return r ? "Desligar" : "Ligar";
    if (var == "RELAY_STATUS")
      return r ? "Ligado" : "Desligado";
  if (var == "TEMP")
    return String(t, 1) + " C";
  if (var == "HUM")
    return String(h, 1) ;
  if (var == "GAS")
    return String(c) + " ppm";
  if (var == "ALT")
    return String(a, 1) + " m";
  if (var == "PRESS")
    return String(p, 1) + " hPa";
  if (var == "FAN_STATUS")
    return f ? "Ligada" : "Desligada";
  return String();
}

void setup()
{
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print("a.");
  }
  Serial.println("\nWiFi Conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  dht.begin();
  gpio_set_direction(led, GPIO_MODE_OUTPUT);
  gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(FAN_PIN, GPIO_MODE_OUTPUT);

  gpio_set_level(led, 0);
  gpio_set_level(RELAY_PIN, 0);
  gpio_set_level(FAN_PIN, 0);

  altPressMutex = xSemaphoreCreateMutex();
  coMutex = xSemaphoreCreateMutex();
  relayMutex = xSemaphoreCreateMutex();
  tempUmidMutex = xSemaphoreCreateMutex();
  if (altPressMutex == nullptr || coMutex == nullptr || relayMutex == nullptr || tempUmidMutex == nullptr)
  {
    Serial.println("Falha ao criar mutex");
    return;
  }

  if (!SPIFFS.begin(true))
  {
    Serial.println("Falha ao montar SPIFFS");
    return;
  }

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!baro.begin())
  {
    Serial.println("MPL3115A2 not found!");
    return;
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/index.html", "text/html", false, processor); });
  server.on("/relay/toggle", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              alternarRele();
              request->redirect("/"); });
  /* server.on("/temp", HTTP_GET, [](AsyncWebServerRequest *request)
            {
      float t;
      xSemaphoreTake(tempUmidMutex, portMAX_DELAY);
      t = temp;
      xSemaphoreGive(tempUmidMutex);
      request-> send(200, "text/plain", String(t, 1)) ; });
  server.on("/umid", HTTP_GET, [](AsyncWebServerRequest *request)
            {
      float h;
      xSemaphoreTake(tempUmidMutex, portMAX_DELAY);
      h = umid;
      xSemaphoreGive(tempUmidMutex);
      request->send(200, "text/plain", String(h, 1)); });
  server.on("/co", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    float c;
    xSemaphoreTake(coMutex, portMAX_DELAY);
    c = co;
    xSemaphoreGive(coMutex);
    request->send(200, "text/plain", String(c, 1)); });

  server.on("/alt", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    float a;
    xSemaphoreTake(altPressMutex, portMAX_DELAY);
    a = alt;
    xSemaphoreGive(altPressMutex);
    request->send(200, "text/plain", String(a, 1)); });

  server.on("/press", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    float p;
    xSemaphoreTake(altPressMutex, portMAX_DELAY);
    p = press;
    xSemaphoreGive(altPressMutex);
    request->send(200, "text/plain", String(p, 1)); });

  server.on("/fan", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    bool f;
    xSemaphoreTake(tempUmidMutex, portMAX_DELAY);
    f = fan_status;
    xSemaphoreGive(tempUmidMutex);
    request->send(200, "text/plain", f ? "Ligada" : "Desligada"); });

  server.on("/relay", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    bool r;
    xSemaphoreTake(relayMutex, portMAX_DELAY);
    r = relay_status;
    xSemaphoreGive(relayMutex);
    request->send(200, "text/plain", r ? "Ligado" : "Desligado"); }); */

  server.serveStatic("/", SPIFFS, "/");

  server.begin();

  xTaskCreatePinnedToCore(taskDHT22, "taskTemp", 2048, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(taskMQ07, "taskCO", 2048, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(taskMPL3115A2, "taskAlt", 2048, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(TaskVentila, "taskFan", 2048, nullptr, 1, nullptr, 1); 
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.begin(ssid, password);
    Serial.print("Conectando ao WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(100);
      Serial.print(".");
    }
    Serial.println("\nWiFi Conectado");
    Serial.print("IP: ");
  }
  Serial.println(WiFi.localIP());
  vTaskDelay(pdMS_TO_TICKS(10000));
}