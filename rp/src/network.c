#include "include/network.h"

static bool cyw43Initialized = false;
static wifi_mode_t wifiCurrentMode = WIFI_MODE_STA;
static wifi_network_info_t wifiNetworkInfo = {0};
static wifi_scan_data_t wifiScanData = {0};
static bool wifiScanInProgress = false;
static char wifiHostname[NETWORK_MAX_STRING_LENGTH];
static ip_addr_t currentIp = {0};
static uint8_t cyw43Mac[NETWORK_MAC_SIZE];
static wifi_sta_conn_status_t connectionStatus = DISCONNECTED;
static char connectionStatusStr[NETWORK_MAX_STRING_LENGTH] = {0};

// Static variable to store the callback function
static NetworkPollingCallback networkPollingCallback = NULL;

static const char *picoSerialStr() {
  static char buf[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
  pico_unique_board_id_t boardId;

  memset(&boardId, 0, sizeof(boardId));
  pico_get_unique_board_id(&boardId);
  for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
    snprintf(&buf[i * 2], 3, "%02x", boardId.id[i]);
  }

  return buf;
}

static uint32_t getCountryCode(char *code, char **validCountryStr) {
  *validCountryStr = "XX";
  // empty configuration select worldwide
  if (strlen(code) == 0) {
    return CYW43_COUNTRY_WORLDWIDE;
  }

  if (strlen(code) != 2) {
    return CYW43_COUNTRY_WORLDWIDE;
  }

  // current supported country code
  // https://www.raspberrypi.com/documentation/pico-sdk/networking.html#CYW43_COUNTRY_
  // ISO-3166-alpha-2
  // XX select worldwide
  char *validCountryCode[] = {
      "XX", "AU", "AR", "AT", "BE", "BR", "CA", "CL", "CN", "CO", "CZ",
      "DK", "EE", "FI", "FR", "DE", "GR", "HK", "HU", "IS", "IN", "IL",
      "IT", "JP", "KE", "LV", "LI", "LT", "LU", "MY", "MT", "MX", "NL",
      "NZ", "NG", "NO", "PE", "PH", "PL", "PT", "SG", "SK", "SI", "ZA",
      "KR", "ES", "SE", "CH", "TW", "TH", "TR", "GB", "US"};

  char country[3] = {toupper(code[0]), toupper(code[1]), 0};
  for (int i = 0; i < (sizeof(validCountryCode) / sizeof(validCountryCode[0]));
       i++) {
    if (!strcmp(country, validCountryCode[i])) {
      *validCountryStr = validCountryCode[i];
      return CYW43_COUNTRY(country[0], country[1], 0);
    }
  }
  return CYW43_COUNTRY_WORLDWIDE;
}

/**
 * @brief Deinitializes the network.
 *
 * This function deinitializes the network by setting the `cyw43_initialized`
 * flag to false and calling `cyw43_arch_deinit()`. It is important to set the
 * flag to false because calling a cyw43 function before initialization will
 * cause a crash.
 */
#ifdef CYW43_WL_GPIO_LED_PIN
void network_deInit() {
  // This flag is important, because calling a cyw43 function before the
  // initialization will cause a crash
  if (cyw43Initialized) {
    DPRINTF("Deinitializing the network\n");
    cyw43Initialized = false;
    cyw43_arch_deinit();
    DPRINTF("Network deinitialized\n");
  } else {
    DPRINTF("Network already deinitialized\n");
  }
}
#endif

// NOLINTBEGIN(readability-magic-numbers)
static u_int32_t getAuthPicoCode(u_int16_t connectCode) {
  switch (connectCode) {
    case 0:
      return CYW43_AUTH_OPEN;
    case 1:
    case 2:
      return CYW43_AUTH_WPA_TKIP_PSK;
    case 3:
    case 4:
    case 5:
      return CYW43_AUTH_WPA2_AES_PSK;
    case 6:
    case 7:
    case 8:
      return CYW43_AUTH_WPA2_MIXED_PSK;
    default:
      return CYW43_AUTH_OPEN;
  }
}
// NOLINTEND(readability-magic-numbers)

// Setter for the callback function
void network_setPollingCallback(NetworkPollingCallback callback) {
  networkPollingCallback = callback;
}

// NOLINTBEGIN(readability-magic-numbers)
const char *network_getAuthTypeString(u_int16_t connectCode) {
  switch (connectCode) {
    case 0:
      return "OPEN";
    case 1:
    case 2:
      return "WPA_TKIP_PSK";
    case 3:
    case 4:
    case 5:
      return "WPA2_AES_PSK";
    case 6:
    case 7:
    case 8:
      return "WPA2_MIXED_PSK";
    default:
      return "OPEN";
  }
}
// NOLINTEND(readability-magic-numbers)

// NOLINTBEGIN(readability-magic-numbers)
const char *network_getAuthTypeStringShort(u_int16_t connectCode) {
  switch (connectCode) {
    case 1:
    case 2:
      return "WPA";
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
      return "WPA2";
    default:
      return "OPEN";
  }
}
// NOLINTEND(readability-magic-numbers)

/**
 * @brief Initializes the CYW43 WiFi chip if it has not been initialized
 * already.
 *
 * This function checks if the CYW43 WiFi chip has already been initialized. If
 * not, it sets the initialization flag to true and proceeds to initialize the
 * chip. It should not be called to initialize the WiFi network, only the chip.
 * For example, it can be used to detect the VBUS pin state or toogling the
 * green LED.
 *
 * @return int Returns 0 on successful initialization or if the chip was already
 * initialized. Returns -1 if the initialization fails.
 */
#ifdef CYW43_WL_GPIO_LED_PIN
int network_initChipOnly() {
  if (cyw43Initialized) {
    DPRINTF("WiFi already initialized\n");
    return 0;
  }
  // This flag is important, because calling a cyw43 function before the
  // initialization will cause a crash
  cyw43Initialized = true;
  DPRINTF("CYW43 Logging level: %d\n", CYW43_VERBOSE_DEBUG);
  int res;
  DPRINTF("Initialization CYW43 chip ONLY...\n");

  if ((res = cyw43_arch_init())) {
    DPRINTF("Failed to initialize CYW43: %d\n", res);
    return -1;
  }
  return 0;
}
#endif

/**
 * @brief Initialize the WiFi network with the specified mode.
 *
 * This function initializes the WiFi network and sets the country code and
 * power management settings. It supports both STA (Station) and AP (Access
 * Point) modes.
 *
 * @param mode The WiFi mode to initialize. It can be either WIFI_MODE_STA or
 * WIFI_MODE_AP.
 * @return int Returns 0 on success, or -1 on failure.
 */
#ifdef CYW43_WL_GPIO_LED_PIN
int network_wifiInit(wifi_mode_t mode) {
  if (cyw43Initialized) {
    DPRINTF("WiFi already initialized\n");
    return 0;
  }
  // This flag is important, because calling a cyw43 function before the
  // initialization will cause a crash
  cyw43Initialized = true;
  DPRINTF("CYW43 Logging level: %d\n", CYW43_VERBOSE_DEBUG);
  uint32_t country = CYW43_COUNTRY_WORLDWIDE;
  SettingsConfigEntry *countryEntry =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_COUNTRY);
  if (countryEntry != NULL) {
    char *valid;
    country = getCountryCode(countryEntry->value, &valid);
    settings_put_string(gconfig_getContext(), PARAM_WIFI_COUNTRY, valid);
  }

  int res;
  DPRINTF("Initialization WiFi...\n");

  if ((res = cyw43_arch_init_with_country(country))) {
    DPRINTF("Failed to initialize WiFi: %d\n", res);
    return -1;
  }
  DPRINTF("Country: %s\n", countryEntry->value);

  // Start STA or AP mode
  if (mode == WIFI_MODE_STA) {
    DPRINTF("Enabling STA mode...\n");
    cyw43_arch_enable_sta_mode();
    wifiCurrentMode = WIFI_MODE_STA;
  } else {
    DPRINTF("Enabling AP mode...\n");
    DPRINTF("Read the SSID, password and auth mode\n");

    DPRINTF("No SSID found in config for AP mode. Setting default SSID\n");

    char ssidStr[MAX_SSID_LENGTH] = WIFI_AP_SSID;
    DPRINTF("SSID: %s\n", ssidStr);

    char passwordStr[WIFI_AP_PASS_MAX_LENGTH] = WIFI_AP_PASS;
    DPRINTF("Password: %s\n", passwordStr);

    int authInt = WIFI_AP_AUTH;  // WPA2_AES_PSK

    DPRINTF("Auth mode: %08x\n", getAuthPicoCode(authInt));
    cyw43_arch_enable_ap_mode(ssidStr, passwordStr, getAuthPicoCode(authInt));

    // Set static IP address for the AP
    struct netif *netif = &cyw43_state.netif[CYW43_ITF_AP];
    ip4_addr_t netmask;
    ip4_addr_t gateway;
    ip4addr_aton(WIFI_AP_NETMASK, &netmask);
    ip4addr_aton(WIFI_AP_GATEWAY, &gateway);

    DPRINTF("GW IP: %s\n", ip4addr_ntoa(&gateway));
    DPRINTF("Mask IP: %s\n", ip4addr_ntoa(&netmask));

#ifdef MICROPY_INCLUDED_LIB_NETUTILS_DHCPSERVER_H
    // Start the dhcp server
    dhcp_server_init(&gateway, &netmask);
    DPRINTF("DHCP server started.\n");
#endif

#ifdef _DNSSERVER_H_
    // Start the dns server
    dns_server_init(&gateway);
    DPRINTF("DNS server started\n");
#endif
    wifiCurrentMode = WIFI_MODE_AP;
  }

  // Setting the power management
  uint32_t pmValue = NETWORK_POWER_MGMT_DISABLED;  // 0: Disable PM
  SettingsConfigEntry *pmEntry =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_POWER);
  if (pmEntry != NULL) {
    pmValue = strtoul(pmEntry->value, NULL, HEX_BASE);
  }
  if (pmValue < NETWORK_POWER_MGMT_MAX_OPTIONS) {
    switch (pmValue) {
      case 0:
        pmValue = NETWORK_POWER_MGMT_DISABLED;  // DISABLED_PM
        break;
      case 1:
        pmValue = CYW43_PERFORMANCE_PM;  // PERFORMANCE_PM
        break;
      case 2:
        pmValue = CYW43_AGGRESSIVE_PM;  // AGGRESSIVE_PM
        break;
      case 3:
        pmValue = CYW43_DEFAULT_PM;  // DEFAULT_PM
        break;
      default:
        pmValue = CYW43_NO_POWERSAVE_MODE;  // NO_POWERSAVE_MODE
        break;
    }
  }
  DPRINTF("Setting power management to: %08x\n", pmValue);
  cyw43_wifi_pm(&cyw43_state, pmValue);
  return 0;
}
#endif

/**
 * @brief Safely polls the network if the CYW43 module is initialized.
 *
 * This function checks if the CYW43 module has been initialized before
 * calling the `cyw43_arch_poll()` function to poll the network. This ensures
 * that the polling operation is only performed when the module is ready,
 * preventing potential errors or undefined behavior.
 */
void network_safePoll() {
  if (cyw43Initialized) {
    cyw43_arch_poll();
  }
}

/**
 * @brief Scans for available Wi-Fi networks and stores the results.
 *
 * This function initiates a Wi-Fi network scan if the network is initialized
 * and the scan interval has elapsed. It processes the scan results and stores
 * unique networks in the global `wifi_scan_data` structure.
 *
 * @param wifi_scan_time Pointer to the absolute time of the last scan.
 * @param wifi_scan_interval Interval between scans in seconds.
 * @return int Returns 0 on success, -1 if the network is not initialized.
 */
int network_scan(absolute_time_t *wifiScanTime, int wifiScanInterval) {
  if (!cyw43Initialized) {
    // If the network is not initialized, we cancel the scan
    return -1;
  }
  int scan_result(void *env, const cyw43_ev_scan_result_t *result) {
    // Check if the BSSID already exists in the found networks
    bool bssid_exists(wifi_network_info_t * network) {
      for (size_t i = 0; i < wifiScanData.count; i++) {
        if (strcmp(wifiScanData.networks[i].bssid, network->bssid) == 0) {
          return true;  // BSSID found
        }
      }
      return false;  // BSSID not found
    }
    if (result && wifiScanData.count < MAX_NETWORKS) {
      wifi_network_info_t network;

      // Copy SSID
      snprintf(network.ssid, sizeof(network.ssid), "%s", result->ssid);

      // Format BSSID
      snprintf(network.bssid, sizeof(network.bssid),
               "%02x:%02x:%02x:%02x:%02x:%02x", result->bssid[0],
               result->bssid[1], result->bssid[2], result->bssid[3],
               result->bssid[4], result->bssid[5]);

      // Store authentication mode
      network.auth_mode = result->auth_mode;

      // Store signal strength
      network.rssi = result->rssi;

      // Check if BSSID already exists
      if (!bssid_exists(&network)) {
        if (strlen(network.ssid) > 0) {
          wifiScanData.networks[wifiScanData.count] = network;
          wifiScanData.count++;
          DPRINTF("FOUND NETWORK %s (%s) with auth %d and RSSI %d\n",
                  network.ssid, network.bssid, network.auth_mode, network.rssi);
        }
      }
    }
    return 0;
  }
  // DPRINTF("Time diff: %lld\n", absolute_time_diff_us(get_absolute_time(),
  // (absolute_time_t)*wifi_scan_time));
  if (absolute_time_diff_us(get_absolute_time(), *wifiScanTime) < 0) {
    if (!wifiScanInProgress) {
      DPRINTF("Scanning networks...\n");
      cyw43_wifi_scan_options_t scanOptions = {0};
      int err = cyw43_wifi_scan(&cyw43_state, &scanOptions, NULL, scan_result);
      if (err == 0) {
        DPRINTF("Performing wifi scan\n");
        wifiScanInProgress = true;
      } else {
        DPRINTF("Failed to start scan: %d\n", err);
        *wifiScanTime = make_timeout_time_ms(wifiScanInterval * SEC_TO_MS);
      }
    } else {
      if (!cyw43_wifi_scan_active(&cyw43_state)) {
        DPRINTF("Continue scanning...\n");
        wifiScanInProgress = false;
      }
      *wifiScanTime = make_timeout_time_ms(wifiScanInterval * SEC_TO_MS);
    }
  }
  // else {
  //     DPRINTF("Scan already in progress\n");
  // }
}

int network_scanIsActive() {
  if (!cyw43Initialized) {
    // If the network is not initialized, we cancel the scan
    DPRINTF("WiFi not initialized.\n");
    return -1;
  }
  return (int)cyw43_wifi_scan_active(&cyw43_state);
}

/**
 * @brief Return the list of found networks.
 *
 * This function returns a pointer to the list of Wi-Fi networks that have been
 * found during a scan.
 *
 * @return wifi_scan_data_t* Pointer to the list of found Wi-Fi networks.
 */
wifi_scan_data_t *network_getFoundNetworks() { return &wifiScanData; }

static void wifiLinkCallback(struct netif *netif) {
  DPRINTF("WiFi Link: %s\n", (netif_is_link_up(netif) ? "UP" : "DOWN"));
}

static void networkStatusCallback(struct netif *netif) {
  DPRINTF("WiFi Status: %s\n", (netif_is_up(netif) ? "UP" : "DOWN"));
  if (netif_is_up(netif)) {
    connectionStatus = CONNECTED_WIFI_IP;
    snprintf(connectionStatusStr, sizeof(connectionStatusStr), "LINK UP");
    DPRINTF("IP address allocated: %s\n", ipaddr_ntoa(netif_ip_addr4(netif)));
    ip_addr_set(&currentIp, netif_ip_addr4(netif));
  } else {
    DPRINTF("WiFi Status: DOWN\n");
  }
}

wifi_sta_conn_process_status_t network_wifiStaConnect() {
  if (!cyw43Initialized) {
    DPRINTF("WiFi not initialized. Cancelling connection\n");
    return NETWORK_WIFI_STA_CONN_ERR_NOT_INITIALIZED;
  }
  if (wifiCurrentMode != WIFI_MODE_STA) {
    DPRINTF("WiFi mode is not STA. Cancelling connection\n");
    return NETWORK_WIFI_STA_CONN_ERR_INVALID_MODE;
  }

  int res;

  // Set hostname
  char *hostname =
      settings_find_entry(gconfig_getContext(), PARAM_HOSTNAME)->value;

  // Set the STA mode interface mode
  struct netif *nif = &cyw43_state.netif[CYW43_ITF_STA];

  cyw43_arch_lwip_begin();

  if ((hostname != NULL) && (strlen(hostname) > 0)) {
    strncpy(wifiHostname, hostname, sizeof(wifiHostname));
  } else {
    snprintf(wifiHostname, sizeof(wifiHostname), "SidecarT-%s",
             picoSerialStr());
  }
  DPRINTF("Hostname: %s\n", wifiHostname);
  netif_set_hostname(nif, wifiHostname);

  // Set callbacks
  netif_set_link_callback(nif, wifiLinkCallback);
  netif_set_status_callback(nif, networkStatusCallback);

  // DHCP or static IP
  if ((settings_find_entry(gconfig_getContext(), PARAM_WIFI_DHCP) != NULL) &&
      (settings_find_entry(gconfig_getContext(), PARAM_WIFI_DHCP)->value[0] ==
           't' ||
       settings_find_entry(gconfig_getContext(), PARAM_WIFI_DHCP)->value[0] ==
           'T')) {
    DPRINTF("DHCP enabled\n");
  } else {
    DPRINTF("Static IP enabled\n");
    dhcp_stop(nif);
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gwy;
    ipaddr.addr = ipaddr_addr(
        settings_find_entry(gconfig_getContext(), PARAM_WIFI_IP)->value);
    netmask.addr = ipaddr_addr(
        settings_find_entry(gconfig_getContext(), PARAM_WIFI_NETMASK)->value);
    gwy.addr = ipaddr_addr(
        settings_find_entry(gconfig_getContext(), PARAM_WIFI_GATEWAY)->value);
    netif_set_addr(nif, &ipaddr, &netmask, &gwy);
    DPRINTF("IP: %s\n", ipaddr_ntoa(&ipaddr));
    DPRINTF("Netmask: %s\n", ipaddr_ntoa(&netmask));
    DPRINTF("Gateway: %s\n", ipaddr_ntoa(&gwy));

    // Now set the DNS
    // The values in PARAM_WIFI_DNS are separated by commas. Only one or two
    // values are allowed
    SettingsConfigEntry *entry =
        settings_find_entry(gconfig_getContext(), PARAM_WIFI_DNS);
    if (entry == NULL || entry->value == NULL) {
      DPRINTF("Error: DNS configuration is missing.\n");
    } else {
      char *dns = entry->value;
      char *dnsCopy = strdup(
          dns);  // Make a copy of the string to avoid modifying the original
      if (dnsCopy == NULL) {
        DPRINTF("Error: Memory allocation failed.\n");
      } else {
        char *dns1 = strtok(dnsCopy, ",");
        char *dns2 = strtok(NULL, ",");

        ip_addr_t dns1Ip;
        ip_addr_t dns2Ip;
        if (dns1 == NULL || (dns1Ip.addr = ipaddr_addr(dns1)) == IPADDR_NONE) {
          DPRINTF("Error: Invalid DNS1 address.\n");
          free(dnsCopy);  // Free the allocated memory
        } else {
          dns_setserver(0, &dns1Ip);
          DPRINTF("DNS1: %s\n", ipaddr_ntoa(&dns1Ip));

          if (dns2 != NULL) {
            if ((dns2Ip.addr = ipaddr_addr(dns2)) == IPADDR_NONE) {
              DPRINTF("Error: Invalid DNS2 address.\n");
            } else {
              dns_setserver(1, &dns2Ip);
              DPRINTF("DNS2: %s\n", ipaddr_ntoa(&dns2Ip));
            }
          }
        }
      }

      free(dnsCopy);  // Free the copied string after use
    }
  }
  netif_set_up(nif);

  cyw43_arch_lwip_end();

  // Get the MAC address
  if ((res = cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, cyw43Mac))) {
    DPRINTF("Failed to get MAC address: %d\n", res);
    return NETWORK_WIFI_STA_CONN_ERR_MAC_FAILED;
  }

  SettingsConfigEntry *ssid =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_SSID);
  if (strlen(ssid->value) == 0) {
    DPRINTF("No SSID found in config. Can't connect\n");
    return NETWORK_WIFI_STA_CONN_ERR_NO_SSID;
  }
  SettingsConfigEntry *authMode =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_AUTH);
  if (strlen(authMode->value) == 0) {
    DPRINTF("No auth mode found in config. Can't connect\n");
    return NETWORK_WIFI_STA_CONN_ERR_NO_AUTH_MODE;
  }
  char *passwordValue = NULL;
  SettingsConfigEntry *password =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_PASSWORD);
  if (strlen(password->value) > 0) {
    passwordValue = strdup(password->value);
  } else {
    DPRINTF(
        "No password found in config. Trying to connect without password\n");
  }
  DPRINTF("The password is: %s\n", passwordValue);

  uint32_t authValue = getAuthPicoCode(atoi(authMode->value));
  int errorCode = 0;
  DPRINTF("Connecting to SSID=%s, password=%s, auth=%08x. ASYNC\n", ssid->value,
          passwordValue, authValue);
  errorCode =
      cyw43_arch_wifi_connect_async(ssid->value, passwordValue, authValue);
  free(passwordValue);
  if (errorCode != 0) {
    DPRINTF("Failed to connect to WiFi: %d\n", errorCode);
    return NETWORK_WIFI_STA_CONN_ERR_CONNECTION_FAILED;
  }

  // Enter a loop until the device has a WiFi connection with an IP address. Or
  // timesout.
  wifi_sta_conn_status_t prevStatus = DISCONNECTED;
  int wifiConnPollingInterval = 1;  // 1 seconds
  absolute_time_t wifiConnStatusTime = make_timeout_time_ms(1 * SEC_TO_MS);
  absolute_time_t wifiConnConnTimeout =
      make_timeout_time_ms(NETWORK_CONNECT_TIMEOUT * SEC_TO_MS);  // 30 seconds
  while (absolute_time_diff_us(get_absolute_time(), wifiConnConnTimeout) > 0) {
#ifdef BLINK_H
    blink_morse('T');
#endif

    wifi_sta_conn_status_t status =
        network_wifiConnStatus(&wifiConnStatusTime, wifiConnPollingInterval);
#if PICO_CYW43_ARCH_POLL
    network_safePoll();
    cyw43_arch_wait_for_work_until(make_timeout_time_ms(2 * SEC_TO_MS));
#else
    sleep_ms(NETWORK_POLLING_INTERVAL);
#endif
    if (networkPollingCallback != NULL) {
      networkPollingCallback();
    }
    if (status != prevStatus) {
      DPRINTF("WiFi connection status: %s[%i]\n", network_wifiConnStatusStr(),
              status);
      prevStatus = status;
    }
    if (status == CONNECTED_WIFI_IP) {
#ifdef BLINK_H
      blink_on();
#endif
      break;
    }
  }
  if (absolute_time_diff_us(get_absolute_time(), wifiConnConnTimeout) <= 0) {
    DPRINTF("WiFi connection timeout\n");
    return NETWORK_WIFI_STA_CONN_ERR_TIMEOUT;
  }

  DPRINTF("Connected. Check the connection status...\n");
  return 0;
}

char *network_wifiConnStatusStr() { return connectionStatusStr; }

wifi_sta_conn_status_t network_wifiConnStatus(
    absolute_time_t *wifiConnStatusTime, int wifiConStatusInterval) {
  if (!cyw43Initialized) {
    DPRINTF("WiFi not initialized. Cancelling connection\n");
    return -1;
  }
  if (wifiCurrentMode != WIFI_MODE_STA) {
    DPRINTF("WiFi mode is not STA. Cancelling connection\n");
    return -2;
  }
  // If wifi_conn_status_time is NULL or the time has elapsed, check the
  // connection status
  if ((wifiConnStatusTime == NULL) ||
      (absolute_time_diff_us(get_absolute_time(), *wifiConnStatusTime) < 0)) {
    // Check the connection status
    int linkStatus = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    switch (linkStatus) {
      case CYW43_LINK_DOWN: {
        connectionStatus = DISCONNECTED;
        snprintf(connectionStatusStr, sizeof(connectionStatusStr), "LINK DOWN");
        break;
      }
      case CYW43_LINK_JOIN: {
        connectionStatus = CONNECTED_WIFI;
        snprintf(connectionStatusStr, sizeof(connectionStatusStr), "LINK JOIN");
        break;
      }
      case CYW43_LINK_NOIP: {
        connectionStatus = CONNECTED_WIFI_NO_IP;
        snprintf(connectionStatusStr, sizeof(connectionStatusStr),
                 "LINK NO IP");
        break;
      }
      case CYW43_LINK_UP: {
        connectionStatus = CONNECTED_WIFI_IP;
        snprintf(connectionStatusStr, sizeof(connectionStatusStr), "LINK UP");
        break;
      }
      case CYW43_LINK_FAIL: {
        connectionStatus = GENERIC_ERROR;
        snprintf(connectionStatusStr, sizeof(connectionStatusStr), "LINK FAIL");
        break;
      }
      case CYW43_LINK_NONET: {
        connectionStatus = CONNECT_FAILED_ERROR;
        snprintf(connectionStatusStr, sizeof(connectionStatusStr),
                 "LINK NO NET");
        break;
      }
      case CYW43_LINK_BADAUTH: {
        connectionStatus = BADAUTH_ERROR;
        snprintf(connectionStatusStr, sizeof(connectionStatusStr),
                 "LINK BAD AUTH");
        break;
      }
      default: {
        connectionStatus = GENERIC_ERROR;
        snprintf(connectionStatusStr, sizeof(connectionStatusStr),
                 "LINK UNKNOWN");
      }
    }
    *wifiConnStatusTime =
        make_timeout_time_ms(wifiConStatusInterval * SEC_TO_MS);
  }
  // else {
  //     DPRINTF("Connection status check skipped\n");
  // }
  return connectionStatus;
}

/**
 * @brief Retrieves the current IP address.
 *
 * This function returns the current IP address stored in the system.
 *
 * @return The current IP address as an ip_addr_t structure.
 */
ip_addr_t network_getCurrentIp() { return currentIp; }
