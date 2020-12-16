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

 * Copyright  2018-2020 Klaus Wilting <verkehrsrot@arcor.de>
 * https://github.com/cyberman54/ESP32-Paxcounter
 * https://github.com/cyberman54/ESP32-Paxcounter/blob/30731f5c0ce5396fdbcc0d5147481a5c69e15bff/src/wifiscan.cpp

Which in turn is based of Łukasz Marcin Podkalicki's ESP32/016 WiFi Sniffer

 * Copyright 2017 Łukasz Marcin Podkalicki <lpodkalicki@gmail.com>
 * https://github.com/lpodkalicki/blog/tree/master/esp32/016_wifi_sniffer

*/
#include "globals.h"

#include <string.h>
#include "wifiscan.h"
#include "libpax.h"

TimerHandle_t WifiChanTimer;
int initialized_wifi = 0;
int rssi_threshold = -80;

// configData_t cfg_pax;

#define WIFI_CHANNEL_MAX 13
// default values for country configuration
static wifi_country_t wifi_country = {"EU", 1,
                                      WIFI_CHANNEL_MAX, 100,
                                      WIFI_COUNTRY_POLICY_MANUAL};

void wifi_noop_sniffer(void* buff, wifi_promiscuous_pkt_type_t type) {}

// using IRAM_ATTR here to speed up callback function
static IRAM_ATTR void
wifi_sniffer_packet_handler(void* buff, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* ppkt = (wifi_promiscuous_pkt_t*)buff;
  const wifi_ieee80211_packet_t* ipkt = (wifi_ieee80211_packet_t*)ppkt->payload;
  const wifi_ieee80211_mac_hdr_t* hdr = &ipkt->hdr;

  if ((rssi_threshold) &&
      (ppkt->rx_ctrl.rssi < rssi_threshold))  // rssi is negative value
  {
    return;
  }

  int universal_bit = hdr->addr2[0] & 0b10;

  if(!universal_bit) {
     return;
  }

  mac_add((uint8_t *)hdr->addr2, MAC_SNIFF_WIFI);
}

uint16_t channels_map;
// Software-timer driven Wifi channel rotation callback function
void switchWifiChannel(TimerHandle_t xTimer) {
  configASSERT(xTimer);
  do { channel =
      (channel % WIFI_CHANNEL_MAX) + 1;  // rotate channels in bitmap
  } while (!(channels_map >> (channel - 1) & 1));
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void set_country(uint8_t cc) {
  switch(cc) {
      case 1:
        memcpy(wifi_country.cc, "EU", sizeof("EU"));
      break;
  }
}

void set_channels(uint16_t set_channels_map) {
  channels_map = set_channels_map;
}

void set_rssi_filter(int8_t set_rssi_threshold) {
  rssi_threshold = set_rssi_threshold;
}

void wifi_sniffer_init(uint16_t wifi_channel_switch_interval) {
  #ifdef LIBPAX_WIFI
  wifi_init_config_t wificfg = WIFI_INIT_CONFIG_DEFAULT();
  wificfg.nvs_enable = 0;         // we don't need any wifi settings from NVRAM
  wificfg.wifi_task_core_id = 0;  // we want wifi task running on core 0

  // wifi_promiscuous_filter_t filter = {
  //    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT}; // only MGMT frames
  // .filter_mask = WIFI_PROMIS_FILTER_MASK_ALL}; // we use all frames

  wifi_promiscuous_filter_t filter = {.filter_mask =
                                          WIFI_PROMIS_FILTER_MASK_MGMT |
                                          WIFI_PROMIS_FILTER_MASK_DATA};

  ESP_ERROR_CHECK(esp_wifi_init(&wificfg));  // configure Wifi with cfg
  ESP_ERROR_CHECK(
      esp_wifi_set_country(&wifi_country));  // set locales for RF and channels
  ESP_ERROR_CHECK(
      esp_wifi_set_storage(WIFI_STORAGE_RAM));  // we don't need NVRAM
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));  // no modem power saving
  ESP_ERROR_CHECK(
      esp_wifi_set_promiscuous_filter(&filter));  // set frame filter
  ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler));
  ESP_ERROR_CHECK(esp_wifi_start());  // for esp_wifi v3.3
  ESP_ERROR_CHECK(
      esp_wifi_set_promiscuous(true));  // now switch on monitor mode

  // setup wifi channel rotation timer


  if(wifi_channel_switch_interval > 0) {
    WifiChanTimer = xTimerCreate("WifiChannelTimer", pdMS_TO_TICKS(wifi_channel_switch_interval * 10),
                                pdTRUE, (void*)0, switchWifiChannel);
    assert(WifiChanTimer);
    xTimerStart(WifiChanTimer, 0);
  }
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_wifi_set_promiscuous(true);
  initialized_wifi = 1;
  #endif
}

void wifi_sniffer_stop() {
  #ifdef LIBPAX_WIFI
  if(initialized_wifi) {
    if(WifiChanTimer) xTimerStop(WifiChanTimer, 0);
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(&wifi_noop_sniffer));
    ESP_ERROR_CHECK(
        esp_wifi_set_promiscuous(false));  // now switch off monitor mode
    ESP_ERROR_CHECK(esp_wifi_stop());
    initialized_wifi = 0;
  }
  #endif
}
