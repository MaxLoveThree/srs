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

#ifndef SRS_APP_HEALTH_HPP
#define SRS_APP_HEALTH_HPP

/*
#include <srs_app_health.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>

#include <string>
#include <list>

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

/**
* edge used to ingest stream from origin.
*/
// 边缘服务器对应的源服务器健康检测
class SrsHealthCheck : public ISrsReusableThread2Handler
{
private:
    SrsReusableThread2* pthread;
    st_netfd_t stfd;
	std::string vhost;
	std::string origin;
	std::string server;
	int	port;
	bool health;
public:
    SrsHealthCheck(std::string v, std::string o);
    virtual ~SrsHealthCheck();
public:
    virtual int start();
    virtual void stop();
	std::string get_vhost();
	std::string get_origin();
// interface ISrsReusableThread2Handler
public:
    virtual int cycle();
private:
    virtual void close_underlayer_socket();
};

enum SrsHealthMissionType
{
	SrsHealthMissionType_Add,
	SrsHealthMissionType_Remove,
};

class SrsHealthMission
{
public:
	SrsHealthMission(SrsHealthMissionType t, std::string v, std::string o);
	~SrsHealthMission();
	SrsHealthMissionType mission_type;
	std::string vhost;
	std::string origin;
};
// 健康检测机制实现类
class SrsHealth
{
private:
	std::list<SrsHealthCheck*> health_checks;
	std::list<SrsHealthMission*> missions;
public:
    SrsHealth();
    virtual ~SrsHealth();
public:
	virtual int initialize();
    virtual int cycle();
	virtual int on_reload_vhost_added(std::string vhost);
	virtual int on_reload_vhost_removed(std::string vhost);
	virtual int on_reload_vhost_origin(std::string vhost);
private:
	void destroy_health_checks();
	void destroy_missions();
	bool is_health_check(std::string vhost, std::string origin);
	SrsHealthCheck* pop_health_check(std::string vhost, std::string origin);
};

#endif

