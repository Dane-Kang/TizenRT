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

iot_error_t set_st_device_key(void)
{
    int ret = IOT_ERROR_NONE;
    // "privateKey": "jHqh8Xqx3wXxJSd3PjzfNnJp+qEHxw4GirVuKfC/1iA=",
	// "publicKey": "SRVNDuXGgXLrbOh1izRkXMmKbi6bmpzsjDh19X8UnOY=",
#if SWAP_KEY
    uint8_t ed25519_privkey[] = {
        0x20, 0xd6, 0xbf, 0xf0, 0x29, 0x6e, 0xb5, 0x8a,
        0x06, 0x0e, 0xc7, 0x07, 0xa1, 0xfa, 0x69, 0x72,
        0x36, 0xdf, 0x3c, 0x3e, 0x77, 0x27, 0x25, 0xf1,
        0x05, 0xdf, 0xb1, 0x7a, 0xf1, 0xa1, 0x7a, 0x8c};
    uint8_t ed25519_pubkey[] = {
        0xe6, 0x9c, 0x14, 0x7f, 0xf5, 0x75, 0x38, 0x8c,
        0xec, 0x9c, 0x9a, 0x9b, 0x2e, 0x6e, 0x8a, 0xc9,
        0x5c, 0x64, 0x34, 0x8b, 0x75, 0xe8, 0x6c, 0xeb,
        0x72, 0x81, 0xc6, 0xe5, 0x0e, 0x4d, 0x15, 0x49
    };
#else
    uint8_t ed25519_privkey[] = {
        0x8c, 0x7a, 0xa1, 0xf1, 0x7a, 0xb1, 0xdf, 0x05,
        0xf1, 0x25, 0x27, 0x77, 0x3e, 0x3c, 0xdf, 0x36,
        0x72, 0x69, 0xfa, 0xa1, 0x07, 0xc7, 0x0e, 0x06,
        0x8a, 0xb5, 0x6e, 0x29, 0xf0, 0xbf, 0xd6, 0x20};
    uint8_t ed25519_pubkey[] = {
        0x49, 0x15, 0x4d, 0x0e, 0xe5, 0xc6, 0x81, 0x72,
        0xeb, 0x6c, 0xe8, 0x75, 0x8b, 0x34, 0x64, 0x5c,
        0xc9, 0x8a, 0x6e, 0x2e, 0x9b, 0x9a, 0x9c, 0xec,
        0x8c, 0x38, 0x75, 0xf5, 0x7f, 0x14, 0x9c, 0xe6};
#endif
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

iot_error_t tizenrt_helper_pk_sign_ed25519(iot_security_pk_params_t *pk_params, iot_security_buffer_t *input_buf, iot_security_buffer_t *sig_buf)
{
	if (ensure_seclink() < 0) {
        return IOT_ERROR_SECURITY_PK_SIGN;
    }

	if (!pk_params->pubkey.p || (pk_params->pubkey.len != ED25519_PUB_LEN)) {
		IOT_ERROR("pubkey is invalid with %d@%p", (int)pk_params->pubkey.len, pk_params->pubkey.p);
		return IOT_ERROR_SECURITY_PK_INVALID_PUBKEY;
	}

	if (!pk_params->seckey.p || (pk_params->seckey.len != ED25519_PRIV_LEN)) {
		IOT_ERROR("seckey is invalid with %d@%p", (int)pk_params->seckey.len, pk_params->seckey.p);
		return IOT_ERROR_SECURITY_PK_INVALID_SECKEY;
	}
	
	hal_data pub_key  = { pk_params->pubkey.p, pk_params->pubkey.len, NULL, 0 };
    hal_data priv_key = { pk_params->seckey.p, pk_params->seckey.len, NULL, 0 };

	IOT_DEBUG("input:  %3d@%p", (int)input_buf->len, input_buf->p);
	IOT_DEBUG("seckey: %3d@%p", (int)pk_params->seckey.len, pk_params->seckey.p);
	IOT_DEBUG("pubkey: %3d@%p", (int)pk_params->pubkey.len, pk_params->pubkey.p);

	// 키를 slot에 로드
    if (sl_set_key(g_st_hnd, HAL_KEY_ED_25519, RW_SLOT_ENTRY, &pub_key, &priv_key) != SECLINK_OK) {
        IOT_ERROR("sl_set_key failed");
        return IOT_ERROR_SECURITY_PK_SIGN;
    }

	sig_buf->len = ED25519_SIG_LEN;
	sig_buf->p = (unsigned char *)iot_os_malloc(sig_buf->len);
	if (!sig_buf->p) {
		IOT_ERROR("malloc sig failed");
        sl_remove_key(g_st_hnd, HAL_KEY_ED_25519, RW_SLOT_ENTRY);
		return IOT_ERROR_SECURITY_MEM_ALLOC;
	}

	hal_data in  = { input_buf->p, input_buf->len, NULL, 0 };
    hal_data sig = { sig_buf->p, sig_buf->len, NULL, 0 };

	hal_ecdsa_mode mode;
    memset(&mode, 0, sizeof(mode));
    mode.curve  = HAL_ECDSA_CURVE_25519;
    mode.hash_t = HAL_HASH_SHA512;

	int ret = sl_ecdsa_sign_md(g_st_hnd, mode, &in, RW_SLOT_ENTRY, &sig);

    // 키 제거 (slot 고정 사용 시 충돌 방지)
    sl_remove_key(g_st_hnd, HAL_KEY_ED_25519, RW_SLOT_ENTRY);

    if (ret != SECLINK_OK) {
        IOT_ERROR("sl_ecdsa_sign_md failed: %d", ret);
        iot_security_buffer_free(sig_buf);
        return IOT_ERROR_SECURITY_PK_SIGN;
    }

    // seclink가 실제 길이를 갱신하는 경우를 대비
    if (sig.data_len != 0 && sig.data_len != sig_buf->len) {
        // Ed25519면 보통 64여야 정상. 다르면 플랫폼 반환 포맷 확인 필요.
        IOT_ERROR("signature length mismatch: %d", (int)sig.data_len);
        iot_security_buffer_free(sig_buf);
        return IOT_ERROR_SECURITY_PK_KEY_LEN;
    }

	IOT_DEBUG("sig:    %3d@%p", (int)sig_buf->len, sig_buf->p);
    sl_deinit(&g_st_hnd);
    g_seclink_inited = 0;
	return IOT_ERROR_NONE;
}

iot_error_t tizenrt_helper_pk_verify_ed25519(iot_security_pk_params_t *pk_params, iot_security_buffer_t *input_buf, iot_security_buffer_t *sig_buf)
{
	if (ensure_seclink() < 0) {
        return IOT_ERROR_SECURITY_PK_VERIFY;
    }

    if (!sig_buf->p || sig_buf->len != ED25519_SIG_LEN) {
        IOT_ERROR("sig invalid: %d", (int)sig_buf->len);
        return IOT_ERROR_SECURITY_PK_KEY_LEN;
    }

    if (!pk_params->pubkey.p || (pk_params->pubkey.len != ED25519_PUB_LEN)) {
		IOT_ERROR("pubkey is invalid with %d@%p", (int)pk_params->pubkey.len, pk_params->pubkey.p);
		return IOT_ERROR_SECURITY_PK_INVALID_PUBKEY;
	}

	if (!pk_params->seckey.p || (pk_params->seckey.len != ED25519_PRIV_LEN)) {
		IOT_ERROR("seckey is invalid with %d@%p", (int)pk_params->seckey.len, pk_params->seckey.p);
		return IOT_ERROR_SECURITY_PK_INVALID_SECKEY;
	}
	
	hal_data pub_key  = { pk_params->pubkey.p, pk_params->pubkey.len, NULL, 0 };
    hal_data priv_key = { pk_params->seckey.p, pk_params->seckey.len, NULL, 0 };

    // verify는 priv 없어도 되지만, API가 set_key에 priv를 요구하면 NULL로 가능 여부 확인 필요
    if (sl_set_key(g_st_hnd, HAL_KEY_ED_25519, RW_SLOT_ENTRY, &pub_key, NULL) != SECLINK_OK) {
        // 어떤 seclink 구현은 pub/priv 둘 다 요구할 수 있음.
        // 그 경우 pk_params->seckey가 있다면 priv도 같이 넣어주세요.
        IOT_ERROR("sl_set_key(pub only) failed");
        return IOT_ERROR_SECURITY_PK_VERIFY;
    }

    hal_data in  = { input_buf->p, input_buf->len, NULL, 0 };
    hal_data sig = { sig_buf->p, sig_buf->len, NULL, 0 };

    hal_ecdsa_mode mode;
    memset(&mode, 0, sizeof(mode));
    mode.curve  = HAL_ECDSA_CURVE_25519;
    mode.hash_t = HAL_HASH_SHA512;

    int ret = sl_ecdsa_verify_md(g_st_hnd, mode, &in, &sig, RW_SLOT_ENTRY);

    sl_remove_key(g_st_hnd, HAL_KEY_ED_25519, RW_SLOT_ENTRY);

    if (ret != SECLINK_OK) {
        IOT_ERROR("sl_ecdsa_verify_md failed: %d", ret);
        return IOT_ERROR_SECURITY_PK_VERIFY;
    }

	IOT_DEBUG("sign verify success");
    sl_deinit(&g_st_hnd);
    g_seclink_inited = 0;
	return IOT_ERROR_NONE;
}

iot_error_t tizenrt_helper_ecdh_compute_shared_x25519(mbedtls_ecp_group *grp, mbedtls_mpi *z, const mbedtls_ecp_point *Q, const mbedtls_mpi *d, int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
    iot_error_t err = IOT_ERROR_SECURITY_ECDH_LIBRARY;
    int ret;
    hal_ecdh_data ecc_pub = {0,};
	unsigned char shared_secret_data[256];
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

	if (!(ecc_pub.pubkey_y->data = (unsigned char *)malloc(ecc_pub.pubkey_y->data_len))) {
		ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
		goto cleanup_with_mem;
	}


	ret = mbedtls_mpi_write_binary(&Q->MBEDTLS_PRIVATE(X), ecc_pub.pubkey_x->data, ecc_pub.pubkey_x->data_len);
    if (ret) {
		IOT_ERROR("mbedtls_mpi_write_binary = -0x%04X", -ret);
		err = IOT_ERROR_SECURITY_ECDH_LIBRARY;
		goto cleanup_with_mem;
	}

    sl_print_buffer(ecc_pub.pubkey_x->data, ecc_pub.pubkey_x->data_len, "ecc_pub.pubkey_x key");

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
    
    sl_print_buffer(shared_secret.data, shared_secret.data_len, "result shared key");

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