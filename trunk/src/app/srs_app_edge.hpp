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

#ifndef SRS_APP_EDGE_HPP
#define SRS_APP_EDGE_HPP

#include <map>

/*
#include <srs_app_edge.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>

#include <string>

class SrsStSocket;
class SrsRtmpServer;
class SrsSource;
class SrsRequest;
class SrsPlayEdge;
class SrsPublishEdge;
class SrsRtmpClient;
class SrsCommonMessage;
class SrsMessageQueue;
class ISrsProtocolReaderWriter;
class SrsKbps;
class SrsConfDirective;

/**
* the state of edge, auto machine
*/
enum SrsEdgeState
{
    SrsEdgeStateInit = 0,

    // for play edge
    SrsEdgeStatePlay = 100,
    // play stream from origin, ingest stream
    SrsEdgeStateIngestConnected = 101,
    
    // for publish edge
    SrsEdgeStatePublish = 200,
};

/**
* the state of edge from user, manual machine
*/
enum SrsEdgeUserState
{
    SrsEdgeUserStateInit = 0,
    SrsEdgeUserStateReloading = 100,
};

/**
* edge used to ingest stream from origin.
*/
// ��Ե������������Դ��������������
class SrsEdgeIngester : public ISrsReusableThread2Handler
{
private:
    int stream_id;
private:
    SrsSource* _source;
    SrsPlayEdge* _edge;
    SrsRequest* _req;
    SrsReusableThread2* pthread;
    st_netfd_t stfd;
    ISrsProtocolReaderWriter* io;
    SrsKbps* kbps;
    SrsRtmpClient* client;
	// ��ǰ����ʹ�õ�Դ��������λ�������е����
	int cur_origin_index;
	// �´�����ʹ�õ�Դ��������λ�������е����
    int origin_index;
    // Դ�����������ɼ�ʧ��map��¼��keyΪԴ��������λ�������е���ţ�valueΪϵͳʱ��
    // ʧ�ܱ�׼:1, Դ���������Ӳ���2, �������ˣ���ָ��ʱ����������Ƶ����3, �������ˣ��յ�playSourceInvalid��Ϣ
    // һ����һ��Դ�����������ɹ���ͳ��map
	std::map<int, int64_t> ingest_fail;
	// �����е�origin���ݣ�ÿ��ѭ����ʼ֮ǰ���ж����Ƿ���Ҫ���»�ȡ
	SrsConfDirective* origin;
	bool _reload_origin;
public:
    SrsEdgeIngester();
    virtual ~SrsEdgeIngester();
public:
    virtual int initialize(SrsSource* source, SrsPlayEdge* edge, SrsRequest* req);
    virtual int start();
    virtual void stop();
	// ����ֵΪtrue�������������������play�ͻ��ˣ�ֱ�ӻظ�playSourceInvalid��Ϣ
	virtual bool is_ingest_fail_all();
	virtual void reload_origin();
// interface ISrsReusableThread2Handler
public:
	virtual int on_before_cycle();
    virtual int cycle();
private:
	virtual int cycle_imp();
    virtual int ingest();
    virtual void close_underlayer_socket();
	virtual void ingest_fail_record();
	virtual void ingest_fail_clear();
    virtual int connect_server(std::string& ep_server, std::string& ep_port);
    virtual int connect_app(std::string ep_server, std::string ep_port);
    virtual int process_publish_message(SrsCommonMessage* msg);
};

/**
* edge used to forward stream to origin.
*/
class SrsEdgeForwarder : public ISrsReusableThread2Handler
{
private:
    int stream_id;
private:
    SrsSource* _source;
    SrsPublishEdge* _edge;
    SrsRequest* _req;
    SrsReusableThread2* pthread;
    st_netfd_t stfd;
    ISrsProtocolReaderWriter* io;
    SrsKbps* kbps;
    SrsRtmpClient* client;//��Դ�������������ӵĿͻ��˽ṹ��
    int origin_index;
    /**
    * we must ensure one thread one fd principle,
    * that is, a fd must be write/read by the one thread.
    * the publish service thread will proxy(msg), and the edge forward thread
    * will cycle(), so we use queue for cycle to send the msg of proxy.
    */
    SrsMessageQueue* queue;
    /**
    * error code of send, for edge proxy thread to query.
    */
    int send_error_code;
public:
    SrsEdgeForwarder();
    virtual ~SrsEdgeForwarder();
public:
    virtual void set_queue_size(double queue_size);
public:
    virtual int initialize(SrsSource* source, SrsPublishEdge* edge, SrsRequest* req);
    virtual int start();
    virtual void stop();
// interface ISrsReusableThread2Handler
public:
    virtual int cycle();
public:
    virtual int proxy(SrsCommonMessage* msg);
private:
    virtual void close_underlayer_socket();
    virtual int connect_server(std::string& ep_server, std::string& ep_port);
    virtual int connect_app(std::string ep_server, std::string ep_port);
};

/**
* play edge control service.
* downloading edge speed-up.
*/
// ��Ե�����࣬������Դվ��ȡ��Դ
class SrsPlayEdge
{
private:
    SrsEdgeState state;
    SrsEdgeIngester* ingester;
public:
    SrsPlayEdge();
    virtual ~SrsPlayEdge();
public:
    /**
    * always use the req of source,
    * for we assume all client to edge is invalid,
    * if auth open, edge must valid it from origin, then service it.
    */
    virtual int initialize(SrsSource* source, SrsRequest* req);
    /**
    * when client play stream on edge.
    */
    virtual int on_client_play();
	virtual bool is_ingest_fail_all();
    /**
    * when all client stopped play, disconnect to origin.
    */
    virtual void on_all_client_stop();
public:
    /**
    * when ingester start to play stream.
    */
    virtual int on_ingest_play();
};

/**
* publish edge control service.
* uploading edge speed-up.
*/
class SrsPublishEdge
{
private:
    SrsEdgeState state;
    SrsEdgeForwarder* forwarder;
public:
    SrsPublishEdge();
    virtual ~SrsPublishEdge();
public:
    virtual void set_queue_size(double queue_size);
public:
    virtual int initialize(SrsSource* source, SrsRequest* req);
    virtual bool can_publish();
    /**
    * when client publish stream on edge.
    */
    virtual int on_client_publish();
    /**
    * proxy publish stream to edge
    */
    virtual int on_proxy_publish(SrsCommonMessage* msg);
    /**
    * proxy unpublish stream to edge.
    */
    virtual void on_proxy_unpublish();
};

#endif

