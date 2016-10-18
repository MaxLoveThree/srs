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

#ifndef SRS_APP_HTTP_CLIENT_HPP
#define SRS_APP_HTTP_CLIENT_HPP

/*
#include <srs_app_http_client.hpp>
*/
#include <srs_core.hpp>

#include <string>

#ifdef SRS_AUTO_HTTP_CORE

#include <srs_app_st.hpp>

class SrsHttpUri;
class SrsHttpParser;
class ISrsHttpMessage;
class SrsStSocket;

// the default timeout for http client.
// http客户端超时时间
#define SRS_HTTP_CLIENT_TIMEOUT_US (int64_t)(30*1000*1000LL)

/**
* http client to GET/POST/PUT/DELETE uri
*/
// http客户端类
class SrsHttpClient
{
private:
	// 链接标志
    bool connected;
	// st对应的fd
    st_netfd_t stfd;
	// st 的socket类
    SrsStSocket* skt;
	// http的解析类指针
    SrsHttpParser* parser;
private:
	// 客户端超时时间
    int64_t timeout_us;
    // host name or ip.
    // 主机ip
    std::string host;
	// 端口
    int port;
public:
    SrsHttpClient();
    virtual ~SrsHttpClient();
public:
    /**
    * initialize the client, connect to host and port.
    */
    // http客户端初始化
    // 入参分别是服务器主机地址和端口
    virtual int initialize(std::string h, int p, int64_t t_us = SRS_HTTP_CLIENT_TIMEOUT_US);
public:
    /**
    * to post data to the uri.
    * @param the path to request on.
    * @param req the data post to uri. empty string to ignore.
    * @param ppmsg output the http message to read the response.
    */
    // 向http服务器发送POST消息，path为http-api路径
    virtual int post(std::string path, std::string req, ISrsHttpMessage** ppmsg);
    /**
    * to get data from the uri.
    * @param the path to request on.
    * @param req the data post to uri. empty string to ignore.
    * @param ppmsg output the http message to read the response.
    */
    // 向http服务器发送GET消息，path为http-api路径
    virtual int get(std::string path, std::string req, ISrsHttpMessage** ppmsg);
private:
	// 断开链接
    virtual void disconnect();
	// 链接http服务器
    virtual int connect();
};

#endif

#endif

