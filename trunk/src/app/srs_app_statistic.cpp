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

#include <srs_app_statistic.hpp>

#include <unistd.h>
#include <sstream>
using namespace std;

#include <srs_rtmp_stack.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_log.hpp>

int64_t srs_gvid = getpid();

int64_t srs_generate_id()
{
    return srs_gvid++;
}

SrsStatisticVhost::SrsStatisticVhost()
{
    id = srs_generate_id();
    
    kbps = new SrsKbps();
    kbps->set_io(NULL, NULL);
    
    nb_clients = 0;
    nb_streams = 0;
}

SrsStatisticVhost::~SrsStatisticVhost()
{
    srs_freep(kbps);
}

int SrsStatisticVhost::dumps(stringstream& ss)
{
    int ret = ERROR_SUCCESS;
    
    // dumps the config of vhost.
    bool hls_enabled = _srs_config->get_hls_enabled(vhost);
    bool enabled = _srs_config->get_vhost_enabled(vhost);
    
    ss << SRS_JOBJECT_START
            << SRS_JFIELD_ORG("id", id) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("name", vhost) << SRS_JFIELD_CONT
            << SRS_JFIELD_BOOL("enabled", enabled) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("clients", nb_clients) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("streams", nb_streams) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("send_bytes", kbps->get_send_bytes()) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("recv_bytes", kbps->get_recv_bytes()) << SRS_JFIELD_CONT
            << SRS_JFIELD_OBJ("kbps")
                << SRS_JFIELD_ORG("recv_30s", kbps->get_recv_kbps_30s()) << SRS_JFIELD_CONT
                << SRS_JFIELD_ORG("send_30s", kbps->get_send_kbps_30s())
            << SRS_JOBJECT_END << SRS_JFIELD_CONT
            << SRS_JFIELD_NAME("hls") << SRS_JOBJECT_START
                << SRS_JFIELD_BOOL("enabled", hls_enabled);
    if (hls_enabled) {
        ss                                                  << SRS_JFIELD_CONT;
        ss      << SRS_JFIELD_ORG("fragment", _srs_config->get_hls_fragment(vhost));
    }
    ss      << SRS_JOBJECT_END
        << SRS_JOBJECT_END;
    
    return ret;
}

SrsStatisticStream::SrsStatisticStream()
{
    id = srs_generate_id();
    vhost = NULL;
    active = false;
    connection_cid = -1;
    
    has_video = false;
    vcodec = SrsCodecVideoReserved;
    avc_profile = SrsAvcProfileReserved;
    avc_level = SrsAvcLevelReserved;
    
    has_audio = false;
    acodec = SrsCodecAudioReserved1;
    asample_rate = SrsCodecAudioSampleRateReserved;
    asound_type = SrsCodecAudioSoundTypeReserved;
    aac_object = SrsAacObjectTypeReserved;
    
    kbps = new SrsKbps();
    kbps->set_io(NULL, NULL);
    
    nb_clients = 0;

	publish_start_time = 0;
}

SrsStatisticStream::~SrsStatisticStream()
{
    srs_freep(kbps);
}

int SrsStatisticStream::dumps(stringstream& ss)
{
    int ret = ERROR_SUCCESS;
    
    ss << SRS_JOBJECT_START
            << SRS_JFIELD_ORG("id", id) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("name", stream) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("vhost", vhost->id) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("app", app) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("live_ms", (publish_start_time ? (srs_get_system_time_ms() - publish_start_time):publish_start_time)) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("clients", nb_clients) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("send_bytes", kbps->get_send_bytes()) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("recv_bytes", kbps->get_recv_bytes()) << SRS_JFIELD_CONT
            << SRS_JFIELD_OBJ("kbps")
                << SRS_JFIELD_ORG("recv_30s", kbps->get_recv_kbps_30s()) << SRS_JFIELD_CONT
                << SRS_JFIELD_ORG("send_30s", kbps->get_send_kbps_30s())
            << SRS_JOBJECT_END << SRS_JFIELD_CONT
            << SRS_JFIELD_OBJ("publish")
                << SRS_JFIELD_BOOL("active", active) << SRS_JFIELD_CONT
                << SRS_JFIELD_ORG("play_client_count", play_cid_set.size()) << SRS_JFIELD_CONT
                << SRS_JFIELD_ORG("cid", connection_cid)
            << SRS_JOBJECT_END << SRS_JFIELD_CONT;
    
    if (!has_video) {
        ss  << SRS_JFIELD_NULL("video") << SRS_JFIELD_CONT;
    } else {
        ss  << SRS_JFIELD_NAME("video") << SRS_JOBJECT_START
                << SRS_JFIELD_STR("codec", srs_codec_video2str(vcodec)) << SRS_JFIELD_CONT
                << SRS_JFIELD_STR("profile", srs_codec_avc_profile2str(avc_profile)) << SRS_JFIELD_CONT
                << SRS_JFIELD_STR("level", srs_codec_avc_level2str(avc_level))
                << SRS_JOBJECT_END
            << SRS_JFIELD_CONT;
    }
    
    if (!has_audio) {
        ss  << SRS_JFIELD_NULL("audio");
    } else {
        ss  << SRS_JFIELD_NAME("audio") << SRS_JOBJECT_START
                << SRS_JFIELD_STR("codec", srs_codec_audio2str(acodec)) << SRS_JFIELD_CONT
                << SRS_JFIELD_ORG("sample_rate", (int)flv_sample_rates[asample_rate]) << SRS_JFIELD_CONT
                << SRS_JFIELD_ORG("channel", (int)asound_type + 1) << SRS_JFIELD_CONT
                << SRS_JFIELD_STR("profile", srs_codec_aac_object2str(aac_object))
            << SRS_JOBJECT_END;
    }
    
    ss << SRS_JOBJECT_END;
    
    return ret;
}

void SrsStatisticStream::publish(int cid)
{
    connection_cid = cid;
    active = true;
    
    vhost->nb_streams++;
	publish_start_time = srs_get_system_time_ms();
}

void SrsStatisticStream::close()
{
    has_video = false;
    has_audio = false;
    active = false;
    connection_cid = -1;
    vhost->nb_streams--;
	publish_start_time = 0;
}

SrsStatisticClient::SrsStatisticClient()
{
    id = 0;
    stream = NULL;
    conn = NULL;
    req = NULL;
    type = SrsRtmpConnUnknown;
    create = srs_get_system_time_ms();
	kick = false;
}

SrsStatisticClient::~SrsStatisticClient()
{
}

int SrsStatisticClient::dumps(stringstream& ss)
{
    int ret = ERROR_SUCCESS;
    
    ss << SRS_JOBJECT_START
            << SRS_JFIELD_ORG("id", id) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("vhost", stream->vhost->id) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("stream", stream->id) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("ip", req->ip) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("pageUrl", req->pageUrl) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("swfUrl", req->swfUrl) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("tcUrl", req->tcUrl) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("url", req->get_stream_url()) << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("type", srs_client_type_string(type)) << SRS_JFIELD_CONT
            << SRS_JFIELD_BOOL("publish", srs_client_type_is_publish(type)) << SRS_JFIELD_CONT
            << SRS_JFIELD_BOOL("kick", kick) << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("alive", srs_get_system_time_ms() - create)
        << SRS_JOBJECT_END;
    
    return ret;
}

SrsStatistic* SrsStatistic::_instance = new SrsStatistic();

SrsStatistic::SrsStatistic()
{
    _server_id = srs_generate_id();
    
    kbps = new SrsKbps();
    kbps->set_io(NULL, NULL);
}

SrsStatistic::~SrsStatistic()
{
    srs_freep(kbps);
    
    if (true) {
        std::map<int64_t, SrsStatisticVhost*>::iterator it;
        for (it = vhosts.begin(); it != vhosts.end(); it++) {
            SrsStatisticVhost* vhost = it->second;
            srs_freep(vhost);
        }
    }
    if (true) {
        std::map<int64_t, SrsStatisticStream*>::iterator it;
        for (it = streams.begin(); it != streams.end(); it++) {
            SrsStatisticStream* stream = it->second;
            srs_freep(stream);
        }
    }
    if (true) {
        std::map<int, SrsStatisticClient*>::iterator it;
        for (it = clients.begin(); it != clients.end(); it++) {
            SrsStatisticClient* client = it->second;
            srs_freep(client);
        }
    }
    
    vhosts.clear();
    rvhosts.clear();
    streams.clear();
    rstreams.clear();
}
// ��ȡͳ����ʵ��ָ��
SrsStatistic* SrsStatistic::instance()
{
    return _instance;
}
// ����idѰ��vhostͳ���࣬�ڲ��������������µ�
SrsStatisticVhost* SrsStatistic::find_vhost(int vid)
{
    std::map<int64_t, SrsStatisticVhost*>::iterator it;
    if ((it = vhosts.find(vid)) != vhosts.end()) {
        return it->second;
    }
    return NULL;
}
// ����idѰ��streamͳ���࣬�ڲ��������������µ�
SrsStatisticStream* SrsStatistic::find_stream(int sid)
{
    std::map<int64_t, SrsStatisticStream*>::iterator it;
    if ((it = streams.find(sid)) != streams.end()) {
        return it->second;
    }
    return NULL;
}
// ����idѰ��clientͳ���࣬�ڲ��������������µ�
SrsStatisticClient* SrsStatistic::find_client(int cid)
{
    std::map<int, SrsStatisticClient*>::iterator it;
    if ((it = clients.find(cid)) != clients.end()) {
        return it->second;
    }
    return NULL;
}
// ��source��video��Ϣͬ����ͳ������
int SrsStatistic::on_video_info(SrsRequest* req, 
    SrsCodecVideo vcodec, SrsAvcProfile avc_profile, SrsAvcLevel avc_level
) {
    int ret = ERROR_SUCCESS;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->has_video = true;
    stream->vcodec = vcodec;
    stream->avc_profile = avc_profile;
    stream->avc_level = avc_level;
    
    return ret;
}
// ��source��audio��Ϣͬ����ͳ������
int SrsStatistic::on_audio_info(SrsRequest* req,
    SrsCodecAudio acodec, SrsCodecAudioSampleRate asample_rate, SrsCodecAudioSoundType asound_type,
    SrsAacObjectType aac_object
) {
    int ret = ERROR_SUCCESS;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->has_audio = true;
    stream->acodec = acodec;
    stream->asample_rate = asample_rate;
    stream->asound_type = asound_type;
    stream->aac_object = aac_object;
    
    return ret;
}
// source�յ�publish��Ϣ��ͳ����ͬ��
void SrsStatistic::on_stream_publish(SrsRequest* req, int cid)
{
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->publish(cid);
}
// source�յ�unpublish��Ϣ��ͳ����ͬ��
void SrsStatistic::on_stream_close(SrsRequest* req)
{
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->close();
}

//���id: �ͻ���������st�߳�id
// ����client�ͻ���ͳ����
int SrsStatistic::on_client(int id, SrsRequest* req, SrsConnection* conn, SrsRtmpConnType type)
{
    int ret = ERROR_SUCCESS;
	//���ɶ�Ӧ��vhostͳ����
    SrsStatisticVhost* vhost = create_vhost(req);
	//���ɶ�Ӧ��streamͳ����
    SrsStatisticStream* stream = create_stream(vhost, req);

    // create client if not exists
	//������λ�ȡ��Ӧ������ͳ�Ƶ�client�ṹ��
	//����Ӧ��clientԭ�������ڣ���new
	//����Ӧ��client�Ѵ��ڣ���ֱ�ӷ���
    SrsStatisticClient* client = NULL;
    if (clients.find(id) == clients.end()) {
        client = new SrsStatisticClient();
        client->id = id;
        client->stream = stream;
        clients[id] = client;
    } else {
        client = clients[id];
    }
    
    // got client.
    client->conn = conn;
    client->req = req;
    client->type = type;

	if (SrsRtmpConnPlay == type)
	{
		stream->play_cid_set.insert(client->id);
	}
	
    stream->nb_clients++;
    vhost->nb_clients++;

    return ret;
}
// ����id�Ƴ�client�ͻ���ͳ���࣬��Ӧ�ͻ���st�߳�ֹͣʱ����
void SrsStatistic::on_disconnect(int id)
{
    std::map<int, SrsStatisticClient*>::iterator it;
    if ((it = clients.find(id)) == clients.end()) {
        return;
    }

    SrsStatisticClient* client = it->second;
    SrsStatisticStream* stream = client->stream;
    SrsStatisticVhost* vhost = stream->vhost;

	if (SrsRtmpConnPlay == client->type)
	{
		stream->play_cid_set.erase(client->id);
	}
	
    srs_freep(client);
    clients.erase(it);

    stream->nb_clients--;

	if (0 == stream->nb_clients)
	{
		//��ǰû���û����������Ҳû���û����͸������ͷ�
		//printf("destroy stream[%s]\n", stream->url.c_str());
		//destroy stream[/my_test/test]
		//��������ͷţ�����Խ��Խ��
		destroy_stream(stream->url);
	}

    vhost->nb_clients--;
	if (0 == vhost->nb_clients)
	{
		//ʵ���ӡ����
		//printf("destroy vhost[%s]\n", vhost->vhost.c_str());
		//destroy vhost[__defaultVhost__]
		//����ò��û��Ҫȥ������ȥ��Ҳ��Ӱ�죬���ǲ��������ͷţ��������е�СӰ��
		destroy_vhost(vhost->vhost);
	}
}
// ��ȡ��Ӧ�ͻ��˵��շ������ֽ�����������������Ӧ��ͳ����
// 1���Կͻ�������kbps���в�����2����������������ӵ�����ͳ�����3���Կͻ���kbpsͳ�����������ݽ���ͬ��
void SrsStatistic::kbps_add_delta(SrsConnection* conn)
{
    int id = conn->srs_id();
    if (clients.find(id) == clients.end()) {
        return;
    }
    
    SrsStatisticClient* client = clients[id];
    
    // resample the kbps to collect the delta.
    conn->resample();
    
    // add delta of connection to kbps.
    // for next sample() of server kbps can get the stat.
    // ���ͻ��˵��շ�����ͬ����ͳ������
    kbps->add_delta(conn);
    client->stream->kbps->add_delta(conn);
    client->stream->vhost->kbps->add_delta(conn);
    
    // cleanup the delta.
    conn->cleanup();
}
// ��ͳ�����vhosts��streams���в���
SrsKbps* SrsStatistic::kbps_sample()
{
    kbps->sample();
	
    if (true) {
        std::map<int64_t, SrsStatisticVhost*>::iterator it;
        for (it = vhosts.begin(); it != vhosts.end(); it++) {
            SrsStatisticVhost* vhost = it->second;
            vhost->kbps->sample();
        }
    }
    if (true) {
        std::map<int64_t, SrsStatisticStream*>::iterator it;
        for (it = streams.begin(); it != streams.end(); it++) {
            SrsStatisticStream* stream = it->second;
            stream->kbps->sample();
        }
    }
    
    return kbps;
}

int64_t SrsStatistic::server_id()
{
    return _server_id;
}
// json����������е�vhost��Ϣ
int SrsStatistic::dumps_vhosts(stringstream& ss)
{
    int ret = ERROR_SUCCESS;

    ss << SRS_JARRAY_START;
    std::map<int64_t, SrsStatisticVhost*>::iterator it;
    for (it = vhosts.begin(); it != vhosts.end(); it++) {
        SrsStatisticVhost* vhost = it->second;
        
        if (it != vhosts.begin()) {
            ss << SRS_JFIELD_CONT;
        }
        
        if ((ret = vhost->dumps(ss)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    ss << SRS_JARRAY_END;

    return ret;
}
// json����������е�stream��Ϣ
int SrsStatistic::dumps_streams(stringstream& ss)
{
    int ret = ERROR_SUCCESS;
    
    ss << SRS_JARRAY_START;
    std::map<int64_t, SrsStatisticStream*>::iterator it;
    for (it = streams.begin(); it != streams.end(); it++) {
        SrsStatisticStream* stream = it->second;
        
        if (it != streams.begin()) {
            ss << SRS_JFIELD_CONT;
        }

        if ((ret = stream->dumps(ss)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    ss << SRS_JARRAY_END;
    
    return ret;
}
// json����������е�clients��Ϣ
int SrsStatistic::dumps_clients(stringstream& ss, int start, int count)
{
    int ret = ERROR_SUCCESS;
    
    ss << SRS_JARRAY_START;
    std::map<int, SrsStatisticClient*>::iterator it = clients.begin();
    for (int i = 0; i < start + count && it != clients.end(); it++, i++) {
        if (i < start) {
            continue;
        }
        
        SrsStatisticClient* client = it->second;
        
        if (i != start) {
            ss << SRS_JFIELD_CONT;
        }
        
        if ((ret = client->dumps(ss)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    ss << SRS_JARRAY_END;
    
    
    return ret;
}

// json����������е�clients��Ϣ
int SrsStatistic::dumps_clients(stringstream& ss)
{
    int ret = ERROR_SUCCESS;
    
    ss << SRS_JARRAY_START;
    std::map<int, SrsStatisticClient*>::iterator it = clients.begin();
    for (; it != clients.end(); it++) {

        SrsStatisticClient* client = it->second;
        
        if (it != clients.begin()) {
            ss << SRS_JFIELD_CONT;
        }
        
        if ((ret = client->dumps(ss)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    ss << SRS_JARRAY_END;
    
    
    return ret;
}

//������η��ض�Ӧ������ͳ�Ƶ�vhost�ṹ��
//����Ӧ��vhostԭ�������ڣ���new
//����Ӧ��vhost�Ѵ��ڣ���ֱ�ӷ���
SrsStatisticVhost* SrsStatistic::create_vhost(SrsRequest* req)
{
    SrsStatisticVhost* vhost = NULL;
    
    // create vhost if not exists.
    if (rvhosts.find(req->vhost) == rvhosts.end()) {
        vhost = new SrsStatisticVhost();
        vhost->vhost = req->vhost;
        rvhosts[req->vhost] = vhost;
        vhosts[vhost->id] = vhost;
        return vhost;
    }

    vhost = rvhosts[req->vhost];
    
    return vhost;
}
// ����vhostͳ����
bool SrsStatistic::destroy_vhost(std::string vhost)
{
	int64_t id;
	SrsStatisticVhost* pvhost = NULL;
	if (rvhosts.find(vhost) == rvhosts.end()) {
		return false;
    }

	pvhost = rvhosts[vhost];
	rvhosts.erase(rvhosts.find(vhost));
	vhosts.erase(vhosts.find(pvhost->id));
	srs_freep(pvhost);    
    return true;
}
//������η��ض�Ӧ������ͳ�Ƶ�stream�ṹ��
//����Ӧ��streamԭ�������ڣ���new
//����Ӧ��stream�Ѵ��ڣ���ֱ�ӷ���
SrsStatisticStream* SrsStatistic::create_stream(SrsStatisticVhost* vhost, SrsRequest* req)
{
    std::string url = req->get_stream_url();
    
    SrsStatisticStream* stream = NULL;
    
    // create stream if not exists.
    if (rstreams.find(url) == rstreams.end()) {
        stream = new SrsStatisticStream();
        stream->vhost = vhost;
        stream->stream = req->stream;
        stream->app = req->app;
        stream->url = url;
        rstreams[url] = stream;
        streams[stream->id] = stream;
        return stream;
    }
    
    stream = rstreams[url];
    
    return stream;
}
// ����streamͳ����
bool SrsStatistic::destroy_stream(std::string url)
{
	SrsStatisticStream* stream = NULL;
    if (rstreams.find(url) == rstreams.end()) {
		return false;
    }

	stream = rstreams[url];
	rstreams.erase(rstreams.find(url));
	streams.erase(streams.find(stream->id));
	srs_freep(stream);    
	return true;
}
