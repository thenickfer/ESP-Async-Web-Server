#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "FS.h"
#include "SPIFFS.h"

const char *ssid = "iPhone (7)";
const char *password = "12345678";
AsyncWebServer server(80);
const gpio_num_t led = GPIO_NUM_2;
const gpio_num_t RELAY_PIN = led; // GPIO_NUM_32; //por enquanto led
const gpio_num_t FAN_PIN = GPIO_NUM_4;

float temp = 0.0f;
float umid = 0.0f;
float co = 0.0f;
float alt = 0.0f;
float press = 0.0f;
bool fan_status = 0;
bool relay_status = 0;

SemaphoreHandle_t sensorsMutex = nullptr; // semaforo para evitar conflitos em updates

void fan_setStatus(bool newStatus)
{
  fan_status = newStatus;
  gpio_set_level(FAN_PIN, fan_status);
}

void lerTemp()
{
  temp = random(0, 400) / 10.0f;
}

void lerUmid()
{
  umid = random(0, 1000) / 10.0f;
}

void lerCO()
{
  co = random(0, 1000) / 10.0f;
}

void lerAlt()
{
  alt = random(0, 1000) / 10.0f;
}

void lerPress()
{
  press = random(0, 1000) / 10.0f;
}

void alternarRele()
{
  xSemaphoreTake(sensorsMutex, portMAX_DELAY);
  relay_status = !relay_status;
  gpio_set_level(RELAY_PIN, relay_status);
  xSemaphoreGive(sensorsMutex);
}

void taskDHT22(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    xSemaphoreTake(sensorsMutex, portMAX_DELAY);
    lerTemp();
    lerUmid();
    xSemaphoreGive(sensorsMutex);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void taskMQ07(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    xSemaphoreTake(sensorsMutex, portMAX_DELAY);
    lerCO();
    xSemaphoreGive(sensorsMutex);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void taskMPL3115A2(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    xSemaphoreTake(sensorsMutex, portMAX_DELAY);
    lerAlt();
    lerPress();
    xSemaphoreGive(sensorsMutex);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void TaskVentila(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    xSemaphoreTake(sensorsMutex, portMAX_DELAY);
    if (temp > 30.0f)
    {
      fan_setStatus(1);
    }
    else if (temp < 25.0f)
    {
      fan_setStatus(0);
    }
    xSemaphoreGive(sensorsMutex);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

String processor(const String &var)
{

  float t, h, c, a, p;
  bool f, r;

  xSemaphoreTake(sensorsMutex, portMAX_DELAY);
  t = temp;
  h = umid;
  c = co;
  a = alt;
  p = press;
  f = fan_status;
  r = relay_status;
  xSemaphoreGive(sensorsMutex);

  if (var == "TEMP")
    return String(t, 1) + " C";
  if (var == "HUM")
    return String(h, 1) + " %";
  if (var == "CO")
    return String(c) + " ppm";
  if (var == "ALT")
    return String(a, 1) + " m";
  if (var == "PRESS")
    return String(p, 1) + " hPa";
  if (var == "FAN_STATUS")
    return f ? "Ligada" : "Desligada";
  if (var == "RELAY_STATUS")
    return r ? "Ligado" : "Desligado";
  if (var == "RELAY_ACTION")
    return r ? "Desligar" : "Ligar";
  return String();
}

void setup()
{
  Serial.begin(115200);

  gpio_set_direction(led, GPIO_MODE_OUTPUT);
  gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(FAN_PIN, GPIO_MODE_OUTPUT);

  gpio_set_level(led, 0);
  gpio_set_level(RELAY_PIN, 0);
  gpio_set_level(FAN_PIN, 0);

  sensorsMutex = xSemaphoreCreateMutex();
  if (sensorsMutex == nullptr)
  {
    Serial.println("Falha ao criar mutex");
    return;
  }

  if (!SPIFFS.begin(true))
  {
    Serial.println("Falha ao montar SPIFFS");
    return;
  }

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }
  Serial.println("\nWiFi Conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

 

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/index.html", "text/html", false, processor); });
  server.on("/relay/toggle", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              alternarRele();
              request->redirect("/"); });
  server.on("/temp", HTTP_GET, [](AsyncWebServerRequest *request)
            {
      float t;
      xSemaphoreTake(sensorsMutex, portMAX_DELAY);
      t = temp;
      xSemaphoreGive(sensorsMutex);
      request-> send(200, "text/plain", String(t, 1)) ; });
  server.on("/umid", HTTP_GET, [](AsyncWebServerRequest *request)
            {
      float h;
      xSemaphoreTake(sensorsMutex, portMAX_DELAY);
      h = umid;
      xSemaphoreGive(sensorsMutex);
      request->send(200, "text/plain", String(h, 1)); });
  server.on("/co", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    float c;
    xSemaphoreTake(sensorsMutex, portMAX_DELAY);
    c = co;
    xSemaphoreGive(sensorsMutex);
    request->send(200, "text/plain", String(c, 1)); });

  server.on("/alt", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    float a;
    xSemaphoreTake(sensorsMutex, portMAX_DELAY);
    a = alt;
    xSemaphoreGive(sensorsMutex);
    request->send(200, "text/plain", String(a, 1)); });

  server.on("/press", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    float p;
    xSemaphoreTake(sensorsMutex, portMAX_DELAY);
    p = press;
    xSemaphoreGive(sensorsMutex);
    request->send(200, "text/plain", String(p, 1)); });

  server.on("/fan", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    bool f;
    xSemaphoreTake(sensorsMutex, portMAX_DELAY);
    f = fan_status;
    xSemaphoreGive(sensorsMutex);
    request->send(200, "text/plain", f ? "Ligada" : "Desligada"); });

  server.on("/relay", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    bool r;
    xSemaphoreTake(sensorsMutex, portMAX_DELAY);
    r = relay_status;
    xSemaphoreGive(sensorsMutex);
    request->send(200, "text/plain", r ? "Ligado" : "Desligado"); });

  server.serveStatic("/", SPIFFS, "/");

  server.begin();

  xTaskCreatePinnedToCore(taskDHT22, "taskTemp", 2048, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(taskMQ07, "taskCO", 2048, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(taskMPL3115A2, "taskAlt", 2048, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(TaskVentila, "taskFan", 2048, nullptr, 1, nullptr, 1);
}

void loop()
{
  if(WiFi.status() != WL_CONNECTED){
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