#include "stationcloud.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "cloud_ca.h"

#define PACKET_MAX      256       // longest packet line
#define QUEUE_LINES   30        // lines held while the connection is down, oldest dropped after
#define RETRY_MS      5000      // wait between connection attempts
#define MQTT_BUFFER   512
#define TASK_STACK    10240
#define TASK_CORE     0         // the radio runs on core 1

static WiFiClientSecure net;
static PubSubClient     mqtt(net);
static QueueHandle_t    queue = nullptr;
static const char      *cloudUser = "", *cloudPass = "", *cloudTopic = "";

static void keepConnected() {   // connect when WiFi is up, retrying slowly
  static uint32_t triedAt = 0;
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  if (triedAt && millis() - triedAt < RETRY_MS) return;
  triedAt = millis();
  bool ok = mqtt.connect("webground", cloudUser, cloudPass);
  Serial.printf("[cloud] %s\n", ok ? "connected" : "connect failed");
}

static void task(void *) {      // upload lines; slow network work stays away from the radio
  char line[PACKET_MAX];
  for (;;) {
    keepConnected();
    mqtt.loop();
    if (mqtt.connected() && xQueueReceive(queue, line, pdMS_TO_TICKS(50)) == pdTRUE) mqtt.publish(cloudTopic, line);
    else vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void cloudStart(const char *host, int port, const char *user, const char *pass, const char *topic) {
  if (!host[0]) return;         // no online service set
  cloudUser = user; cloudPass = pass; cloudTopic = topic;
  queue = xQueueCreate(QUEUE_LINES, PACKET_MAX);
  net.setCACert(CLOUD_CA);
  mqtt.setServer(host, port);
  mqtt.setBufferSize(MQTT_BUFFER);
  xTaskCreatePinnedToCore(task, "cloud", TASK_STACK, nullptr, 1, nullptr, TASK_CORE);
}

void cloudSend(const String &line) {
  if (!queue) return;
  char buf[PACKET_MAX];
  line.toCharArray(buf, sizeof(buf));
  if (xQueueSend(queue, buf, 0) == pdTRUE) return;
  char old[PACKET_MAX];
  xQueueReceive(queue, old, 0);
  xQueueSend(queue, buf, 0);
}
