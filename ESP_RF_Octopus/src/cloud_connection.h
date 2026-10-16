#pragma once
// These calls run in the cloud task/continuation, never the motion loop. Leave
// enough time for ESP-12S certificate verification on a real Wi-Fi connection.
constexpr uint32_t kCloudConnectTimeoutMs = 10000;
constexpr uint32_t kCloudHttpTimeoutMs = 15000;
constexpr uint32_t kCloudHandshakeTimeoutSeconds = 20;
constexpr uint32_t kCloudResponseIdleMs = 6000;
constexpr uint32_t kCloudResponseLimitMs = 15000;

template<class SecureClient>
void configureCloudConnection(SecureClient& client, HTTPClient& request) {
#if defined(ESP8266)
  client.setTrustAnchors(&cloudRoots); client.setSession(&cloudTlsSession);
  client.setTimeout(kCloudHttpTimeoutMs);
#else
  client.setCACert(kCloudRunRootCa);
  client.setHandshakeTimeout(kCloudHandshakeTimeoutSeconds);
  request.setConnectTimeout(kCloudConnectTimeoutMs);
#endif
  request.setTimeout(kCloudHttpTimeoutMs);
  request.setReuse(false); request.useHTTP10(true);
  request.setUserAgent("Lumiac-Controller/1.0");
}

template<class SecureClient>
String cloudConnectionError(SecureClient& client, int code) {
  if (code > 0) return "Gateway HTTP " + String(code);
  String error = "Secure connection failed (" + String(code) + "): " + HTTPClient::errorToString(code);
  char detail[160] = {};
#if defined(ESP8266)
  const int tlsCode = client.getLastSSLError(detail, sizeof(detail));
#else
  const int tlsCode = client.lastError(detail, sizeof(detail));
#endif
  if (tlsCode) error += "; TLS " + String(tlsCode) + ": " + detail;
  return error;
}
