idf_component_register(
    SRCS "main.c" "media_keys.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash esp_wifi esp_http_server esp_netif esp_event esp_timer json
    REQUIRED_IDF_TARGETS esp32
)

# Generate a byte array for both native ESP-IDF and PlatformIO builds.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/dashboard.html")
file(READ "${CMAKE_CURRENT_LIST_DIR}/dashboard.html" DASHBOARD_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," DASHBOARD_BYTES "${DASHBOARD_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dashboard_data.h"
    "static const unsigned char dashboard_html[] = {${DASHBOARD_BYTES}0};\n")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
