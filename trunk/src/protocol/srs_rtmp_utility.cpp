/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_rtmp_utility.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <stdlib.h>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_io.hpp>

// 解析tc_url，获取相应字段
// schema 表示url中协议类型
// host 表示主机地址
// port 表示主机端口
// app 表示应用的名字
// vhost 表示虚拟主机
// param 表示额外参数
void srs_discovery_tc_url(
    string tcUrl, 
    string& schema, string& host, string& vhost, 
    string& app, string& port, std::string& param
) {
    size_t pos = std::string::npos;
    std::string url = tcUrl;
    // 解析url中的协议类型，比如rtmp，http之类的，这里应该为rtmp
    if ((pos = url.find("://")) != std::string::npos) {
        schema = url.substr(0, pos);
        url = url.substr(schema.length() + 3);
        srs_info("discovery schema=%s", schema.c_str());
    }
    // 解析url中的主机地址，可以是dns域名，也可以是ip
    if ((pos = url.find("/")) != std::string::npos) {
        host = url.substr(0, pos);
        url = url.substr(host.length() + 1);
        srs_info("discovery host=%s", host.c_str());
    }
	// 默认端口为1935
    port = SRS_CONSTS_RTMP_DEFAULT_PORT;
	// 解析url中的通信端口
    if ((pos = host.find(":")) != std::string::npos) {
        port = host.substr(pos + 1);
        host = host.substr(0, pos);
        srs_info("discovery host=%s, port=%s", host.c_str(), port.c_str());
    }
    
    app = url;
    vhost = host;
	// 解析vhost，app及额外参数param
    srs_vhost_resolve(vhost, app, param);
}

// 根据app 参数解析vhost 以及param 值
void srs_vhost_resolve(string& vhost, string& app, string& param)
{
    // get original param
    size_t pos = 0;
	// 额外参数字符串保存，第一个问号之前的数据是app，之后的数据就是额外参数了
	// 比如rtmp://127.0.0.1:[port]/live?vhost=[vhost]/livestream，一开始app为live?vhost=[vhost]/livestream
	// 实际app则为"live"，param为"?vhost=[vhost]/livestream"
	if ((pos = app.find("?")) != std::string::npos) {
		// 额外参数
        param = app.substr(pos);
    }
    
    // filter tcUrl
    // 将app 字符串中的特殊字符统一替换成? 问号，然后再接着处理
    app = srs_string_replace(app, ",", "?");
    app = srs_string_replace(app, "...", "?");
    app = srs_string_replace(app, "&&", "?");
    app = srs_string_replace(app, "=", "?");
    
    if ((pos = app.find("?")) != std::string::npos) {
		// query此时是app后面的数据，比如"vhost=[vhost]/livestream"
        std::string query = app.substr(pos + 1);
        app = app.substr(0, pos);
        // 解析获得vhost
        if ((pos = query.find("vhost?")) != std::string::npos) {
            query = query.substr(pos + 6);
            if (!query.empty()) {
                vhost = query;
            }
            if ((pos = vhost.find("?")) != std::string::npos) {
                vhost = vhost.substr(0, pos);
            }
        }
    }
    
    /* others */
	// 如果想要扩展对tcUrl的解析，可以在这里继续添加解析代码，主要用于功能扩展
}

void srs_random_generate(char* bytes, int size)
{
    static bool _random_initialized = false;
    if (!_random_initialized) {
        srand(0);
        _random_initialized = true;
        srs_trace("srand initialized the random.");
    }
    
    for (int i = 0; i < size; i++) {
        // the common value in [0x0f, 0xf0]
        bytes[i] = 0x0f + (rand() % (256 - 0x0f - 0x0f));
    }
}

string srs_generate_tc_url(string ip, string vhost, string app, string port, string param)
{
    string tcUrl = "rtmp://";
    
    if (vhost == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        tcUrl += ip;
    } else {
        tcUrl += vhost;
    }
    
    if (port != SRS_CONSTS_RTMP_DEFAULT_PORT) {
        tcUrl += ":";
        tcUrl += port;
    }
    
    tcUrl += "/";
    tcUrl += app;
    tcUrl += param;
    
    return tcUrl;
}

/**
* compare the memory in bytes.
*/
bool srs_bytes_equals(void* pa, void* pb, int size)
{
    u_int8_t* a = (u_int8_t*)pa;
    u_int8_t* b = (u_int8_t*)pb;
    
    if (!a && !b) {
        return true;
    }
    
    if (!a || !b) {
        return false;
    }
    
    for(int i = 0; i < size; i++){
        if(a[i] != b[i]){
            return false;
        }
    }

    return true;
}

int srs_do_rtmp_create_msg(char type, u_int32_t timestamp, char* data, int size, int stream_id, SrsSharedPtrMessage** ppmsg)
{
    int ret = ERROR_SUCCESS;
    
    *ppmsg = NULL;
    SrsSharedPtrMessage* msg = NULL;
    
    if (type == SrsCodecFlvTagAudio) {
        SrsMessageHeader header;
        header.initialize_audio(size, timestamp, stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->create(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(msg);
            return ret;
        }
    } else if (type == SrsCodecFlvTagVideo) {
        SrsMessageHeader header;
        header.initialize_video(size, timestamp, stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->create(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(msg);
            return ret;
        }
    } else if (type == SrsCodecFlvTagScript) {
        SrsMessageHeader header;
        header.initialize_amf0_script(size, stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->create(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(msg);
            return ret;
        }
    } else {
        ret = ERROR_STREAM_CASTER_FLV_TAG;
        srs_error("rtmp unknown tag type=%#x. ret=%d", type, ret);
        return ret;
    }

    *ppmsg = msg;

    return ret;
}

int srs_rtmp_create_msg(char type, u_int32_t timestamp, char* data, int size, int stream_id, SrsSharedPtrMessage** ppmsg)
{
    int ret = ERROR_SUCCESS;

    // only when failed, we must free the data.
    if ((ret = srs_do_rtmp_create_msg(type, timestamp, data, size, stream_id, ppmsg)) != ERROR_SUCCESS) {
        srs_freepa(data);
        return ret;
    }

    return ret;
}
// 根据vhost，app，stream生成相应的stream url
std::string srs_generate_stream_url(std::string vhost, std::string app, std::string stream) 
{
    std::string url = "";
    
    if (SRS_CONSTS_RTMP_DEFAULT_VHOST != vhost){
    	url += vhost;
    }
    url += "/";
    url += app;
    url += "/";
    url += stream;

    return url;
}

int srs_write_large_iovs(ISrsProtocolReaderWriter* skt, iovec* iovs, int size, ssize_t* pnwrite)
{
    int ret = ERROR_SUCCESS;
    
    // the limits of writev iovs.
    // for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
    // for linux, generally it's 1024.
    static int limits = (int)sysconf(_SC_IOV_MAX);
#else
    static int limits = 1024;
#endif
    
    // send in a time.
    if (size < limits) {
        if ((ret = skt->writev(iovs, size, pnwrite)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("send with writev failed. ret=%d", ret);
            }
            return ret;
        }
        return ret;
    }
    
    // send in multiple times.
    int cur_iov = 0;
    while (cur_iov < size) {
        int cur_count = srs_min(limits, size - cur_iov);
        if ((ret = skt->writev(iovs + cur_iov, cur_count, pnwrite)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("send with writev failed. ret=%d", ret);
            }
            return ret;
        }
        cur_iov += cur_count;
    }
    
    return ret;
}

