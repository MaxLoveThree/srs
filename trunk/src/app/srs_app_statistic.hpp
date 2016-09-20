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

#ifndef SRS_APP_STATISTIC_HPP
#define SRS_APP_STATISTIC_HPP

/*
#include <srs_app_statistic.hpp>
*/

#include <srs_core.hpp>

#include <map>
#include <set>
#include <string>

#include <srs_kernel_codec.hpp>
#include <srs_rtmp_stack.hpp>

class SrsKbps;
class SrsRequest;
class SrsConnection;

struct SrsStatisticVhost
{
public:
    int64_t id;//虚拟主机id号
    std::string vhost;//虚拟主机名字
    int nb_streams;//该虚拟主机下有多少个流
    int nb_clients;//该虚拟主机下有多少个客户端，即推送/拉取该vhost的客户端总和
public:
    /**
    * vhost total kbps.
    */
    SrsKbps* kbps;//虚拟主机流量统计类
public:
    SrsStatisticVhost();
    virtual ~SrsStatisticVhost();
public:
    virtual int dumps(std::stringstream& ss);
};

struct SrsStatisticStream
{
public:
    int64_t id;//stream id
    SrsStatisticVhost* vhost;//stream 所属的vhost对应的统计类
    std::string app;//stream对应的app名字
    std::string stream;//stream名字
    std::string url;//stream对应的url名字
    bool active;//该标识表示是否有客户端向服务器请求该流
    int connection_cid;//第一个play的用户id
    int nb_clients;//使用该stream的客户端总和，即推送/拉取该stream的客户端数量总和
	std::set<int> play_cid_set;//期望play的客户端id
	int64_t publish_start_time;// 推流开始时间
public:
    /**
    * stream total kbps.
    */
    SrsKbps* kbps;
public:
    bool has_video;
    SrsCodecVideo vcodec;
    // profile_idc, H.264-AVC-ISO_IEC_14496-10.pdf, page 45.
    SrsAvcProfile avc_profile;
    // level_idc, H.264-AVC-ISO_IEC_14496-10.pdf, page 45.
    SrsAvcLevel avc_level;
public:
    bool has_audio;
    SrsCodecAudio acodec;
    SrsCodecAudioSampleRate asample_rate;
    SrsCodecAudioSoundType asound_type;
    /**
    * audio specified
    * audioObjectType, in 1.6.2.1 AudioSpecificConfig, page 33,
    * 1.5.1.1 Audio object type definition, page 23,
    *           in aac-mp4a-format-ISO_IEC_14496-3+2001.pdf.
    */
    SrsAacObjectType aac_object;
public:
    SrsStatisticStream();
    virtual ~SrsStatisticStream();
public:
    virtual int dumps(std::stringstream& ss);
public:
    /**
    * publish the stream.
    */
    virtual void publish(int cid);
    /**
    * close the stream.
    */
    virtual void close();
};

struct SrsStatisticClient
{
public:
    SrsStatisticStream* stream;//客户端推送/拉取的stream对应的统计类
    SrsConnection* conn;//客户端连接类
    SrsRequest* req;//客户端req消息
    SrsRtmpConnType type;//客户端类型
    int id;	//客户端所属的st线程id
    int64_t create;
	bool	kick;	//客户端是否等待被踢除，便于页面处理
public:
    SrsStatisticClient();
    virtual ~SrsStatisticClient();
public:
    virtual int dumps(std::stringstream& ss);
};
//信息统计类
class SrsStatistic
{
private:
	//类实例
    static SrsStatistic *_instance;
    // the id to identify the sever.
    int64_t _server_id;
private:
    // key: vhost id, value: vhost object.
    //当前客户端推流/拉流使用到的所有的vhost统计类
    std::map<int64_t, SrsStatisticVhost*> vhosts;
    // key: vhost url, value: vhost Object.
    // @remark a fast index for vhosts.
    //这个只是为了快速返回添加的
    std::map<std::string, SrsStatisticVhost*> rvhosts;
private:
    // key: stream id, value: stream Object.
    //但钱客户端推流/拉流使用到的所有的stream统计类
    std::map<int64_t, SrsStatisticStream*> streams;
    // key: stream url, value: stream Object.
    // @remark a fast index for streams.
    //这个只是为了快速返回添加的
    std::map<std::string, SrsStatisticStream*> rstreams;
private:
    // key: client id, value: stream object.
    //当前客户端推流/拉流使用到的所有的client统计类
    std::map<int, SrsStatisticClient*> clients;
    // server total kbps.
    SrsKbps* kbps;
private:
    SrsStatistic();
    virtual ~SrsStatistic();
public:
	// 获取统计类实例指针
    static SrsStatistic* instance();
public:
	// 根据id寻找vhost统计类，内部不会主动创建新的
    virtual SrsStatisticVhost* find_vhost(int vid);
	// 根据id寻找stream统计类，内部不会主动创建新的
    virtual SrsStatisticStream* find_stream(int sid);
	// 根据id寻找client统计类，内部不会主动创建新的
    virtual SrsStatisticClient* find_client(int cid);
public:
    /**
    * when got video info for stream.
    */
    virtual int on_video_info(SrsRequest* req, 
        SrsCodecVideo vcodec, SrsAvcProfile avc_profile, SrsAvcLevel avc_level
    );
    /**
    * when got audio info for stream.
    */
    virtual int on_audio_info(SrsRequest* req,
        SrsCodecAudio acodec, SrsCodecAudioSampleRate asample_rate, SrsCodecAudioSoundType asound_type,
        SrsAacObjectType aac_object
    );
    /**
     * when publish stream.
     * @param req the request object of publish connection.
     * @param cid the cid of publish connection.
     */
    virtual void on_stream_publish(SrsRequest* req, int cid);
    /**
    * when close stream.
    */
    virtual void on_stream_close(SrsRequest* req);
public:
    /**
     * when got a client to publish/play stream,
     * @param id, the client srs id.
     * @param req, the client request object.
     * @param conn, the physical absract connection object.
     * @param type, the type of connection.
     */
    virtual int on_client(int id, SrsRequest* req, SrsConnection* conn, SrsRtmpConnType type);
    /**
     * client disconnect
     * @remark the on_disconnect always call, while the on_client is call when
     *      only got the request object, so the client specified by id maybe not
     *      exists in stat.
     */
    virtual void on_disconnect(int id);
    /**
    * sample the kbps, add delta bytes of conn.
    * use kbps_sample() to get all result of kbps stat.
    */
    // TODO: FIXME: the add delta must use IKbpsDelta interface instead.
    virtual void kbps_add_delta(SrsConnection* conn);
    /**
    * calc the result for all kbps.
    * @return the server kbps.
    */
    virtual SrsKbps* kbps_sample();
public:
    /**
    * get the server id, used to identify the server.
    * for example, when restart, the server id must changed.
    */
    virtual int64_t server_id();
    /**
    * dumps the vhosts to sstream in json.
    */
    virtual int dumps_vhosts(std::stringstream& ss);
    /**
    * dumps the streams to sstream in json.
    */
    virtual int dumps_streams(std::stringstream& ss);
    /**
     * dumps the clients to sstream in json.
     * @param start the start index, from 0.
     * @param count the max count of clients to dump.
     */
    virtual int dumps_clients(std::stringstream& ss, int start, int count);
	virtual int dumps_clients(std::stringstream& ss);
private:
    virtual SrsStatisticVhost* create_vhost(SrsRequest* req);
	virtual bool destroy_vhost(std::string vhost);
    virtual SrsStatisticStream* create_stream(SrsStatisticVhost* vhost, SrsRequest* req);
	virtual bool destroy_stream(std::string url);
};

#endif
