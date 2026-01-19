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
#include <tinyara/seclink.h>
#include <tinyara/seclink_drv.h>
#include <tinyara/security_hal.h>

#define ED25519_PUB_LEN   32
#define ED25519_PRIV_LEN  32
#define ED25519_SIG_LEN   64

#define RW_SLOT_ENTRY 34

static sl_ctx g_st_hnd;
static int g_seclink_inited = 0;

void sl_print_buffer(char *data, int buf_len, const char *message)
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
    if (sl_set_key(g_st_hnd, HAL_KEY_ECC_25519, RW_SLOT_ENTRY, &pub_key, &priv_key) != SECLINK_OK) {
        IOT_ERROR("sl_set_key failed");
        return IOT_ERROR_SECURITY_PK_SIGN;
    }

	sig_buf->len = ED25519_SIG_LEN;
	sig_buf->p = (unsigned char *)iot_os_malloc(sig_buf->len);
	if (!sig_buf->p) {
		IOT_ERROR("malloc sig failed");
        sl_remove_key(g_st_hnd, HAL_KEY_ECC_25519, RW_SLOT_ENTRY);
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
    sl_remove_key(g_st_hnd, HAL_KEY_ECC_25519, RW_SLOT_ENTRY);

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
    if (sl_set_key(g_st_hnd, HAL_KEY_ECC_25519, RW_SLOT_ENTRY, &pub_key, NULL) != SECLINK_OK) {
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

    sl_remove_key(g_st_hnd, HAL_KEY_ECC_25519, RW_SLOT_ENTRY);

    if (ret != SECLINK_OK) {
        IOT_ERROR("sl_ecdsa_verify_md failed: %d", ret);
        return IOT_ERROR_SECURITY_PK_VERIFY;
    }

	IOT_DEBUG("sign verify success");

	return IOT_ERROR_NONE;
}

#define X25519_KEY_LEN 32
#define X25519_SECRET_LEN 32
#define TMP_SLOT 32
#define SL_AUTH_MEM_SIZE 4096

int sl_malloc_buffer(hal_data *data, int buf_len)
{
	if (!data) {
		return -1;
	}
	data->data = (unsigned char *)zalloc(buf_len);
	if (!data->data) {
		return -2;
	}
	data->data_len = buf_len;

	return 0;
}

void sl_free_buffer(hal_data *data)
{
	if (data == NULL) {
		return;
	}

	if (data->data) {
		free(data->data);
		data->data = NULL;
	}
	if (data->priv) {
		free(data->priv);
		data->priv = NULL;
	}
	data->data_len = 0;
	data->priv_len = 0;
}

iot_error_t tizenrt_helper_ecdh_compute_shared_x25519(
        iot_security_buffer_t *t_seckey_buf,
        iot_security_buffer_t *c_pubkey_buf,
        iot_security_buffer_t *output_buf)
{
    iot_error_t err = IOT_ERROR_SECURITY_ECDH_LIBRARY;
    int ret;

    iot_security_buffer_t swap_priv = {0};
    iot_security_buffer_t swap_peer = {0};

    if (!t_seckey_buf || !c_pubkey_buf || !output_buf) {
        IOT_ERROR("parameters is invalid");
        return IOT_ERROR_SECURITY_INVALID_ARGS;
    }

    if (t_seckey_buf->len != X25519_KEY_LEN) {
        IOT_ERROR("seckey len invalid (%d)", (int)t_seckey_buf->len);
        return IOT_ERROR_SECURITY_ECDH_INVALID_SECKEY;
    }

    if (c_pubkey_buf->len != X25519_KEY_LEN) {
        IOT_ERROR("pubkey len invalid (%d)", (int)c_pubkey_buf->len);
        return IOT_ERROR_SECURITY_ECDH_INVALID_PUBKEY;
    }

    if (ensure_seclink() < 0) {
        IOT_ERROR("ensure_seclink failed: %d", ret);
        return IOT_ERROR_SECURITY_PK_SIGN;
    }

    // (옵션) 엔디안/포맷 swap이 필요한 환경이면 유지
    // 필요 없으면 아래 _tizenrt_swap_secret() 블록을 제거하고 원본 버퍼로 진행해도 됨.
    // err = _tizenrt_swap_secret(t_seckey_buf, &swap_priv);
    // if (err) goto exit;

    // err = _tizenrt_swap_secret(c_pubkey_buf, &swap_peer);
    // if (err) goto exit;
    sl_test_print_buffer(c_pubkey_buf->p, c_pubkey_buf->len, "key A pub");
	sl_test_print_buffer(t_seckey_buf->p, t_seckey_buf->len, "key A priv");
    // 1) 내 private key를 slot에 저장
    hal_data priv_key = {t_seckey_buf->p, t_seckey_buf->len, NULL, 0};
    //hal_data priv_key = {swap_priv.p, swap_priv.len, NULL, 0};

    // 1) 내 private key를 slot에 저장
    hal_data pub_key = {c_pubkey_buf->p, c_pubkey_buf->len, NULL, 0};
    //hal_data pub_key = {swap_peer.p, swap_peer.len, NULL, 0};

    // x25519는 pubkey를 굳이 넣을 필요 없는데, HAL이 요구하면 pub도 같이 넣어야 함.
    // 대부분은 priv만으로도 OK (peer pub는 hal_ecdh_data로 전달)
    ret = sl_set_key(g_st_hnd, HAL_KEY_ED_25519, RW_SLOT_ENTRY, &pub_key, &priv_key);
    if (ret != SECLINK_OK) {
        IOT_ERROR("sl_set_key failed: %d", ret);
        err = IOT_ERROR_SECURITY_ECDH_LIBRARY;
        goto exit;
    }

    // // 2) peer public key를 hal_ecdh_data로 구성
    // hal_data peer_x = {0};
    // peer_x.data     = swap_peer.p;     // 또는 c_pubkey_buf->p
    // peer_x.data_len = swap_peer.len;   // 32

    // // pubkey_y는 x25519에서 일반적으로 사용하지 않음
    // hal_data dummy_y = {0}; // NULL 허용 안 하는 HAL 대비용 (len=0)
    // dummy_y.data     = NULL;
    // dummy_y.data_len = 0;

    hal_ecdh_data ecdh;
    ecdh.pubkey_x = (hal_data *)zalloc(sizeof(hal_data));
    ecdh.pubkey_y = (hal_data *)zalloc(sizeof(hal_data));

    ecdh.curve    = HAL_ECDSA_CURVE_25519;
    ecdh.pubkey_x->data = c_pubkey_buf->p;      // peer pub
    ecdh.pubkey_x->data_len = c_pubkey_buf->len;
    ecdh.pubkey_y->data = t_seckey_buf->p;      // my priv
    ecdh.pubkey_y->data_len = t_seckey_buf->len;

    // 3) shared secret 출력 버퍼 준비
    // seclink 예제는 넉넉히 SL_TEST_AUTH_MEM_SIZE로 잡았지만,
    // x25519는 32바이트 고정이라 우선 32로 잡고, HAL이 더 필요하면 그에 맞춰 늘리면 됨.
    output_buf->len = X25519_SECRET_LEN;
    output_buf->p = (unsigned char *)iot_os_malloc(output_buf->len);
    if (!output_buf->p) {
        IOT_ERROR("malloc failed for shared secret");
        err = IOT_ERROR_SECURITY_MEM_ALLOC;
        goto remove_key;
    }

    hal_data shared = {NULL, 0, NULL, 0};
    sl_malloc_buffer(&shared, SL_AUTH_MEM_SIZE);
    // shared.data     = output_buf->p;
    // shared.data_len = output_buf->len;

    // 4) ECDH shared secret 계산
    ret = sl_ecdh_compute_shared_secret(g_st_hnd, &ecdh, RW_SLOT_ENTRY, &shared);
    if (ret != SECLINK_OK) {
        IOT_ERROR("sl_ecdh_compute_shared_secret failed: %d", ret);
        err = IOT_ERROR_SECURITY_ECDH_LIBRARY;
        goto remove_key;
    }
    IOT_INFO("shared.data: %d",(int)shared.data_len);

    // HAL이 실제 길이를 data_len에 갱신하는 구조면 반영
    if (shared.data_len != 0 && shared.data_len != output_buf->len) {
        // x25519는 보통 32여야 정상. 다르면 HAL 동작/포맷 확인 필요
        IOT_ERROR("shared secret length unexpected: %d", (int)shared.data_len);
        err = IOT_ERROR_SECURITY_ECDH_LIBRARY;
        goto remove_key;
    }

    // (옵션) output도 기존처럼 swap해서 내보내야 하는 프로토콜이면 적용
    // 기존 mbedTLS 코드는 output도 swap 했었음.
    // 여기서도 동일하게 swap 적용:
    // {
    //     iot_security_buffer_t swap_out = {0};
    //     err = _tizenrt_swap_secret(output_buf, &swap_out);
    //     if (err == IOT_ERROR_NONE) {
    //         iot_security_buffer_free(output_buf);
    //         output_buf->p   = swap_out.p;
    //         output_buf->len = swap_out.len;
    //     } else {
    //         // swap 실패하면 원본 output 유지 or 에러 처리 선택
    //         // err 유지하고 exit로 갈 수도 있음
    //     }
    // }

    err = IOT_ERROR_NONE;

remove_key:
    // 키 슬롯 정리 (고정 slot 쓰면 매번 지우는게 안전)
    sl_remove_key(g_st_hnd, HAL_KEY_ED_25519, RW_SLOT_ENTRY);
    free(ecdh.pubkey_x);
	free(ecdh.pubkey_y);
    sl_free_buffer(&shared);

exit:
    iot_security_buffer_free(&swap_priv);
    iot_security_buffer_free(&swap_peer);

    //sl_free_buffer(&get_key);
    if (err != IOT_ERROR_NONE) {
        iot_security_buffer_free(output_buf);
    }
    return err;
}