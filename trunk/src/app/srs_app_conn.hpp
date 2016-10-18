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

#ifndef SRS_APP_CONN_HPP
#define SRS_APP_CONN_HPP

/*
#include <srs_app_conn.hpp>
*/

#include <srs_core.hpp>

#include <string>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>
#include <srs_protocol_kbps.hpp>

class SrsConnection;

/**
 * the manager for connection.
 */
// srs connect 链接管理类的句柄类
class IConnectionManager
{
public:
    IConnectionManager();
    virtual ~IConnectionManager();
public:
    /**
     * remove the specified connection.
     */
    virtual void remove(SrsConnection* c) = 0;
};

/**
* the basic connection of SRS,
* all connections accept from listener must extends from this base class,
* server will add the connection to manager, and delete it when remove.
*/
// 客户端链接抽象类，继承了单次循环句柄类
// 该类实现了ISrsOneCycleThreadHandler句柄类对应的抽象接口，并增加了自己需要的功能接口以及抽象接口
class SrsConnection : public virtual ISrsOneCycleThreadHandler, public virtual IKbpsDelta
{
private:
    /**
    * each connection start a green thread,
    * when thread stop, the connection will be delete by server.
    */
    // 单次循环线程类指针，该类由SrsConnection构造函数new出来，并赋值
    SrsOneCycleThread* pthread;
    /**
    * the id of connection.
    */
    // 链接id
    int id;
protected:
    /**
    * the manager object to manage the connection.
    */
    // 链接管理类指针
    IConnectionManager* manager;
    /**
    * the underlayer st fd handler.
    */
    // 链接对应的st的fd
    st_netfd_t stfd;
    /**
    * the ip of client.
    */
    // 链接对应的客户端ip
    std::string ip;
    /**
     * whether the connection is disposed,
     * when disposed, connection should stop cycle and cleanup itself.
     */
    // 
    bool disposed;
    /**
     * whether connection is expired, application definition.
     * when expired, the connection must never be served and quit ASAP.
     */
    // 链接是否失效标志
    bool expired;
public:
    SrsConnection(IConnectionManager* cm, st_netfd_t c);
    virtual ~SrsConnection();
public:
    /**
     * to dipose the connection.
     */
    virtual void dispose();
    /**
    * start the client green thread.
    * when server get a client from listener, 
    * 1. server will create an concrete connection(for instance, RTMP connection),
    * 2. then add connection to its connection manager,
    * 3. start the client thread by invoke this start()
    * when client cycle thread stop, invoke the on_thread_stop(), which will use server
    * to remove the client by server->remove(this).
    */
    virtual int start();
// interface ISrsOneCycleThreadHandler
public:
    /**
    * the thread cycle function,
    * when serve connection completed, terminate the loop which will terminate the thread,
    * thread will invoke the on_thread_stop() when it terminated.
    */
    // ISrsOneCycleThreadHandler句柄类对应的抽象接口的定义
    virtual int cycle();
    /**
    * when the thread cycle finished, thread will invoke the on_thread_stop(),
    * which will remove self from server, server will remove the connection from manager 
    * then delete the connection.
    */
    virtual void on_thread_stop();
public:
    /**
    * get the srs id which identify the client.
    */
    virtual int srs_id();
    /**
     * set connection to expired.
     */
    // 将链接失效标志位expired置为true
    virtual void expire();
protected:
    /**
    * for concrete connection to do the cycle.
    */
    // 新增抽象接口，在cycle中被调用
    virtual int do_cycle() = 0;
};

#endif
