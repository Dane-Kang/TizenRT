/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/****************************************************************************
*
* This demo showcases BLE GATT server. It can send adv data, be connected by client.
* Run the gatt_client demo, the client demo will automatically connect to the gatt_server demo.
* Client demo will enable gatt_server's notify after connection. The two devices will then exchange
* data.
*
****************************************************************************/
#include <tinyara/config.h>
#include <tinyara/clock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <ble_manager/ble_manager.h>
#include <semaphore.h>
#include <errno.h>

#include "iot_debug.h"
#include "iot_os_util.h"
#include "iot_util.h"
#include "iot_bsp_ble.h"

/* Advertising Define */
#define PACKET_MAX_SIZE     31
#define ADV_FLAG_LEN            0x02
#define ADV_FLAG_TYPE           0x01
#define ADV_FLAG_VALUE          0x04
#define ADV_MANUFACTURER_DATA_TYPE        0xFF
#define ADV_LOCAL_NAME_TYPE         0x09

#define BLE_STATE_MANAGER_RMC_HANDLE_OTA_COMMAND (0xff01)

#define ADV_CONFIG_FLAG         (1 << 0)
#define SCAN_RSP_CONFIG_FLAG    (1 << 1)

#define GATTS_MTU_MAX    517

static iot_ble_cbs_t *g_ble_cbs;
static uint32_t g_mtu = 0;

static void utc_cb_desc_b_1(ble_server_attr_cb_type_e type, ble_conn_handle conn_handle, ble_attr_handle attr_handle, void *arg)
{
	char *arg_str = "None";
	if (arg != NULL) {
		arg_str = (char *)arg;
	}
	IOT_DEBUG("[DESC_A_1][%s] type : %d / handle : %d / attr : %02x \n", arg_str, type, conn_handle, attr_handle);
}

static void ble_peri_cb_charact_ota(ble_server_attr_cb_type_e type, ble_conn_handle conn_handle, ble_attr_handle attr_handle, void* arg) {
	char *arg_str = "None";
	if (arg != NULL) {
		arg_str = (char *)arg;
	}
	IOT_DEBUG("[CHAR_OTA][%s] type : %d / handle : %d / attr : %02x \n", arg_str, type, conn_handle, attr_handle);
}

static ble_server_gatt_t gatt_profile[] = {
	{
		.type = BLE_SERVER_GATT_SERVICE,
		.uuid = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01},
		.uuid_length = 16,
		.attr_handle = 0x006a,
	},

	{
		.type = BLE_SERVER_GATT_CHARACT,
		.uuid = {0x09, 0x0E, 0xE6, 0x80, 0x02, 0x30, 0xC7, 0xA4, 0x8E, 0x4F, 0x2D, 0xAE, 0x0E, 0x0F, 0x94, 0xBE},
		.uuid_length = 16,
		.property =  BLE_ATTR_PROP_READ|BLE_ATTR_PROP_WRITE|BLE_ATTR_PROP_INDICATE,
		.permission = BLE_ATTR_PERM_R_PERMIT|BLE_ATTR_PERM_W_PERMIT,
		.attr_handle = BLE_STATE_MANAGER_RMC_HANDLE_OTA_COMMAND, 
		.cb = ble_peri_cb_charact_ota,
		.arg = "char_4"
	},

	{
		.type = BLE_SERVER_GATT_DESC, 
		.uuid = {0x02, 0x29}, 
		.uuid_length = 2, 
		.permission = BLE_ATTR_PERM_R_PERMIT | BLE_ATTR_PERM_W_PERMIT, 
		.attr_handle = 0x006c, 
		.cb = utc_cb_desc_b_1, 
		.arg = "desc_b_1",
	},
};

static void ble_server_connected_cb(ble_conn_handle con_handle, ble_server_connection_type_e conn_type, uint8_t mac[BLE_BD_ADDR_MAX_LEN], uint8_t adv_handle)
{
	IOT_DEBUG("'%s' is called\n", __FUNCTION__);
	IOT_DEBUG("conn : %d / conn_type : %d\n", con_handle, conn_type);
	IOT_DEBUG("conn mac : %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	IOT_DEBUG("adv_handle : %d\n", adv_handle);
	if (conn_type == BLE_SERVER_DISCONNECTED) {
		restart_server();
	}
	adv_handle = 0xff;
	return;
}

static void ble_server_disconnected_cb(ble_conn_handle con_handle, uint16_t cause)
{
	IOT_DEBUG("'%s' is called", __FUNCTION__);
	IOT_DEBUG("conn : %d", con_handle);
	IOT_DEBUG("cause : %d", cause);
	return;
}

static void ble_server_mtu_update_cb(ble_conn_handle con_handle, uint16_t mtu_size)
{
	IOT_DEBUG("'%s' is called", __FUNCTION__);
	IOT_DEBUG("conn : %d", con_handle);
	IOT_DEBUG("mtu_size : %d", mtu_size);
	g_mtu = mtu_size;
	return;
}

static void ble_server_passkey_display_cb(uint32_t passkey, ble_conn_handle con_handle)
{
	IOT_DEBUG("'%s' is called", __FUNCTION__);
	IOT_DEBUG("passkey %ld, con_handle : %d", passkey, con_handle);
	return;
}

static void ble_server_pair_bond_cb(uint32_t bond_pair, ble_conn_handle con_handle)
{
	IOT_DEBUG("'%s' is called", __FUNCTION__);
	IOT_DEBUG("bond_pair 0x%x, con_handle : %d", bond_pair, con_handle);
	return;
}

static ble_server_init_config server_config = {
	ble_server_connected_cb,
	ble_server_disconnected_cb,
	ble_server_mtu_update_cb,
	ble_server_passkey_display_cb,
	ble_server_pair_bond_cb,
	true,
	gatt_profile, sizeof(gatt_profile) / sizeof(ble_server_gatt_t)
};

iot_error_t iot_bsp_ble_init(iot_ble_cbs_t *ble_cbs)
{
	ble_result_e ret;
	ret = ble_manager_init(&server_config);
	if (ret != BLE_MANAGER_SUCCESS) {
		if (ret != BLE_MANAGER_ALREADY_WORKING) {
			IOT_ERROR("init fail[%d]", ret);
			return IOT_ERROR_INIT_FAIL;
		}
		IOT_ERROR("init is already done");
	} else {
		IOT_ERROR("init with config done[%d]", ret);
	}

	g_ble_cbs = ble_cbs;

	IOT_DEBUG("done");

	return IOT_ERROR_NONE;
}

void iot_bsp_ble_deinit(void)
{
	ble_result_e ret;
	ret = ble_manager_deinit();
	IOT_DEBUG("deinit done[%d]\n", ret);
}


static void build_adv_and_scan_rsp_raw(uint16_t mn_code,
                                       const uint8_t *mn_data,
                                       size_t mn_data_len,
                                       const char *local_name,
                                       uint8_t *adv_data,
                                       uint16_t *adv_data_len,
                                       uint8_t *scan_rsp,
                                       uint16_t *scan_rsp_len)
{
    size_t offset_adv = 0;
    size_t offset_scan = 0;
    size_t mn_in_adv = 0;
    size_t mn_in_scan = 0;

    /* 1) Flags (ESP32 코드와 동일) */
    adv_data[offset_adv++] = ADV_FLAG_LEN;   /* length (type+value) */
    adv_data[offset_adv++] = ADV_FLAG_TYPE;
    adv_data[offset_adv++] = ADV_FLAG_VALUE;

    /* 2) Manufacturer data split (ESP32 코드와 동일 컨셉)
       Manufacturer AD 구조:
       Len(1) + Type(1) + CompanyID(2) + Data(N)
       여기서 Len 필드 값 = (1 + 2 + N) = N + 3
       전체 점유 바이트 = (Len필드 1) + (N+3) = N+4
    */
    if ((1 + ADV_FLAG_LEN) + (1 + 1 + 2 + mn_data_len) > PACKET_MAX_SIZE) {
        /* adv에서 mfg data로 쓸 수 있는 최대 N = 31 - flags(3) - overhead(4) */
        size_t max_mn_in_adv = 0;
        if (PACKET_MAX_SIZE > (3 + 4)) {
            max_mn_in_adv = PACKET_MAX_SIZE - 3 - 4;
        }
        mn_in_adv = (mn_data_len > max_mn_in_adv) ? max_mn_in_adv : mn_data_len;
        mn_in_scan = mn_data_len - mn_in_adv;
    } else {
        mn_in_adv = mn_data_len;
        mn_in_scan = 0;
    }

    /* 3) Manufacturer in ADV */
    adv_data[offset_adv++] = (uint8_t)(mn_in_adv + 3);
    adv_data[offset_adv++] = ADV_MANUFACTURER_DATA_TYPE;
    adv_data[offset_adv++] = (uint8_t)(mn_code & 0xFF);
    adv_data[offset_adv++] = (uint8_t)((mn_code >> 8) & 0xFF);
    if (mn_in_adv > 0) {
        memcpy(&adv_data[offset_adv], mn_data, mn_in_adv);
        offset_adv += mn_in_adv;
    }
    *adv_data_len = (uint16_t)offset_adv;

    /* 4) Local name in Scan Response */
    {
        size_t name_len = strlen(local_name);
        /* Len(1) + Type(1) + Name(name_len) <= 31 */
        if ((2 + name_len) <= PACKET_MAX_SIZE) {
            scan_rsp[offset_scan++] = (uint8_t)(name_len + 1);
            scan_rsp[offset_scan++] = ADV_LOCAL_NAME_TYPE;
            memcpy(&scan_rsp[offset_scan], local_name, name_len);
            offset_scan += name_len;
        } else {
            /* 너무 길면 잘라서 넣음 */
            size_t max_name_len = (PACKET_MAX_SIZE >= 2) ? (PACKET_MAX_SIZE - 2) : 0;
            scan_rsp[offset_scan++] = (uint8_t)(max_name_len + 1);
            scan_rsp[offset_scan++] = ADV_LOCAL_NAME_TYPE;
            memcpy(&scan_rsp[offset_scan], local_name, max_name_len);
            offset_scan += max_name_len;
        }
    }

    /* 5) Remaining manufacturer in Scan Response (if any & if space) */
    if (mn_in_scan > 0) {
        size_t remain = (offset_scan < PACKET_MAX_SIZE) ? (PACKET_MAX_SIZE - offset_scan) : 0;
        if (remain > 4) {
            size_t max_mn_in_scan = remain - 4;
            size_t put = (mn_in_scan > max_mn_in_scan) ? max_mn_in_scan : mn_in_scan;

            scan_rsp[offset_scan++] = (uint8_t)(put + 3);
            scan_rsp[offset_scan++] = ADV_MANUFACTURER_DATA_TYPE;
            scan_rsp[offset_scan++] = (uint8_t)(mn_code & 0xFF);
            scan_rsp[offset_scan++] = (uint8_t)((mn_code >> 8) & 0xFF);
            memcpy(&scan_rsp[offset_scan], mn_data + mn_in_adv, put);
            offset_scan += put;
        }
        /* 공간 부족하면 나머지는 버림(legacy 31바이트 한계) */
    }

    *scan_rsp_len = (uint16_t)offset_scan;
}

int iot_bsp_ble_start_adv(uint16_t mn_code, uint8_t *mn_data, size_t mn_data_len, char *local_name)
{
    /* ble_server_set_adv_data/resp가 내부에서 복사하는지 보장 못 하면
       stack buffer는 위험할 수 있어서 static으로 둠 (안전빵) */
    static uint8_t adv_raw[PACKET_MAX_SIZE];
    static uint8_t scan_raw[PACKET_MAX_SIZE];
    static ble_data adv_data;
    static ble_data scan_rsp;

    uint16_t adv_len = 0;
    uint16_t scan_len = 0;

    if (!mn_data || !local_name) {
        return IOT_ERROR_INVALID_ARGS;
    }

    memset(adv_raw, 0, sizeof(adv_raw));
    memset(scan_raw, 0, sizeof(scan_raw));

    /* 1) raw payload 구성 (ESP32와 동일 컨셉) */
    build_adv_and_scan_rsp_raw(mn_code, mn_data, mn_data_len, local_name,
                               adv_raw, &adv_len,
                               scan_raw, &scan_len);

    adv_data.data = adv_raw;
    adv_data.length = adv_len;

    scan_rsp.data = scan_raw;
    scan_rsp.length = scan_len;

    /* 2) (선택) GAP device name 설정
          - scan_rsp에 이름을 직접 넣었더라도, OS에서 name을 별도로 쓰는 경우가 있어서 맞춰두는 게 안전 */
    {
        char name_buf[BLE_GAP_DEVICE_NAME_LEN] = {0}; /* :contentReference[oaicite:4]{index=4} */
        strncpy(name_buf, local_name, BLE_GAP_DEVICE_NAME_LEN - 1);
        (void)ble_manager_set_gap_device_name(name_buf); /* 실패해도 adv 자체는 가능할 수 있어 무시 가능 :contentReference[oaicite:5]{index=5} */
    }

    /* 3) 광고/스캔응답 raw data 설정 */
    if (ble_server_set_adv_data(&adv_data) != BLE_MANAGER_SUCCESS) {  /* :contentReference[oaicite:6]{index=6} */
        return IOT_ERROR_BAD_REQ;
    }
    if (ble_server_set_adv_resp(&scan_rsp) != BLE_MANAGER_SUCCESS) {  /* :contentReference[oaicite:7]{index=7} */
        return IOT_ERROR_BAD_REQ;
    }

    /* 4) adv type / interval 설정 후 start */
    if (ble_server_set_adv_type(BLE_ADV_TYPE_IND, NULL) != BLE_MANAGER_SUCCESS) { /* connectable undirected :contentReference[oaicite:8]{index=8} */
        return IOT_ERROR_BAD_REQ;
    }

    /* interval 단위는 보드/드라이버 구현에 따라 다를 수 있음(주석상 0.625ms step)
       기존 프로젝트에서 쓰던 값 있으면 그 값으로 맞추는 게 제일 안전 */
    if (ble_server_set_adv_interval(0xA0) != BLE_MANAGER_SUCCESS) { /* 예: 100ms(가정) :contentReference[oaicite:9]{index=9} */
        return IOT_ERROR_BAD_REQ;
    }

    /* 필요하면 TX power도 설정 가능 */
    /* ble_server_set_adv_tx_power(0x1A); */ /* :contentReference[oaicite:10]{index=10} */

    if (ble_server_start_adv() != BLE_MANAGER_SUCCESS) {  /* :contentReference[oaicite:11]{index=11} */
        return IOT_ERROR_BAD_REQ;
    }

    return IOT_ERROR_NONE;
}

int iot_send_indication(uint8_t *buf, uint32_t len)
{
	ble_conn_handle conn_handle = 0;
    ble_attr_handle value_handle = BLE_STATE_MANAGER_RMC_HANDLE_OTA_COMMAND + 1;
    if (value_handle == 0x0000) {
        return 1;
    }

    ble_data packet;
    packet.data = buf;
    packet.length = (uint16_t)len;

    ble_result_e ret = ble_server_charact_indicate(value_handle, conn_handle, &packet);
    if (ret != BLE_MANAGER_SUCCESS) {
        return 1;
    }
}

uint32_t iot_bsp_ble_get_mtu(void)
{
	return g_mtu;
}

int iot_bsp_ble_get_mac_address(uint8_t mac_address[6])
{
    ble_result_e ret;

	ret = ble_manager_get_mac_addr(mac_address);
	if (ret != BLE_MANAGER_SUCCESS) {
		IOT_ERROR("get mac fail[%d]\n", ret);
		return IOT_ERROR_BAD_REQ;
	}

    return IOT_ERROR_NONE;
}




