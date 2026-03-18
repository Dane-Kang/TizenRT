/* ***************************************************************************
 *
 * Copyright 2019-2021 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdlib.h>
#include <string.h>
#include <tinyara/time.h>
#include <wifi_manager/wifi_manager.h>
#include "iot_debug.h"
#include "iot_bsp_wifi.h"
#include "iot_os_util.h"
#include "iot_util.h"
#include <semaphore.h>
#include <errno.h>
#ifdef CONFIG_ARCH_BOARD_ESP32_FAMILY
#include <tinyara/wifi/wifi_utils.h>
#endif
#ifdef CONFIG_NETUTILS_NTPCLIENT
#include <protocols/ntpclient.h>
#endif

static sem_t g_scan_sem;
static int g_scan_sem_inited = 0;
static volatile int g_scan_done = 0;

static sem_t g_connection_sem;
static int g_connection_sem_inited = 0;
static volatile int g_connection_done = 0;

typedef struct {
	uint16_t ap_num;
	wifi_manager_scan_info_s wifi_scan_result[IOT_WIFI_MAX_SCAN_RESULT];
}t_wifi_manager_scan_result;

static t_wifi_manager_scan_result wifi_manager_scan_result;

static int WIFI_INITIALIZED = false;

static struct iot_mac bsp_mac;

// if _wt_scan_done is for WT_TYPE_SJOIN then it doesn't need to print
// scan list. So if g_scan_join is 1 then it doesn't print scan list
static int g_scan_join = 0;
static int g_scanned_result = 0;
static char g_scanned_ssid[WIFIMGR_SSID_LEN + 1] = {
	0,
};
static wifi_manager_ap_auth_type_e g_scanned_auth_type = WIFI_MANAGER_AUTH_OPEN;
static wifi_manager_ap_crypto_type_e g_scanned_crypto_type = WIFI_MANAGER_CRYPTO_NONE;

#ifdef CONFIG_NETUTILS_NTPCLIENT
static struct ntpc_server_conn_s server_conn[] = {
	{CONFIG_NETUTILS_NTPCLIENT_DEFAULT_SERVER, CONFIG_NETUTILS_NTPCLIENT_DEFAULT_SERVER_PORT},
	{"pool.ntp.org", CONFIG_NETUTILS_NTPCLIENT_DEFAULT_SERVER_PORT},
	{"1.kr.pool.ntp.org", CONFIG_NETUTILS_NTPCLIENT_DEFAULT_SERVER_PORT},
	{"1.asia.pool.ntp.org", CONFIG_NETUTILS_NTPCLIENT_DEFAULT_SERVER_PORT},
	{"us.pool.ntp.org", CONFIG_NETUTILS_NTPCLIENT_DEFAULT_SERVER_PORT},
};

static void ntp_link_error(void)
{
	IOT_ERROR("ntp_link_error() callback is called");
}
#endif

static bool _initialize_ntp(void)
{
	IOT_INFO("Initializing NTP");

#ifdef CONFIG_NETUTILS_NTPCLIENT
	if (ntpc_get_status() == NTP_RUNNING) {
		IOT_WARN("ntpc has been already running");
		return true;
	}

	struct timespec init_tp;
	init_tp.tv_sec = 0;
	init_tp.tv_nsec = 0;
	clock_settime(CLOCK_REALTIME, &init_tp);

	if (ntpc_start(server_conn, sizeof(server_conn) / sizeof(server_conn[0]), 0, (void *)ntp_link_error) < 0) {
		IOT_ERROR("ntpc_start() failed");
		return false;
	}

	IOT_INFO("ntpc_start() succeed");
	return true;
//#else
//#error "CONFIG_NETUTILS_NTPCLIENT not set"
#endif
}

static void _obtain_time(void)
{
	time_t now = 0;
	struct tm timeinfo = { 0 };
	int retry = 0;
	const int retry_count = 10;

	if (!_initialize_ntp()) {
		IOT_ERROR("NTP initialize failed, can not obtain time!");
		return;
	}

	/* wait for time to be set, system time initialized in _initialize_ntp */
	while (timeinfo.tm_year <= (EPOCH_YEAR - TM_YEAR_BASE) && ++retry < retry_count) {
		IOT_INFO("Waiting for system time to be set... (%d/%d)", retry, retry_count);
		IOT_DELAY(3000);
		time(&now);
		localtime_r(&now, &timeinfo);
	}

	if (retry < retry_count) {
		IOT_INFO("[WIFI] system time updated by %ld", now);
	} else {
		IOT_ERROR("[WIFI] system time has not been synced yet!");
	}
}

static int _wm_get_scanned_list(wifi_manager_scan_info_s *slist, char *ssid,
						 wifi_manager_ap_auth_type_e *atype,
						 wifi_manager_ap_crypto_type_e *ctype)
{
	int ssid_len = strlen(ssid);
	while (slist) {
		if (strncmp(ssid, slist->ssid, ssid_len + 1) == 0) {
			*atype = slist->ap_auth_type;
			*ctype = slist->ap_crypto_type;
			return 0;
		}
		slist = slist->next;
	}
	return -1;
}

void _wm_save_scanlist(wifi_manager_scan_info_s *slist)
{
	while (slist != NULL) {
		memcpy(&wifi_manager_scan_result.wifi_scan_result[wifi_manager_scan_result.ap_num], slist, sizeof(wifi_manager_scan_info_s));
		wifi_manager_scan_result.ap_num++;
		if (wifi_manager_scan_result.ap_num >= IOT_WIFI_MAX_SCAN_RESULT) {
			break;
		}
		// IOT_DEBUG("WiFi AP SSID: %-25s, BSSID: %-20s, Rssi: %d, Auth: %d, Crypto: %d",
		// 	   slist->ssid, slist->bssid, slist->rssi,
		// 	   slist->ap_auth_type, slist->ap_crypto_type);
		slist = slist->next;
	}
}

void bsp_wm_sta_connected(wifi_manager_cb_msg_s msg, void *arg)
{
	IOT_INFO(" T%d --> %s res(%d)", getpid(), __FUNCTION__);
	g_connection_done = 1;
    if (g_connection_sem_inited) sem_post(&g_connection_sem);
}

void bsp_wm_sta_disconnected(wifi_manager_cb_msg_s msg, void *arg)
{
	IOT_INFO(" T%d --> %s", getpid(), __FUNCTION__);
	g_connection_done = 0;
	if (g_connection_sem_inited) sem_post(&g_connection_sem);
}

void bsp_wm_softap_sta_join(wifi_manager_cb_msg_s msg, void *arg)
{
	IOT_INFO(" T%d --> %s", getpid(), __FUNCTION__);
}

void bsp_wm_softap_sta_leave(wifi_manager_cb_msg_s msg, void *arg)
{
	IOT_INFO(" T%d --> %s", getpid(), __FUNCTION__);
}

void bsp_wm_scan_done(wifi_manager_cb_msg_s msg, void *arg)
{
	IOT_INFO(" T%d --> %s", getpid(), __FUNCTION__);
	/* Make sure you copy the scan results onto a local data structure.
	 * It will be deleted soon eventually as you exit this function.
	 */
	if (msg.res != WIFI_MANAGER_SUCCESS || msg.scanlist == NULL) {
		IOT_INFO(" T%d --> %s", getpid(), __FUNCTION__);
		g_scan_done = 1;
        if (g_scan_sem_inited) sem_post(&g_scan_sem);
		return;
	}
	if (g_scan_join == 0) {
		_wm_save_scanlist(msg.scanlist);
	} else {
		/* request type is WT_TYPE_SJOIN. so it doesn't print scan list
		 * and pass scan list result to _wt_scan_connect;
		 */
		g_scanned_result = _wm_get_scanned_list(msg.scanlist,
												g_scanned_ssid,
												&g_scanned_auth_type,
												&g_scanned_crypto_type);
	}

	g_scan_done = 1;
    if (g_scan_sem_inited) sem_post(&g_scan_sem);
}

/* Global */
static wifi_manager_cb_s _wifi_callbacks = {
	bsp_wm_sta_connected,
	bsp_wm_sta_disconnected,
	bsp_wm_softap_sta_join,
	bsp_wm_softap_sta_leave,
	bsp_wm_scan_done,
};

iot_error_t iot_bsp_wifi_init()
{
	if (WIFI_INITIALIZED) {
		return IOT_ERROR_NONE;
	}

	wifi_manager_result_e res = WIFI_MANAGER_SUCCESS;
	res = wifi_manager_init(&_wifi_callbacks);
	if (res != WIFI_MANAGER_SUCCESS) {
		IOT_INFO(" wifi_manager_init fail");
		return IOT_ERROR_INIT_FAIL;
	}

	if (!g_scan_sem_inited) {
    	sem_init(&g_scan_sem, 0, 0);
    	g_scan_sem_inited = 1;
	}

	if (!g_connection_sem_inited) {
    	sem_init(&g_connection_sem, 0, 0);
    	g_connection_sem_inited = 1;
	}
	g_connection_done = 0;

	/*
	 * get mac once after wm init, avoid getting mac during wifi provisioning,
	 * which would cause a dead lock issue
	 */
	struct iot_mac init_mac;
	iot_bsp_wifi_get_mac(&init_mac);

	WIFI_INITIALIZED = true;
	
	IOT_INFO(" iot_bsp_wifi_init done");
	return IOT_ERROR_NONE;
}

int _iot_bsp_sta_mode(void)
{
#ifdef CONFIG_ARCH_BOARD_ESP32_FAMILY
	wifi_utils_info_s info;
	wifi_utils_get_info(&info);
	return (info.wifi_status == WIFI_UTILS_SOFTAP_MODE) ? 0 : 1;
#else
	wifi_manager_info_s info;
	wifi_manager_get_info(&info);
	return (info.mode == STA_MODE) ? 1 : 0;
#endif
}

iot_error_t iot_bsp_wifi_set_mode(iot_wifi_conf *conf)
{
	int str_len = 0;
	wifi_manager_result_e res;
	wifi_manager_softap_config_s sap_config;
	wifi_manager_ap_config_s apconfig;

	switch(conf->mode) {
		case IOT_WIFI_MODE_OFF:
			//esp_wifi_set_mode(WIFI_MODE_NULL);
		break;
		case IOT_WIFI_MODE_SCAN:
			/*stdk first scan on sta mode, and then scan on softap mode.
			for tizenrt, we only do the first scan, and reuse the result.*/
			// scan 결과 초기화 (안하면 이전 결과가 남아있을 수 있음)
			wifi_manager_scan_result.ap_num = 0;
			memset(wifi_manager_scan_result.wifi_scan_result, 0, sizeof(wifi_manager_scan_result.wifi_scan_result));
			g_scan_done = 0;
			// sem 값이 이전에 남아 있을 수 있으니 drain
			if (g_scan_sem_inited) {
				while (sem_trywait(&g_scan_sem) == 0) { /* empty */ }
			}

			//if (_iot_bsp_sta_mode() == 1) {
					res = wifi_manager_scan_ap(NULL);
					if (res != WIFI_MANAGER_SUCCESS) {
						IOT_ERROR("wifi_manager_scan_ap fail");
						return IOT_ERROR_CONN_OPERATE_FAIL;
				}
			//}

			{
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				ts.tv_sec += 10;

				while (!g_scan_done) {
					int w = sem_timedwait(&g_scan_sem, &ts);
					if (w == 0) break;
					if (errno == EINTR) continue;
					if (errno == ETIMEDOUT) {
						IOT_ERROR("WiFi scan timeout");
						return IOT_ERROR_CONN_OPERATE_FAIL;
					}
					IOT_ERROR("sem_timedwait error (%d)", errno);
					return IOT_ERROR_CONN_OPERATE_FAIL;
				}
			}
		break;

		case IOT_WIFI_MODE_STATION:
			if (_iot_bsp_sta_mode() == 0) {
				res = wifi_manager_set_mode(STA_MODE, NULL);
				if (res != WIFI_MANAGER_SUCCESS) {
					IOT_INFO(" Set STA mode Fail");
					return IOT_ERROR_CONN_OPERATE_FAIL;
				}
			}

			IOT_INFO("Start STA mode");

			/* Connect to AP */
			memset(&apconfig, 0 , sizeof(wifi_manager_ap_config_s));
			apconfig.ssid_length = strlen(conf->ssid);
			strncpy(apconfig.ssid, conf->ssid, apconfig.ssid_length);
			apconfig.ssid[apconfig.ssid_length] = '\0';
			apconfig.ap_auth_type = conf->authmode;
			if (conf->authmode != WIFI_MANAGER_AUTH_OPEN) {
				apconfig.passphrase_length = strlen(conf->pass);
				strncpy(apconfig.passphrase, conf->pass, apconfig.passphrase_length);
				apconfig.passphrase[apconfig.passphrase_length] = '\0';
				apconfig.ap_crypto_type = WIFI_MANAGER_CRYPTO_AES;
			} else {
				apconfig.passphrase[0] = '\0';
				apconfig.passphrase_length = 0;
				apconfig.ap_crypto_type = WIFI_MANAGER_CRYPTO_NONE;
			}

			IOT_DEBUG("connect to ap SSID:%s password:%s, auth:%d", apconfig.ssid, apconfig.passphrase, apconfig.ap_auth_type);

			res = wifi_manager_connect_ap(&apconfig);
			if (res != WIFI_MANAGER_SUCCESS) {
				IOT_INFO(" AP connect failed");
				return IOT_ERROR_CONN_CONNECT_FAIL;
			}

			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 15;

			while (!g_connection_done) {
				int w = sem_timedwait(&g_connection_sem, &ts);
				if (w == 0) break;
				if (errno == EINTR) continue;
				if (errno == ETIMEDOUT) {
					IOT_ERROR("WiFi connection timeout");
					return IOT_ERROR_CONN_OPERATE_FAIL;
				}
				IOT_ERROR("sem_timedwait error (%d)", errno);
				return IOT_ERROR_CONN_OPERATE_FAIL;
			}

			IOT_INFO("Time is not set yet. Connecting to WiFi and getting time over NTP.");
			_obtain_time();
		break;
		case IOT_WIFI_MODE_SOFTAP:
			memset(&sap_config, 0, sizeof(wifi_manager_softap_config_s));
			str_len = strlen(conf->ssid);
			memcpy(sap_config.ssid, conf->ssid, (str_len > WIFIMGR_SSID_LEN) ? WIFIMGR_SSID_LEN : str_len);

			str_len =  strlen(conf->pass);
			memcpy(sap_config.passphrase, conf->pass, (str_len > WIFIMGR_PASSPHRASE_LEN) ? WIFIMGR_PASSPHRASE_LEN : str_len);
			sap_config.channel = IOT_SOFT_AP_CHANNEL;

			res = wifi_manager_set_mode(SOFTAP_MODE, &sap_config);
			if (res != WIFI_MANAGER_SUCCESS) {
				IOT_INFO(" Run SoftAP Fail");
			}

			IOT_DEBUG("wifi_init_softap finished.SSID:%s password:%s",
						sap_config.ssid, sap_config.passphrase);
		break;
		default:
			IOT_ERROR("iot bsp wifi can't support this mode = %d", conf->mode);
			return IOT_ERROR_CONN_OPERATE_FAIL;
	}

	return IOT_ERROR_NONE;
}

uint16_t iot_bsp_wifi_get_scan_result(iot_wifi_scan_result_t *scan_result)
{
	uint16_t ap_num;
	uint16_t i;

	/*need to initialize the scan buffer before updating*/
	memset(scan_result, 0x0, (IOT_WIFI_MAX_SCAN_RESULT * sizeof(iot_wifi_scan_result_t)));

	ap_num = wifi_manager_scan_result.ap_num;

	for(i = 0; i < ap_num; i++)	{
		iot_wifi_auth_mode_t conv_auth_mode;
		size_t max = IOT_WIFI_MAX_SSID_LEN; // 실제 SSID 최대 길이(널 제외)
		const char *src = (const char *)wifi_manager_scan_result.wifi_scan_result[i].ssid;

		// 원본이 char[]이지만 안전하게 길이 제한
		size_t len = strnlen(src, max);

		memcpy(scan_result[i].ssid, src, len);
		scan_result[i].ssid[len] = '\0'; // uint8_t에도 0 넣으면 동일

		unsigned int b[6];
		if (sscanf(wifi_manager_scan_result.wifi_scan_result[i].bssid,
				"%2x:%2x:%2x:%2x:%2x:%2x",
				&b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
			for (int k = 0; k < 6; k++) {
				scan_result[i].bssid[k] = (uint8_t)b[k];
			}
		} else {
			memset(scan_result[i].bssid, 0, 6);
}
		// sscanf(wifi_manager_scan_result.wifi_scan_result[i].bssid, "%x:%x:%x:%x:%x:%x",
		// 	&scan_result[i].bssid[0], &scan_result[i].bssid[1], &scan_result[i].bssid[2],
		// 	&scan_result[i].bssid[3], &scan_result[i].bssid[4], &scan_result[i].bssid[5]);

		switch (wifi_manager_scan_result.wifi_scan_result[i].ap_auth_type) {
			case WIFI_MANAGER_AUTH_UNKNOWN:
				conv_auth_mode = IOT_WIFI_AUTH_UNKNOWN;
				break;
			case WIFI_MANAGER_AUTH_WPA_AND_WPA2_PSK:
				conv_auth_mode = IOT_WIFI_AUTH_WPA_WPA2_PSK;
				break;
			case WIFI_MANAGER_AUTH_WPA2_AND_WPA3_PSK:
			case WIFI_MANAGER_AUTH_WPA3_PSK:
				conv_auth_mode = IOT_WIFI_AUTH_WPA3_PERSONAL;
				break;
			default:
				conv_auth_mode = wifi_manager_scan_result.wifi_scan_result[i].ap_auth_type;
				break;
		}
		scan_result[i].rssi = wifi_manager_scan_result.wifi_scan_result[i].rssi;
		scan_result[i].freq = iot_util_convert_channel_freq(wifi_manager_scan_result.wifi_scan_result[i].channel);
		scan_result[i].authmode = conv_auth_mode;
	}

	return ap_num;
}

iot_error_t iot_bsp_wifi_get_mac(struct iot_mac *wifi_mac)
{
	int ret = -1;
	if(*((int *)bsp_mac.addr) == 0) {
#ifdef CONFIG_ARCH_BOARD_ESP32_FAMILY
		wifi_utils_info_s info;
		wifi_utils_result_e wret = wifi_utils_get_info(&info);
		ret = (wret == WIFI_UTILS_SUCCESS) ? 0 : -1;
#else
		wifi_manager_info_s info;
		wifi_manager_result_e res = wifi_manager_get_info(&info);
		ret = (res == WIFI_MANAGER_SUCCESS) ? 0 : -1;
#endif
		if (ret != 0) {
			IOT_INFO("Get info failed");
			return IOT_ERROR_CONN_OPERATE_FAIL;
		}

		strncpy((char *)wifi_mac->addr, (const char *)info.bssid, 6);
		bsp_mac = *wifi_mac;
	} else {
		*wifi_mac = bsp_mac;
	}

	return IOT_ERROR_NONE;
}

iot_wifi_freq_t iot_bsp_wifi_get_freq(void)
{
	return IOT_WIFI_FREQ_2_4G_ONLY;
}

iot_error_t iot_bsp_wifi_register_event_cb(iot_bsp_wifi_event_cb_t cb)
{
	return IOT_ERROR_BAD_REQ;
}

void iot_bsp_wifi_clear_event_cb(void)
{
}

iot_wifi_auth_mode_bits_t iot_bsp_wifi_get_auth_mode(void)
{
	iot_wifi_auth_mode_bits_t supported_mode_bits = IOT_WIFI_AUTH_MODE_BIT_ALL;
	supported_mode_bits ^= IOT_WIFI_AUTH_MODE_BIT(IOT_WIFI_AUTH_WPA2_ENTERPRISE);
	supported_mode_bits ^= IOT_WIFI_AUTH_MODE_BIT(IOT_WIFI_AUTH_WPA3_PERSONAL);

	return supported_mode_bits;
}

bool iot_bsp_wifi_is_dhcp_success()
{
	/* TODO */
	return g_connection_done ? true : false;
}