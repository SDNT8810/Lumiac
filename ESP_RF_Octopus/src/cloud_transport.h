#pragma once
// Included inside the controller namespace. Only serialized messages cross the
// boundary; this transport never reads or changes motion, UART, or OTA state.
struct CloudExchange {
  String payload, response, error;
  uint32_t elapsed = 0;
};
bool cloudInFlight = false;

void performCloudExchange(CloudExchange& exchange) {
  const uint32_t started = millis();
#if defined(ESP8266)
  BearSSL::WiFiClientSecure client;
  client.setTrustAnchors(&cloudRoots); client.setSession(&cloudTlsSession);
  client.setTimeout(1500);
#else
  WiFiClientSecure client;
  client.setCACert(kCloudRunRootCa); client.setHandshakeTimeout(3);
#endif
  HTTPClient request;
#if !defined(ESP8266)
  request.setConnectTimeout(1500);
#endif
  request.setTimeout(1500); request.setReuse(false); request.useHTTP10(true);
  if (!request.begin(client, String(LUMIAC_CLOUD_URL) + "/device/v1/devices/" LUMIAC_CLOUD_DEVICE_ID "/sync")) {
    exchange.error = "Cannot open remote gateway";
  } else {
    request.addHeader("Content-Type", "application/json");
    request.addHeader("X-Lumiac-Device-Key", LUMIAC_CLOUD_DEVICE_KEY);
    request.addHeader("Accept-Encoding", "identity");
    const int code = request.POST(exchange.payload);
    exchange.payload = "";
    if (code != 200) exchange.error = "HTTPS status " + String(code);
    else {
      // Bound allocation and read time, including responses without Content-Length.
      const int length = request.getSize();
      uint32_t progress = millis();
      auto* stream = request.getStreamPtr();
      if (length > 3072) exchange.error = "Gateway response is too large";
      while (exchange.error.isEmpty() && (length < 0 || int(exchange.response.length()) < length)) {
        if (stream->available()) {
          if (exchange.response.length() >= 3072) { exchange.error = "Gateway response is too large"; break; }
          exchange.response += char(stream->read()); progress = millis();
        } else {
          if (!request.connected()) {
            if (length >= 0 && int(exchange.response.length()) != length) exchange.error = "Gateway response interrupted";
            break;
          }
          if (millis() - progress > 1500 || millis() - started > 15000) {
            exchange.error = "Gateway response timed out"; break;
          }
          delay(1);
        }
      }
    }
  }
  request.end(); client.stop();
  exchange.elapsed = millis() - started;
}

#if defined(ESP8266)
// Arduino ESP8266 3.1.2 implements cooperative continuations for loop(). Give
// HTTPS its own continuation: DNS/TCP/TLS yield points return to hardware control.
// Restore g_pcont before ANY main-loop operation can yield. Only one TLS operation
// runs at a time (cloud sync OR a parked OTA download).
cont_t cloudContext;
CloudExchange* cloudRequest = nullptr;
CloudExchange* cloudResponse = nullptr;
void cloudTransportEntry() {
  performCloudExchange(*cloudRequest);
  cloudResponse = cloudRequest; cloudRequest = nullptr;
}
bool configureCloudTransport() { cont_init(&cloudContext); return true; }
void serviceCloudTransport() {
  if (!cloudRequest) return;
  cont_t* previous = g_pcont;
  g_pcont = &cloudContext;
  cont_run(&cloudContext, cloudTransportEntry);
  g_pcont = previous;
  cont_check(&cloudContext);
}
bool submitCloudExchange(CloudExchange* exchange) {
  if (cloudRequest || cloudResponse) return false;
  cloudRequest = exchange; return true;
}
CloudExchange* takeCloudExchange() {
  CloudExchange* reply = cloudResponse; cloudResponse = nullptr; return reply;
}
#else
QueueHandle_t cloudRequests = nullptr, cloudResponses = nullptr;
TaskHandle_t cloudTask = nullptr;
void cloudTransportTask(void*) {
  for (;;) {
    CloudExchange* exchange = nullptr;
    if (xQueueReceive(cloudRequests, &exchange, portMAX_DELAY) != pdTRUE) continue;
    performCloudExchange(*exchange);
    xQueueSend(cloudResponses, &exchange, portMAX_DELAY);
  }
}
bool configureCloudTransport() {
  cloudRequests = xQueueCreate(1, sizeof(CloudExchange*));
  cloudResponses = xQueueCreate(1, sizeof(CloudExchange*));
  if (cloudRequests && cloudResponses && xTaskCreatePinnedToCore(cloudTransportTask, "lumiac-cloud", 12288, nullptr, 1, &cloudTask, 0) == pdPASS) return true;
  if (cloudRequests) vQueueDelete(cloudRequests);
  if (cloudResponses) vQueueDelete(cloudResponses);
  cloudRequests = cloudResponses = nullptr; return false;
}
void serviceCloudTransport() {}
bool submitCloudExchange(CloudExchange* exchange) {
  return cloudRequests && xQueueSend(cloudRequests, &exchange, 0) == pdTRUE;
}
CloudExchange* takeCloudExchange() {
  CloudExchange* reply = nullptr;
  if (cloudResponses) xQueueReceive(cloudResponses, &reply, 0);
  return reply;
}
#endif
