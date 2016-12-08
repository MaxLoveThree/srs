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

#include <srs_app_health.hpp>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <set>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_io.hpp>
#include <srs_app_config.hpp>
#include <srs_rtmp_utility.hpp>
#include <srs_app_st.hpp>
#include <srs_app_source.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_rtmp_amf0.hpp>
#include <srs_kernel_utility.hpp>

// health check cycle interval time
#define SRS_HEALTH_CHECK_SLEEP_US (int64_t)(10*1000*1000LL)

// health check connect origin timeout
#define SRS_HEALTH_CHECK_CONNECT_TIMEOUT_US (int64_t)(5*1000*1000LL)


SrsHealthCheck::SrsHealthCheck(std::string v, std::string o)
{
	vhost = v;
	origin = o;
	// �������ò��õ���Ч��server��port
	std::string s_port = SRS_CONSTS_RTMP_DEFAULT_PORT;
	port = ::atoi(SRS_CONSTS_RTMP_DEFAULT_PORT);
	server = origin;
	size_t pos = server.find(":");
	if (pos != std::string::npos) {
		s_port = server.substr(pos + 1);
		server = server.substr(0, pos);
		port = ::atoi(s_port.c_str());
	}
	// ���Ĳ���Ϊst�߳������ѭ����sleepʱ��
    pthread = new SrsReusableThread2("origin-health-check", this, SRS_HEALTH_CHECK_SLEEP_US);
	// ��δ���ǰ��Ĭ��Ϊʧ��
	health = false;
}

SrsHealthCheck::~SrsHealthCheck()
{
    stop();
	srs_freep(pthread);
}

int SrsHealthCheck::start()
{
	// �˴�����һ��st�̣߳���ѭ������SrsEdgeIngester::cycle
    return pthread->start();
}

void SrsHealthCheck::stop()
{
    pthread->stop();
    close_underlayer_socket();
}

std::string SrsHealthCheck::get_vhost()
{
    return vhost;
}

std::string SrsHealthCheck::get_origin()
{
    return origin;
}

int SrsHealthCheck::cycle()
{
    int ret = ERROR_SUCCESS;
    // open socket.
    int64_t timeout = SRS_HEALTH_CHECK_CONNECT_TIMEOUT_US;
	// ����Դ������
    if ((ret = srs_socket_connect(server, port, timeout, &stfd)) != ERROR_SUCCESS)
	{
        
		if (true == health)
		{
			srs_warn("health check [succ]-->[fail], vhost=%s, origin=%s, server=%s, port=%d, timeout=%"PRId64", ret=%d",
            	vhost.c_str(), origin.c_str(), server.c_str(), port, timeout, ret);
			health = false;
		}
		else
		{
			// �ô�ӡ���ʧ����ᶨ�ڳ��֣������ڷ����������⣬������������
			srs_warn("health check failed again, vhost=%s, origin=%s, server=%s, port=%d, timeout=%"PRId64", ret=%d",
            	vhost.c_str(), origin.c_str(), server.c_str(), port, timeout, ret);
		}
    }
	else
	{
		if (false == health)
		{
			srs_warn("health check [fail]-->[succ], vhost=%s, origin=%s, server=%s, port=%d",
            	vhost.c_str(), origin.c_str(), server.c_str(), port);
			
			health = true;
		}
		// ���ӳɹ�������رգ�����Դ��������ɹ������ܸ���
		close_underlayer_socket();
	}
    // �������ӳɹ���������ʧ�ܣ����ڸ�ѭ����˵�������������������Ƿ��سɹ�
    return ERROR_SUCCESS;
}

void SrsHealthCheck::close_underlayer_socket()
{
    srs_close_stfd(stfd);
}

SrsHealthMission::SrsHealthMission(SrsHealthMissionType t, std::string v, std::string o)
{
	mission_type = t;
	vhost = v;
	origin = o;
}

SrsHealthMission::~SrsHealthMission()
{
	
}

SrsHealth::SrsHealth()
{

}

SrsHealth::~SrsHealth()
{
	destroy_missions();
	destroy_health_checks();
}

int SrsHealth::initialize()
{
    int ret = ERROR_SUCCESS;
    std::vector<SrsConfDirective*> vhosts_conf;
	_srs_config->get_vhosts(vhosts_conf);
	for (int i = 0; i < (int)vhosts_conf.size(); i++) {
        SrsConfDirective* vhost = vhosts_conf[i];
		if (false == _srs_config->get_vhost_enabled(vhost) || false == _srs_config->get_vhost_is_edge(vhost))
		{
			continue;
		}
		std::string svhost = vhost->arg0();
		SrsConfDirective* origins = _srs_config->get_vhost_edge_origin(svhost);
		for (int j = 0; j < (int)origins->args.size(); j++)
		{
			std::string origin = origins->args.at(j);
			SrsHealthMission* mission = new SrsHealthMission(SrsHealthMissionType_Add, svhost, origin);
			missions.push_back(mission);
		}
    }
	
    return ret;
}

int SrsHealth::cycle()
{
    int ret = ERROR_SUCCESS;
	while (missions.size() > 0)
	{
		// �������б�ȡ����
		SrsHealthMission* mission = missions.front();
		SrsAutoFree(SrsHealthMission,mission);
		missions.pop_front();
		
		srs_assert(mission);
		// ���ӽ����������
		if (SrsHealthMissionType_Add == mission->mission_type)
		{
			if (true == is_health_check(mission->vhost, mission->origin))
			{
				srs_warn("vhost[%s] origin[%s] already health checking, add fail", mission->vhost.c_str(), mission->origin.c_str());
				continue;
			}

			SrsHealthCheck* health_check = new SrsHealthCheck(mission->vhost, mission->origin);
			health_checks.push_back(health_check);
			if ((ret = health_check->start()) != ERROR_SUCCESS)
			{
				srs_error("vhost[%s], origin[%s] health check start fail, ret=%d", mission->vhost.c_str(), mission->origin.c_str(), ret);
				return ret;
			}
			else
			{
				srs_trace("vhost[%s] origin[%s] health check start", mission->vhost.c_str(), mission->origin.c_str());
			}
		}
		else if (SrsHealthMissionType_Remove == mission->mission_type)// �Ƴ������������
		{
			SrsHealthCheck* health_check = pop_health_check(mission->vhost, mission->origin);
			if (NULL == health_check)
			{
				srs_warn("vhost[%s] origin[%s] is not health checking, remove fail", mission->vhost.c_str(), mission->origin.c_str());
				continue;
			}

			srs_freep(health_check);
			srs_trace("vhost[%s] origin[%s] health check stop", mission->vhost.c_str(), mission->origin.c_str());
		}
		else
		{
			srs_warn("unknown mission type[%d]", mission->mission_type);
		}
	}
	
    return ret;
}

int SrsHealth::on_reload_vhost_added(std::string vhost)
{
	int ret = ERROR_SUCCESS;
	SrsConfDirective* origins = _srs_config->get_vhost_edge_origin(vhost);
	// �ж��Ƿ��Ǳ�Ե����
	if (NULL != origins)
	{
		for (int i = 0; i < (int)origins->args.size(); i++)
		{
			std::string origin = origins->args.at(i);
			SrsHealthMission* mission = new SrsHealthMission(SrsHealthMissionType_Add, vhost, origin);
			missions.push_back(mission);
		}
	}

	return ret;
}

int SrsHealth::on_reload_vhost_removed(std::string vhost)
{
	int ret = ERROR_SUCCESS;
	std::list<SrsHealthCheck*>::iterator iter = health_checks.begin();
	for (; iter != health_checks.end(); iter++)
	{
		srs_assert(*iter);
		if (((*iter)->get_vhost() != vhost))
		{
			continue;
		}

		// ���ɾ��origin����
		SrsHealthMission* mission = new SrsHealthMission(SrsHealthMissionType_Remove, vhost, (*iter)->get_origin());
		missions.push_back(mission);
	}

	return ret;
}

// ��vhost��Ȼ��ʹ�ܵģ���modeû�б��޸Ĺ��ģ������ڲ����ٽ����ж�
int SrsHealth::on_reload_vhost_origin(std::string vhost)
{
	int ret = ERROR_SUCCESS;
	std::set<std::string> origin_new;
	std::set<std::string> origin_old;
	SrsConfDirective* origins = _srs_config->get_vhost_edge_origin(vhost);
	if (NULL != origins)
	{
		for (int i = 0; i < (int)origins->args.size(); i++)
		{
			std::string origin = origins->args.at(i);
			origin_new.insert(origin);
		}
	}

	std::list<SrsHealthCheck*>::iterator iter = health_checks.begin();
	for (; iter != health_checks.end(); iter++)
	{
		srs_assert(*iter);
		if (((*iter)->get_vhost() != vhost))
		{
			continue;
		}

		origin_old.insert((*iter)->get_origin());
	}
	// ���ɾ�������������
	std::set<std::string>::iterator iter1 = origin_old.begin();
	for (; iter1 != origin_old.end(); iter1++)
	{
		// ��vhost���ڸ�origin���򲻶Ը�origin���д���
		if (origin_new.find(*iter1) != origin_new.end())
		{
			continue;
		}
		// ���ɾ��origin����
		SrsHealthMission* mission = new SrsHealthMission(SrsHealthMissionType_Remove, vhost, *iter1);
		missions.push_back(mission);
	}
	// ������ӽ����������
	iter1 = origin_new.begin();
	for (; iter1 != origin_new.end(); iter1++)
	{
		// ��vhost�Ѵ��ڸ�origin���򲻶Ը�origin���д���
		if (origin_old.find(*iter1) != origin_old.end())
		{
			continue;
		}
		// �������origin����
		SrsHealthMission* mission = new SrsHealthMission(SrsHealthMissionType_Add, vhost, *iter1);
		missions.push_back(mission);
	}

	return ret;
}

void SrsHealth::destroy_health_checks()
{
	std::list<SrsHealthCheck*>::iterator iter = health_checks.begin();
	for (; iter != health_checks.end(); iter++)
	{
		srs_freep(*iter);
		health_checks.erase(iter);
	}
}

void SrsHealth::destroy_missions()
{
	std::list<SrsHealthMission*>::iterator iter = missions.begin();
	for (; iter != missions.end(); iter++)
	{
		srs_freep(*iter);
		missions.erase(iter);
	}
}

bool SrsHealth::is_health_check(std::string vhost, std::string origin)
{
	std::list<SrsHealthCheck*>::iterator iter = health_checks.begin();
	for (; iter != health_checks.end(); iter++)
	{
		srs_assert(*iter);
		if (((*iter)->get_vhost() == vhost) && ((*iter)->get_origin() == origin))
		{
			return true;
		}
	}

	return false;
}

SrsHealthCheck* SrsHealth::pop_health_check(std::string vhost, std::string origin)
{
	std::list<SrsHealthCheck*>::iterator iter = health_checks.begin();
	for (; iter != health_checks.end(); iter++)
	{
		srs_assert(*iter);
		if (((*iter)->get_vhost() == vhost) && ((*iter)->get_origin() == origin))
		{
			SrsHealthCheck* ret = *iter;
			health_checks.erase(iter);
			return ret;
		}
	}

	return NULL;
}


