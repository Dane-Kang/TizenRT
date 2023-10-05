/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
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
/****************************************************************************
 * examples/hello/hello_main.c
 *
 *   Copyright (C) 2008, 2011-2012 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdio.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#define __DEBUG__
/****************************************************************************
 * hello_main
 ****************************************************************************/
#define T_MAX_DATA_SIZE  2048
//#define USE_TERMINAL
#define USE_DOCKLIGHT

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int hello_main(int argc, char *argv[])
#endif
{
	printf("Hello, World!!\n");
#ifdef USE_TERMINAL
    int fd = 0;
    char buf_tx[255], ch;
    int ret;

    for(int i = 0 ; i < 98 ; i++) {
        buf_tx[i] = i+1;
    }
    buf_tx[99] = '\r';
    buf_tx[100] = '\n';

    fd = open("/dev/ttyS2",O_RDWR | O_NOCTTY);
    while(1) {
        ret = write(fd, buf_tx, 100) ;        
        printf("write length = %d\n",ret);
        sleep(2);

        // if(ch = getchar())
        //     break;
    }

#else    
    struct termios tio;
    int fd = 0;
    int ret = -1;
    unsigned char *pBuf = NULL;
    pBuf = (unsigned char *)malloc( T_MAX_DATA_SIZE + 1 );
    if( pBuf == NULL )
    {
        printf( "pBuf malloc failed...\n" );
        return 0;
    }
    fd = open( "/dev/ttyS2", O_RDWR | O_NOCTTY );
    if( fd < 0 )
    {
        printf( "tty open failed...%d\n", fd );
        return 0;
    }
    ret = tcgetattr(fd, &tio);
    if (ret < 0) {
        printf( "tcgetattr failed...%d\n", fd );
        return 0;
    }
    tio.c_speed = B115200;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~PARODD;
    tio.c_cflag |= CSTOPB;
    ret = tcsetattr(fd, TCSANOW, &tio);
    if (ret < 0) {
        printf( "tcsetattr failed...%d\n", fd );
        return 0;
    }
    
    

    printf( "%s(%d) : start read from uart...\n", __func__, __LINE__ );
	
	char cmp[] = "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";
	char tmp[101] = {};
	int cnt = 0;
    while( 1 )
    {
        memset( pBuf, 0, T_MAX_DATA_SIZE );
        ret = read( fd, pBuf, T_MAX_DATA_SIZE );
        //printf("input length = %d \n", ret);
		
        if( ret <= 0 )
        {
            break;
        }

		cnt = cnt + ret;
		strcat(tmp, pBuf);


		if(cnt >= 100) 
		{
			cnt = 0;
			if(strcmp(tmp, cmp) == 0)
			{
				printf("Matched %s\r\n",tmp);
			}
			else
			{
				printf("Fail to Matched !!!! %s\r\n",tmp);
			}
			memset(tmp,0,sizeof(tmp));
		}

        ret = write( fd, pBuf, ret );
        if( ret <= 0 )
        {
            break;
        }
    }

    if( pBuf != NULL )
    {
        free( pBuf );
    }

	return 0;
#endif    
	return 0;
}
