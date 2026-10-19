#include "../shared/firmware_image.h"
#include <cassert>
#include <string>
#include <iostream>
int main() {
  std::string bytes = "LUMIAC_OTA_ROLE:esp32"; bytes += '\0';
  bytes += "LUMIAC1-123456789abc"; bytes += '\0'; bytes += "2026-10-19.2"; bytes += '\0';
  for (unsigned size = 1; size < bytes.size(); ++size) {
    lumiac::FirmwareImageMarkers check;
    check.begin("LUMIAC_OTA_ROLE:esp32", "LUMIAC1-123456789abc", "2026-10-19.2");
    for (unsigned offset = 0; offset < bytes.size(); offset += size) check.write(reinterpret_cast<const uint8_t*>(bytes.data()+offset), std::min(size, unsigned(bytes.size()-offset)));
    assert(check.complete());
  }
  for (const char* wrong : {"LUMIAC_OTA_ROLE:esp322", "LUMIAC1-123456789abcd", "2026-10-19.20"}) {
    lumiac::FirmwareMarker check;
    check.begin(wrong[0] == '2' ? "2026-10-19.2" : wrong[6] == '_' ? "LUMIAC_OTA_ROLE:esp32" : "LUMIAC1-123456789abc");
    for (const char* p = wrong; *p; ++p) check.write(*p);
    check.write(0); assert(!check.complete());
  }
  lumiac::FirmwareImageMarkers recovery;
  recovery.begin("LUMIAC_OTA_ROLE:esp32", "LUMIAC1-123456789abc", "2026-10-19.2", "LUMIAC_LOCAL_OTA_V1");
  recovery.write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()); assert(!recovery.complete());
  const char* marker = "LUMIAC_LOCAL_OTA_V1";
  recovery.write(reinterpret_cast<const uint8_t*>(marker), strlen(marker)); assert(recovery.complete());
  std::cout << "Local firmware markers: every chunk boundary and wrong role/identity/revision prefix rejected\n";
}
