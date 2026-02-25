/* ***************************************************************************
 *
 * Copyright (c) 2020 Samsung Electronics All Rights Reserved.
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

#include "iot_debug.h"
#include "tizenrt_helper.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/cipher.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/pk.h"
#include "mbedtls/asn1.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/alt/common.h"

#include <tinyara/seclink.h>
#include <tinyara/seclink_drv.h>
#include <tinyara/security_hal.h>

#define ED25519_PUB_LEN   32
#define ED25519_PRIV_LEN  32
#define ED25519_SIG_LEN   64

#define RW_SLOT_ENTRY 34
#define ST_CONVERT_SLOT_ENTRY 33

static sl_ctx g_st_hnd;
static int g_seclink_inited = 0;

void tizenrt_print_buffer(char *data, int buf_len, const char *message)
{
	printf("================================\n");
	printf("%s length(%d)\n", message, buf_len);
	printf("--------------------------------\n");
	for (int i = 0; i < buf_len; i++) {
		if (i != 0 && i % 16 == 0) {
			printf("\n");
		}
		printf("%02x ", data[i]);
	}
	printf("\n");
	printf("================================\n");
}

static iot_error_t _tizenrt_swap_secret(iot_security_buffer_t *src, iot_security_buffer_t *dst)
{
	unsigned char *p;
	size_t len;
	int i;

	if (!src || !src->p || (src->len == 0) || !dst) {
		return IOT_ERROR_SECURITY_INVALID_ARGS;
	}

	len = src->len;
	p = (unsigned char *)iot_os_malloc(len);

	if (!p) {
		IOT_ERROR("failed to malloc for swap");
		return IOT_ERROR_SECURITY_MEM_ALLOC;
	}

	for (i = 0; i < len; i++) {
		p[(len - 1) - i] = src->p[i];
	}

	dst->p = p;
	dst->len = len;

	return IOT_ERROR_NONE;
}

static int ensure_seclink(void)
{
    if (g_seclink_inited) return 0;

    // 프로젝트에서 이미 다른 곳에서 sl_init/sl_open을 하고 있다면
    // 여기서 중복 init 하면 안 됩니다.
    // 그 경우엔 g_st_hnd 외부에서 주입받는 구조로 바꾸세요.

    if (sl_init(&g_st_hnd) != SECLINK_OK) {
        IOT_ERROR("sl_init failed");
        return -1;
    }
    g_seclink_inited = 1;
    return 0;
}

/*
 * Simple Base64 decoder (RFC 4648, standard base64)
 * - Supports '=' padding
 * - Does NOT support base64url ('-' '_' variants)
 * Returns decoded length, or -1 on error.
 */
static int tizenrt_base64_decode(const char *b64, uint8_t *out, size_t out_max)
{
    static const int8_t T[256] = {
        /* initialize all to -1 */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63, /* +,/ */
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1, /* 0-9, '=' -> -2 */
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        /* rest are -1 */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    size_t len = strlen(b64);
    size_t out_len = 0;

    int val = 0;
    int valb = -8;
    int padding_seen = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)b64[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue; // ignore whitespace

        int8_t d = T[c];
        if (d == -1) return -1; // invalid char

        if (d == -2) { // '=' padding
            padding_seen = 1;
            continue;
        }
        if (padding_seen) {
            // data after '=' is invalid in strict base64
            return -1;
        }

        val = (val << 6) + d;
        valb += 6;

        if (valb >= 0) {
            if (out_len >= out_max) return -1;
            out[out_len++] = (uint8_t)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }

    return (int)out_len;
}

iot_error_t set_st_device_key(void)
{
    int ret = IOT_ERROR_NONE;

    const char *priv_b64 = "jHqh8Xqx3wXxJSd3PjzfNnJp+qEHxw4GirVuKfC/1iA=";
    const char *pub_b64  = "SRVNDuXGgXLrbOh1izRkXMmKbi6bmpzsjDh19X8UnOY=";

    uint8_t ed25519_privkey[128] = {0};
    uint8_t ed25519_pubkey[128]  = {0};

    int priv_len = tizenrt_base64_decode(priv_b64, ed25519_privkey, sizeof(ed25519_privkey));
    int pub_len  = tizenrt_base64_decode(pub_b64,  ed25519_pubkey,  sizeof(ed25519_pubkey));

    if (priv_len < 0) {
        printf("tizenrt_base64_decode failed for priv_b64\n");
        return IOT_ERROR_SECURITY_PK_SIGN;
    }
    if (pub_len < 0) {
        printf("tizenrt_base64_decode failed for pub_b64\n");
        return IOT_ERROR_SECURITY_PK_SIGN;
    }
    tizenrt_print_buffer(ed25519_privkey, priv_len, "ed25519_privkey");
    tizenrt_print_buffer(ed25519_pubkey, pub_len, "ed25519_pubkey");

    hal_data priv_key = { 0 };
    hal_data pub_key = { 0 };
    priv_key.data = ed25519_privkey;
    priv_key.data_len = sizeof(ed25519_privkey);
    priv_key.priv = NULL;
    priv_key.priv_len = 0;
    pub_key.data = ed25519_pubkey;
    pub_key.data_len = sizeof(ed25519_pubkey);
    pub_key.priv = NULL;
    pub_key.priv_len = 0;

    if (ensure_seclink() < 0) {
        IOT_ERROR("ensure_seclink failed: %d", ret);
        return IOT_ERROR_SECURITY_PK_SIGN;
    }

    ret = sl_set_key(g_st_hnd, HAL_KEY_ED_25519, RW_SLOT_ENTRY, &pub_key, &priv_key);
    if (ret != SECLINK_OK) {
        IOT_ERROR("sl_set_key failed: %d", ret);
        return IOT_ERROR_SECURITY_ECDH_LIBRARY;
    }

    IOT_DEBUG("Done");

    return ret;
}

iot_error_t convert_ed25519_to_ecc25519(unsigned char* data, iot_security_buffer_t *output_buf)
{
    int ret = IOT_ERROR_NONE;
    hal_data priv_key = { NULL, 0, NULL, 0};
    hal_data pub_key = { NULL, 0, NULL, 0};
    unsigned char key_data[256];
	unsigned char key_priv[256];
	hal_data key = {key_data, sizeof(key_data), key_priv, sizeof(key_priv)};
    
    if (ensure_seclink() < 0) {
        IOT_ERROR("ensure_seclink failed: %d", ret);
        return IOT_ERROR_SECURITY_PK_SIGN;
    }

    pub_key.data = data;
    pub_key.data_len = 32;

    ret = sl_set_key(g_st_hnd, HAL_KEY_ED_25519, ST_CONVERT_SLOT_ENTRY, &pub_key, &priv_key);
    if (ret != SECLINK_OK) {
        IOT_ERROR("sl_set_key failed: %d", ret);
        return IOT_ERROR_SECURITY_ECDH_LIBRARY;
    }

    ret = sl_get_key(g_st_hnd, HAL_KEY_ECC_25519, ST_CONVERT_SLOT_ENTRY, &key);
    if (ret != SECLINK_OK) {
        IOT_ERROR("sl_get_key failed: %d", ret);
        return IOT_ERROR_SECURITY_ECDH_LIBRARY;
    }
    else {
        sl_remove_key(g_st_hnd, HAL_KEY_ED_25519, ST_CONVERT_SLOT_ENTRY);
    }
    
    unsigned char *p = (unsigned char *)iot_os_malloc(key.data_len);
    if (!p) {
        IOT_ERROR("iot_os_malloc failed: ");
        return IOT_ERROR_SECURITY_ECDH_LIBRARY;
    }
    memcpy(p, key.data, key.data_len);
    
    output_buf->p = p;
    output_buf->len = key.data_len;

    tizenrt_print_buffer(output_buf->p,output_buf->len,"convert pub");

    IOT_DEBUG("Done");

    return ret;
}

iot_error_t tizenrt_helper_ecdh_compute_shared_x25519(mbedtls_ecp_group *grp, mbedtls_mpi *z, const mbedtls_ecp_point *Q, const mbedtls_mpi *d, int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
    iot_error_t err = IOT_ERROR_SECURITY_ECDH_LIBRARY;
    int ret;
    hal_ecdh_data ecc_pub = {0,};
	unsigned char shared_secret_data[256] = {0};
	hal_data shared_secret = {shared_secret_data, 256, NULL, 0};
	hal_key_type key_type = HAL_KEY_ED_25519;


    if (ensure_seclink() < 0) {
        IOT_ERROR("ensure_seclink failed: %d", ret);
        return MBEDTLS_ERR_ECP_ALLOC_FAILED;
    }
    
    memset(&ecc_pub, 0, sizeof(hal_ecdh_data));

	ecc_pub.pubkey_x = (hal_data *)malloc(sizeof(hal_data));
	if (!ecc_pub.pubkey_x) {
		ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
		goto cleanup;
	}
	ecc_pub.pubkey_y = (hal_data *)malloc(sizeof(hal_data));
	if (!ecc_pub.pubkey_y) {
		free(ecc_pub.pubkey_x);
		ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
		goto cleanup;
	}

	ecc_pub.pubkey_x->data_len = mbedtls_mpi_size(&Q->MBEDTLS_PRIVATE(X));
	ecc_pub.pubkey_y->data_len = mbedtls_mpi_size(&Q->MBEDTLS_PRIVATE(Y));

	if (!(ecc_pub.pubkey_x->data = (unsigned char *)malloc(ecc_pub.pubkey_x->data_len))) {
		ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
		goto cleanup_with_mem;
	}

    if(ecc_pub.pubkey_y->data_len != 0){
        if (!(ecc_pub.pubkey_y->data = (unsigned char *)malloc(ecc_pub.pubkey_y->data_len))) {
            ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
            goto cleanup_with_mem;
        }
    }


	ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(X), ecc_pub.pubkey_x->data, ecc_pub.pubkey_x->data_len);
    if (ret) {
		IOT_ERROR("mbedtls_mpi_write_binary = -0x%04X", -ret);
		err = IOT_ERROR_SECURITY_ECDH_LIBRARY;
		goto cleanup_with_mem;
	}

    tizenrt_print_buffer(ecc_pub.pubkey_x->data, ecc_pub.pubkey_x->data_len, "ecc_pub.pubkey_x key");

	ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(Y), ecc_pub.pubkey_y->data, ecc_pub.pubkey_y->data_len);
    if (ret) {
		IOT_ERROR("mbedtls_mpi_write_binary = -0x%04X", -ret);
		err = IOT_ERROR_SECURITY_ECDH_LIBRARY;
		goto cleanup_with_mem;
	}

	if ((ecc_pub.curve = alt_get_curve(grp->id)) == HAL_ECDSA_UNKNOWN) {
		ret = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
		goto cleanup_with_mem;
	}

    IOT_DEBUG("grp->key_index %d", grp->key_index);
    IOT_DEBUG("ecc_pub.curve %d", ecc_pub.curve);

	ret = sl_ecdh_compute_shared_secret(g_st_hnd, &ecc_pub, grp->key_index, &shared_secret);
	if (ret != SECLINK_OK) {
		ret = MBEDTLS_ERR_ECP_HW_ACCEL_FAILED;
		goto cleanup_with_mem;
	}
    
    //tizenrt_print_buffer(shared_secret.data, shared_secret.data_len, "shared key");

	ret = mbedtls_mpi_read_binary(z, shared_secret.data, shared_secret.data_len);
    if (ret) {
		IOT_ERROR("mbedtls_mpi_read_binary = -0x%04X", -ret);
		err = IOT_ERROR_SECURITY_ECDH_LIBRARY;
		goto cleanup_with_mem;
	}
	ret = 0;

cleanup_with_mem:
	if (ecc_pub.pubkey_x) {
		if (ecc_pub.pubkey_x->data) {
			free(ecc_pub.pubkey_x->data);
		}
		free(ecc_pub.pubkey_x);
	}

	if (ecc_pub.pubkey_y) {
		if (ecc_pub.pubkey_y->data) {
			free(ecc_pub.pubkey_y->data);
		}
		free(ecc_pub.pubkey_y);
	}

cleanup:
	sl_remove_key(g_st_hnd, key_type, grp->key_index);

    sl_deinit(&g_st_hnd);
    g_seclink_inited = 0;
    return ret;
}