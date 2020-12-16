/*
LICENSE

Copyright  2020      Deutsche Bahn Station&Service AG

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


This file is based of the ESP32-Paxcounter:

 * Copyright  2018-2020 Oliver Brandmueller <ob@sysadm.in>
 * Copyright  2018-2020 Klaus Wilting <verkehrsrot@arcor.de>
 * https://github.com/cyberman54/ESP32-Paxcounter
 * https://github.com/cyberman54/ESP32-Paxcounter/blob/30731f5c0ce5396fdbcc0d5147481a5c69e15bff/src/blecsan.cpp

Which in turn is based of nkolban's esp32 snippets:

  https://github.com/nkolban/esp32-snippets/tree/master/BLE/scanner

*/
 

#include "blescan.h"
#include "libpax.h"

#define BT_BD_ADDR_HEX(addr)                                                   \
  addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]

// local Tag for logging
static const char TAG[] = "bluetooth";
int initialized_ble = 0;
uint16_t used_blescantime = 0;

#ifdef VERBOSE
const char *bt_addr_t_to_string(esp_ble_addr_type_t type) {
  switch (type) {
  case BLE_ADDR_TYPE_PUBLIC:
    return "BLE_ADDR_TYPE_PUBLIC";
  case BLE_ADDR_TYPE_RANDOM:
    return "BLE_ADDR_TYPE_RANDOM";
  case BLE_ADDR_TYPE_RPA_PUBLIC:
    return "BLE_ADDR_TYPE_RPA_PUBLIC";
  case BLE_ADDR_TYPE_RPA_RANDOM:
    return "BLE_ADDR_TYPE_RPA_RANDOM";
  default:
    return "Unknown addr_t";
  }
} // bt_addr_t_to_string

const char *btsig_gap_type(uint32_t gap_type) {
  switch (gap_type) {
  case 0x01:
    return "Flags";
  case 0x02:
    return "Incomplete List of 16-bit Service Class UUIDs";
  case 0x03:
    return "Complete List of 16-bit Service Class UUIDs";
  case 0x04:
    return "Incomplete List of 32-bit Service Class UUIDs";
  case 0x05:
    return "Complete List of 32-bit Service Class UUIDs";
  case 0x06:
    return "Incomplete List of 128-bit Service Class UUIDs";
  case 0x07:
    return "Complete List of 128-bit Service Class UUIDs";
  case 0x08:
    return "Shortened Local Name";
  case 0x09:
    return "Complete Local Name";
  case 0x0A:
    return "Tx Power Level";
  case 0x0D:
    return "Class of Device";
  case 0x0E:
    return "Simple Pairing Hash C/C-192";
  case 0x0F:
    return "Simple Pairing Randomizer R/R-192";
  case 0x10:
    return "Device ID/Security Manager TK Value";
  case 0x11:
    return "Security Manager Out of Band Flags";
  case 0x12:
    return "Slave Connection Interval Range";
  case 0x14:
    return "List of 16-bit Service Solicitation UUIDs";
  case 0x1F:
    return "List of 32-bit Service Solicitation UUIDs";
  case 0x15:
    return "List of 128-bit Service Solicitation UUIDs";
  case 0x16:
    return "Service Data - 16-bit UUID";
  case 0x20:
    return "Service Data - 32-bit UUID";
  case 0x21:
    return "Service Data - 128-bit UUID";
  case 0x22:
    return "LE Secure Connections Confirmation Value";
  case 0x23:
    return "LE Secure Connections Random Value";
  case 0x24:
    return "URI";
  case 0x25:
    return "Indoor Positioning";
  case 0x26:
    return "Transport Discovery Data";
  case 0x17:
    return "Public Target Address";
  case 0x18:
    return "Random Target Address";
  case 0x19:
    return "Appearance";
  case 0x1A:
    return "Advertising Interval";
  case 0x1B:
    return "LE Bluetooth Device Address";
  case 0x1C:
    return "LE Role";
  case 0x1D:
    return "Simple Pairing Hash C-256";
  case 0x1E:
    return "Simple Pairing Randomizer R-256";
  case 0x3D:
    return "3D Information Data";
  case 0xFF:
    return "Manufacturer Specific Data";

  default:
    return "Unknown type";
  }
} // btsig_gap_type
#endif

// using IRAM_ATTR here to speed up callback function
IRAM_ATTR void gap_callback_handler(esp_gap_ble_cb_event_t event,
                                    esp_ble_gap_cb_param_t *param) {

  esp_ble_gap_cb_param_t *p = (esp_ble_gap_cb_param_t *)param;

#if (COUNT_ENS)
  // UUID of Exposure Notification Service (ENS)
  // https://blog.google/documents/70/Exposure_Notification_-_Bluetooth_Specification_v1.2.2.pdf
  static const char ensMagicBytes[] = "\x16\x6f\xfd";
#endif

#ifdef VERBOSE
  ESP_LOGV(TAG, "BT payload rcvd -> type: 0x%.2x -> %s", *p->scan_rst.ble_adv,
           btsig_gap_type(*p->scan_rst.ble_adv));
#endif

  switch (event) {
  case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
    // restart scan
    ESP_ERROR_CHECK(esp_ble_gap_start_scanning(used_blescantime));
    break;

  case ESP_GAP_BLE_SCAN_RESULT_EVT:
    // evaluate scan results
    if (p->scan_rst.search_evt ==
        ESP_GAP_SEARCH_INQ_CMPL_EVT) // Inquiry complete, scan is done
    {                                // restart scan
      ESP_ERROR_CHECK(esp_ble_gap_start_scanning(used_blescantime));
      return;
    }

    if (p->scan_rst.search_evt ==
        ESP_GAP_SEARCH_INQ_RES_EVT) // Inquiry result for a peer device
    {                               // evaluate sniffed packet
#ifdef VERBOSE
      ESP_LOGV(TAG, "Device address (bda): %02x:%02x:%02x:%02x:%02x:%02x",
               BT_BD_ADDR_HEX(p->scan_rst.bda));
      ESP_LOGV(TAG, "Addr_type           : %s",
               bt_addr_t_to_string(p->scan_rst.ble_addr_type));
      ESP_LOGV(TAG, "RSSI                : %d", p->scan_rst.rssi);
#endif

#if (VENDORFILTER)
      if ((p->scan_rst.ble_addr_type == BLE_ADDR_TYPE_RANDOM) ||
          (p->scan_rst.ble_addr_type == BLE_ADDR_TYPE_RPA_RANDOM)) {
#ifdef VERBOSE
        ESP_LOGV(TAG, "BT device filtered");
#endif
        break;
      }
#endif

      // add this device mac to processing queue

#if (COUNT_ENS)
      // check for ens signature
      if (cfg.enscount) {
        if (strstr((const char *)p->scan_rst.ble_adv, ensMagicBytes) != NULL)
          mac_add((uint8_t *)p->scan_rst.bda, MAC_SNIFF_BLE_ENS);
        else
          mac_add((uint8_t *)p->scan_rst.bda, MAC_SNIFF_BLE);
      }
#else
      mac_add((uint8_t *)p->scan_rst.bda, MAC_SNIFF_BLE);
#endif

      /* to be improved in vendorfilter if:
      // you can search for elements in the payload using the
      // function esp_ble_resolve_adv_data()
      //
      // Like this, that scans for the "Complete name" (looking inside the
      payload buffer)
      // uint8_t len;
      // uint8_t *data = esp_ble_resolve_adv_data(p->scan_rst.ble_adv,
      ESP_BLE_AD_TYPE_NAME_CMPL, &len);

      filter BLE devices using their advertisements to get filter alternative
      to vendor OUI if vendorfiltering is on, we ...
      - want to count: mobile phones and tablets
      - don't want to count: beacons, peripherals (earphones, headsets,
      printers), cars and machines see
      https://github.com/nkolban/ESP32_BLE_Arduino/blob/master/src/BLEAdvertisedDevice.cpp

      http://www.libelium.com/products/meshlium/smartphone-detection/

      https://www.question-defense.com/2013/01/12/bluetooth-cod-bluetooth-class-of-deviceclass-of-service-explained

      https://www.bluetooth.com/specifications/assigned-numbers/baseband

      "The Class of Device (CoD) in case of Bluetooth which allows us to
      differentiate the type of device (smartphone, handsfree, computer,
      LAN/network AP). With this parameter we can differentiate among
      pedestrians and vehicles."

      */

    } // evaluate sniffed packet
    break;

  default:
    break;
  } // switch
} // gap_callback_handler

esp_err_t register_ble_callback(uint16_t blescantime, uint16_t blescanwindow, uint16_t blescaninterval) {
  ESP_LOGI(TAG, "Register GAP callback");

  // set blescantime to be used in callback
  used_blescantime = blescantime;

  // This function is called when gap event occurs, such as scan result.
  // register the scan callback function to the gap module
  ESP_ERROR_CHECK(esp_ble_gap_register_callback(&gap_callback_handler));

  static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_PASSIVE,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,

#if (VENDORFILTER)
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_WLIST_PRA_DIR,
  // ADV_IND, ADV_NONCONN_IND, ADV_SCAN_IND packets are used for broadcasting
  // data in broadcast applications (e.g., Beacons), so we don't want them in
  // vendorfilter mode
#else
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
#endif

    .scan_interval = 0,
    .scan_window = 0,

    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE // Report each packet - no de-duplication.
  };

  ble_scan_params.scan_interval = (uint16_t)(blescantime * 10 / 0.625); // Time = N * 0.625 msec
  ble_scan_params.scan_window = (uint16_t)(blescanwindow / 0.625); // Time = N * 0.625 msec

  ESP_LOGI(TAG, "Set GAP scan parameters");

  // This function is called to set scan parameters.
  ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&ble_scan_params));

  return ESP_OK;

} // register_ble_callback

void start_BLE_scan(uint16_t blescantime, uint16_t blescanwindow, uint16_t blescaninterval) {
#ifdef LIBPAX_BLE
  ESP_LOGI(TAG, "Initializing bluetooth scanner ...");

  // Initialize BT controller to allocate task and other resource.
  ESP_ERROR_CHECK(esp_coex_preference_set(ESP_COEX_PREFER_BT));

  
  #ifdef LIBPAX_ARDUINO
  btStart();
  #endif
  #ifdef LIBPAX_ESPIDF
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
  ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
  #endif

  ESP_ERROR_CHECK(esp_bluedroid_init());
  ESP_ERROR_CHECK(esp_bluedroid_enable());

  // Register callback function for capturing bluetooth packets
  ESP_ERROR_CHECK(register_ble_callback(blescantime, blescanwindow, blescaninterval));

  ESP_LOGI(TAG, "Bluetooth scanner started");
  initialized_ble = 1;
#endif
} // start_BLEscan

void stop_BLE_scan(void) {
#ifdef LIBPAX_BLE
  if (initialized_ble) {
    ESP_LOGI(TAG, "Shutting down bluetooth scanner ...");
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(NULL));
    ESP_ERROR_CHECK(esp_bluedroid_disable());
    ESP_ERROR_CHECK(esp_bluedroid_deinit());
    #ifdef LIBPAX_ARDUINO
    btStop(); // disable bt_controller
    #endif
    #ifdef LIBPAX_ESPIDF
    ESP_ERROR_CHECK(esp_bt_controller_disable());
    ESP_ERROR_CHECK(esp_bt_controller_deinit());
    #endif
    ESP_ERROR_CHECK(esp_coex_preference_set(ESP_COEX_PREFER_WIFI));
    ESP_LOGI(TAG, "Bluetooth scanner stopped");
    initialized_ble = 0;
  }
#endif
} // stop_BLEscan
