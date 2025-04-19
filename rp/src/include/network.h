/**
 * File: network.h
 * Author: Diego Parrilla Santamaría
 * Date: December 2024
 * Copyright: 2024 - GOODDATA LABS SL
 * Description: Header for network.c which starts the network stack
 */

#ifndef NETWORK_H
#define NETWORK_H

#include "constants.h"
#include "debug.h"
#include "gconfig.h"
#include "settings.h"

#ifdef BLINK_H
#include "blink.h"
#endif

#ifdef CYW43_WL_GPIO_LED_PIN
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#endif

#ifdef MICROPY_INCLUDED_LIB_NETUTILS_DHCPSERVER_H
#include "dhcpserver.h"
#endif

#ifdef _DNSSERVER_H_
#include "dnsserver.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#define NETWORK_POLLING_INTERVAL 100  // 100 ms
#define NETWORK_CONNECT_TIMEOUT 30    // 30 seconds

#define NETWORK_POWER_MGMT_DISABLED 0xa11140
#define NETWORK_POWER_MGMT_MAX_OPTIONS 5

#define NETWORK_MAX_STRING_LENGTH 32

#define NETWORK_MAC_SIZE 6

#define MAX_NETWORKS 100
#define MAX_SSID_LENGTH \
  36  // SSID can have up to 32 characters + null terminator + padding
#define MAX_BSSID_LENGTH 20
#define MAX_PASSWORD_LENGTH \
  68  // Password can have up to 64 characters + null terminator + padding
#define WIFI_AP_NETMASK "255.255.255.0"
#define WIFI_AP_GATEWAY "192.168.4.1"
#define WIFI_AP_SSID "SIDECART"
#define WIFI_AP_PASS "sidecart"
#define WIFI_AP_AUTH 5
#define WIFI_AP_HOSTNAME "sidecart"
#define WIFI_AP_PASS_MAX_LENGTH 9

// Connection errors as an enumeration
typedef enum {
  NETWORK_WIFI_STA_CONN_OK = 0,  // WiFi connected successfully
  NETWORK_WIFI_STA_CONN_ERR_NOT_INITIALIZED = -1,  // WiFi not initialized
  NETWORK_WIFI_STA_CONN_ERR_INVALID_MODE = -2,     // Invalid WiFi mode
  NETWORK_WIFI_STA_CONN_ERR_MAC_FAILED = -3,       // Failed to get MAC address
  NETWORK_WIFI_STA_CONN_ERR_NO_SSID = -4,          // No SSID provided
  NETWORK_WIFI_STA_CONN_ERR_NO_AUTH_MODE =
      -5,  // No authentication mode provided
  NETWORK_WIFI_STA_CONN_ERR_CONNECTION_FAILED =
      -6,                                 // Failed to connect to WiFi
  NETWORK_WIFI_STA_CONN_ERR_TIMEOUT = -7  // Connection timeout
} wifi_sta_conn_process_status_t;

typedef enum {
  DISCONNECTED,
  CONNECTING,
  CONNECTED_WIFI,
  CONNECTED_WIFI_NO_IP,
  CONNECTED_WIFI_IP,
  TIMEOUT_ERROR,
  GENERIC_ERROR,
  NO_DATA_ERROR,
  NOT_PERMITTED_ERROR,
  INVALID_ARG_ERROR,
  IO_ERROR,
  BADAUTH_ERROR,
  CONNECT_FAILED_ERROR,
  INSUFFICIENT_RESOURCES_ERROR,
  NOT_SUPPORTED
} wifi_sta_conn_status_t;

typedef enum {
  WIFI_MODE_AP = 0,  // Access Point mode
  WIFI_MODE_STA = 1  // Station mode
} wifi_mode_t;

typedef struct {
  char ssid[MAX_SSID_LENGTH];    // SSID can have up to 32 characters + null
                                 // terminator
  char bssid[MAX_BSSID_LENGTH];  // BSSID in the format xx:xx:xx:xx:xx:xx + null
                                 // terminator
  uint16_t auth_mode;            // MSB is not used, the data is in the LSB
  int16_t rssi;                  // Received Signal Strength Indicator
} wifi_network_info_t;

typedef struct {
  uint32_t magic;  // Some magic value for identification/validation
  wifi_network_info_t networks[MAX_NETWORKS];
  uint16_t count;  // The number of networks found/stored
} wifi_scan_data_t;

// Function to handle callback when trying to connect
typedef void (*NetworkPollingCallback)(void);

#ifdef CYW43_WL_GPIO_LED_PIN
/**
 * @brief Registers a callback for periodic network polling.
 *
 * Allows setting a user-defined function that is invoked during the network
 * polling loop.
 */
void network_setPollingCallback(NetworkPollingCallback callback);

/**
 * @brief Initializes only the network chip hardware.
 *
 * Configures low-level hardware parameters necessary for network operations.
 * Use if you want to use the green led on the pico boards.
 *
 * @return non-negative integer on success, error code otherwise.
 */
int network_initChipOnly();

/**
 * @brief Configures and starts the WiFi network stack.
 *
 * Sets the WiFi mode (either access point or station) and prepares the driver
 * for operation.
 *
 * @param mode Selected WiFi mode defined in wifi_mode_t.
 * @return non-negative integer on success, error code otherwise.
 */
int network_wifiInit(wifi_mode_t mode);

/**
 * @brief Deinitializes the network stack.
 *
 * Shuts down network services and releases used resources.
 */
void network_deInit();

/**
 * @brief Safely processes periodic network tasks.
 *
 * Performs polling on the network stack, ensuring operations occur in a safe
 * context.
 */
void network_safePoll();

/**
 * @brief Initiates a scan for available WiFi networks.
 *
 * Uses timing parameters to control scanning frequency and duration.
 *
 * @param wifi_scan_time Pointer to an absolute time structure for scan timing.
 * @param wifi_scan_interval Interval between scanning cycles in milliseconds.
 * @return Number of networks discovered on success, or an error code if
 * scanning fails.
 */
int network_scan(absolute_time_t* wifi_scan_time, int wifi_scan_interval);

/**
 * @brief Indicates whether a WiFi network scan is currently active.
 *
 * Useful for preventing concurrent scan attempts.
 *
 * @return Non-zero if a scan is in progress, zero otherwise.
 */
int network_scanIsActive();

/**
 * @brief Retrieves information about found WiFi networks.
 *
 * Provides the data structure containing the list of scanned networks.
 *
 * @return Pointer to a wifi_scan_data_t structure with network details.
 */
wifi_scan_data_t* network_getFoundNetworks();

/**
 * @brief Attempts connecting to a WiFi network in station mode.
 *
 * Implements connection logic including error handling and retries.
 *
 * @return Status code indicating connection success or failure.
 */
wifi_sta_conn_process_status_t network_wifiStaConnect();

/**
 * @brief Obtains the current WiFi connection status.
 *
 * Returns a detailed status that includes timing information and connection
 * verification.
 *
 * @param wifi_conn_status_time Pointer to an absolute time structure updated
 * upon status check.
 * @param wifi_con_status_interval Interval for checking status in milliseconds.
 * @return Enumerated WiFi connection status.
 */
wifi_sta_conn_status_t network_wifiConnStatus(
    absolute_time_t* wifi_conn_status_time, int wifi_con_status_interval);

/**
 * @brief Provides a human-readable description of the WiFi connection status.
 *
 * Useful for logging or UI feedback.
 *
 * @return Pointer to a string summarizing connection status.
 */
char* network_wifiConnStatusStr();

/**
 * @brief Converts a numeric WiFi authentication code into a descriptive string.
 *
 * Facilitates understanding of the authentication mode used.
 *
 * @param connect_code Numeric authentication code.
 * @return Pointer to a string with the full authentication type description.
 */
const char* network_getAuthTypeString(u_int16_t connect_code);

/**
 * @brief Returns an abbreviated string for the WiFi authentication type.
 *
 * Useful when display area is limited.
 *
 * @param connect_code Numeric authentication code.
 * @return Pointer to a short string representing the authentication type.
 */
const char* network_getAuthTypeStringShort(u_int16_t connect_code);

/**
 * @brief Retrieves the current IP address assigned to the network interface.
 *
 * Returns the IP structure with the current address information.
 *
 * @return IP address structure.
 */
ip_addr_t network_getCurrentIp();

#endif

#endif  // NETWORK_H
