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

#ifndef SRS_APP_SOURCE_HPP
#define SRS_APP_SOURCE_HPP

/*
#include <srs_app_source.hpp>
*/

#include <srs_core.hpp>

#include <map>
#include <vector>
#include <string>

#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>
#include <srs_core_performance.hpp>

class SrsConsumer;
class SrsPlayEdge;
class SrsPublishEdge;
class SrsSource;
class SrsCommonMessage;
class SrsOnMetaDataPacket;
class SrsSharedPtrMessage;
class SrsForwarder;
class SrsRequest;
class SrsStSocket;
class SrsRtmpServer;
class SrsEdgeProxyContext;
class SrsMessageArray;
class SrsConnection;
#ifdef SRS_AUTO_HLS
class SrsHls;
#endif
#ifdef SRS_AUTO_DVR
class SrsDvr;
#endif
#ifdef SRS_AUTO_TRANSCODE
class SrsEncoder;
#endif
class SrsStream;
class ISrsHlsHandler;
#ifdef SRS_AUTO_HDS
class SrsHds;
#endif

/**
* the time jitter algorithm:
* 1. full, to ensure stream start at zero, and ensure stream monotonically increasing.
* 2. zero, only ensure sttream start at zero, ignore timestamp jitter.
* 3. off, disable the time jitter algorithm, like atc.
*/
enum SrsRtmpJitterAlgorithm
{
	// 时间戳从0开始，修改音视频消息时间戳，以此来达到使时间戳排序单增的目的，而不是通过调整音视频消息的发包顺序，此为配置默认值
    SrsRtmpJitterAlgorithmFULL = 0x01,
    // 时间戳从0开始，但不保证时间戳单增
    SrsRtmpJitterAlgorithmZERO,	
    // 关闭时间矫正功能
    SrsRtmpJitterAlgorithmOFF
};
int _srs_time_jitter_string2int(std::string time_jitter);

/**
* time jitter detect and correct,
* to ensure the rtmp stream is monotonically.
*/
// rtmp消息时间戳矫正类
class SrsRtmpJitter
{
private:
    int64_t last_pkt_time; // 上一个消息包未修正的时间戳，初始化为0
    int64_t last_pkt_correct_time; // 上一个音视频包修正后的时间戳，初始化为-1
public:
    SrsRtmpJitter();
    virtual ~SrsRtmpJitter();
public:
    /**
    * detect the time jitter and correct it.
    * @param ag the algorithm to use for time jitter.
    */
    virtual int correct(SrsSharedPtrMessage* msg, SrsRtmpJitterAlgorithm ag);
    /**
    * get current client time, the last packet time.
    */
    virtual int get_time();
};

#ifdef SRS_PERF_QUEUE_FAST_VECTOR
/**
* to alloc and increase fixed space,
* fast remove and insert for msgs sender.
* @see https://github.com/ossrs/srs/issues/251
*/
class SrsFastVector
{
private:
    SrsSharedPtrMessage** msgs;
    int nb_msgs;
    int count;
public:
    SrsFastVector();
    virtual ~SrsFastVector();
public:
    virtual int size();
    virtual int begin();
    virtual int end();
    virtual SrsSharedPtrMessage** data();
    virtual SrsSharedPtrMessage* at(int index);
    virtual void clear();
    virtual void erase(int _begin, int _end);
    virtual void push_back(SrsSharedPtrMessage* msg);
    virtual void free();
};
#endif

/**
* the message queue for the consumer(client), forwarder.
* we limit the size in seconds, drop old messages(the whole gop) if full.
*/
class SrsMessageQueue
{
private:
    bool _ignore_shrink;	// 决定了shrink操作的日志等级
    int64_t av_start_time; // 音视频缓存数据开始的时间戳
    int64_t av_end_time; // 音视频缓存数据结束的时间戳
    int queue_size_ms;	// 音视频缓存数据允许的最大时长
#ifdef SRS_PERF_QUEUE_FAST_VECTOR
    SrsFastVector msgs;
#else
    std::vector<SrsSharedPtrMessage*> msgs;
#endif
public:
    SrsMessageQueue(bool ignore_shrink = false);
    virtual ~SrsMessageQueue();
public:
    /**
    * get the size of queue.
    */
    virtual int size();
    /**
    * get the duration of queue.
    */
    virtual int duration();
    /**
    * set the queue size
    * @param queue_size the queue size in seconds.
    */
    virtual void set_queue_size(double queue_size);
public:
    /**
    * enqueue the message, the timestamp always monotonically.
    * @param msg, the msg to enqueue, user never free it whatever the return code.
    * @param is_overflow, whether overflow and shrinked. NULL to ignore.
    */
    virtual int enqueue(SrsSharedPtrMessage* msg, bool* is_overflow = NULL);
    /**
     * get packets in consumer queue.
     * @pmsgs SrsSharedPtrMessage*[], used to store the msgs, user must alloc it.
     * @count the count in array, output param.
     * @max_count the max count to dequeue, must be positive.
     */
    virtual int dump_packets(int max_count, SrsSharedPtrMessage** pmsgs, int& count);
    /**
    * dumps packets to consumer, use specified args.
    * @remark the atc/tba/tbv/ag are same to SrsConsumer.enqueue().
    */
    virtual int dump_packets(SrsConsumer* consumer, bool atc, SrsRtmpJitterAlgorithm ag);
private:
    /**
    * remove a gop from the front.
    * if no iframe found, clear it.
    */
    virtual void shrink();
public:
    /**
     * clear all messages in queue.
     */
    virtual void clear();
};

/**
 * the wakable used for some object
 * which is waiting on cond.
 */
class ISrsWakable
{
public:
    ISrsWakable();
    virtual ~ISrsWakable();
public:
    /**
     * when the consumer(for player) got msg from recv thread,
     * it must be processed for maybe it's a close msg, so the cond
     * wait must be wakeup.
     */
    virtual void wakeup() = 0;
};

/**
* the consumer for SrsSource, that is a play client.
*/
class SrsConsumer : public ISrsWakable
{
private:
    SrsRtmpJitter* jitter;
    SrsSource* source;
    SrsMessageQueue* queue;
    // the owner connection for debug, maybe NULL.
    SrsConnection* conn;
	// 是否暂停标志位
    bool paused;
    // when source id changed, notice all consumers
    bool should_update_source_id;
	// 是否需要发送playSourceInvalid消息给play客户端
	bool if_need_send_playSourceInvalid;
	// 是否已经发送playSourceInvalid消息给play客户端
	bool is_send_playSourceInvalid;
#ifdef SRS_PERF_QUEUE_COND_WAIT
    // the cond wait for mw.
    // @see https://github.com/ossrs/srs/issues/251
    st_cond_t mw_wait;
    bool mw_waiting;
    int mw_min_msgs;
    int mw_duration;
#endif
public:
    SrsConsumer(SrsSource* s, SrsConnection* c);
    virtual ~SrsConsumer();
public:
    /**
    * set the size of queue.
    */
    virtual void set_queue_size(double queue_size);
    /**
    * when source id changed, notice client to print.
    */
    virtual void update_source_id();
public:
    /**
    * get current client time, the last packet time.
    */
    virtual int get_time();
    /**
    * enqueue an shared ptr message.
    * @param shared_msg, directly ptr, copy it if need to save it.
    * @param whether atc, donot use jitter correct if true.
    * @param ag the algorithm of time jitter.
    */
    virtual int enqueue(SrsSharedPtrMessage* shared_msg, bool atc, SrsRtmpJitterAlgorithm ag);
    /**
     * get packets in consumer queue.
     * @param msgs the msgs array to dump packets to send.
     * @param count the count in array, intput and output param.
     * @remark user can specifies the count to get specified msgs; 0 to get all if possible.
     */
    virtual int dump_packets(SrsMessageArray* msgs, int& count);
#ifdef SRS_PERF_QUEUE_COND_WAIT
    /**
    * wait for messages incomming, atleast nb_msgs and in duration.
    * @param nb_msgs the messages count to wait.
    * @param duration the messgae duration to wait.
    */
    virtual void wait(int nb_msgs, int duration);
#endif
    /**
    * when client send the pause message.
    */
    virtual int on_play_client_pause(bool is_pause);
// ISrsWakable
public:
    /**
     * when the consumer(for player) got msg from recv thread,
     * it must be processed for maybe it's a close msg, so the cond
     * wait must be wakeup.
     */
    virtual void wakeup();
	/**
    * set the size of queue.
    */
    virtual void set_if_need_send_playSourceInvalid(bool flag);
	/**
    * set the size of queue.
    */
    virtual bool get_if_need_send_playSourceInvalid();
	/**
    * set the size of queue.
    */
    virtual void set_is_send_playSourceInvalid(bool flag);
	/**
    * set the size of queue.
    */
    virtual bool get_is_send_playSourceInvalid();
};

/**
* cache a gop of video/audio data,
* delivery at the connect of flash player,
* to enable it to fast startup.
*/
class SrsGopCache
{
private:
    /**
    * if disabled the gop cache,
    * the client will wait for the next keyframe for h264,
    * and will be black-screen.
    */
    // gop功能配置是否打开
    bool enable_gop_cache;
    /**
    * the video frame count, avoid cache for pure audio stream.
    */
    // gop缓存中视屏消息个数统计
    int cached_video_count;
    /**
    * when user disabled video when publishing, and gop cache enalbed,
    * we will cache the audio/video for we already got video, but we never
    * know when to clear the gop cache, for there is no video in future,
    * so we must guess whether user disabled the video.
    * when we got some audios after laster video, for instance, 600 audio packets,
    * about 3s(26ms per packet) 115 audio packets, clear gop cache.
    * 
    * @remark, it is ok for performance, for when we clear the gop cache,
    *       gop cache is disabled for pure audio stream.
    * @see: https://github.com/ossrs/srs/issues/124
    */
    // 最后一个视频消息后面连续收到音频消息个数统计
    // 一旦收到一个视频消息，该数据清零
    int audio_after_last_video_count;
    /**
    * cached gop.
    */
    std::vector<SrsSharedPtrMessage*> gop_cache;
public:
    SrsGopCache();
    virtual ~SrsGopCache();
public:
    /**
     * cleanup when system quit.
     */
    virtual void dispose();
    /**
    * to enable or disable the gop cache.
    */
    virtual void set(bool enabled);
    /**
    * only for h264 codec
    * 1. cache the gop when got h264 video packet.
    * 2. clear gop when got keyframe.
    * @param shared_msg, directly ptr, copy it if need to save it.
    */
    virtual int cache(SrsSharedPtrMessage* shared_msg);
    /**
    * clear the gop cache.
    */
    virtual void clear();
    /**
    * dump the cached gop to consumer.
    */
    virtual int dump(SrsConsumer* consumer, bool atc, SrsRtmpJitterAlgorithm jitter_algorithm);
    /**
    * used for atc to get the time of gop cache,
    * the atc will adjust the sequence header timestamp to gop cache.
    */
    virtual bool empty();
    /**
    * get the start time of gop cache, in ms.
    * @return 0 if no packets.
    */
    virtual int64_t start_time();
    /**
    * whether current stream is pure audio,
    * when no video in gop cache, the stream is pure audio right now.
    */
    virtual bool pure_audio();
};

/**
* the handler to handle the event of srs source.
* for example, the http flv streaming module handle the event and 
* mount http when rtmp start publishing.
*/
class ISrsSourceHandler
{
public:
    ISrsSourceHandler();
    virtual ~ISrsSourceHandler();
public:
    /**
    * when stream start publish, mount stream.
    */
    virtual int on_publish(SrsSource* s, SrsRequest* r) = 0;
    /**
    * when stream stop publish, unmount stream.
    */
    virtual void on_unpublish(SrsSource* s, SrsRequest* r) = 0;
};

/**
 * the mix queue to correct the timestamp for mix_correct algorithm.
 */
class SrsMixQueue
{
private:
    u_int32_t nb_videos;
    u_int32_t nb_audios;
    std::multimap<int64_t, SrsSharedPtrMessage*> msgs;
public:
    SrsMixQueue();
    virtual ~SrsMixQueue();
public:
    virtual void clear();
    virtual void push(SrsSharedPtrMessage* msg);
    virtual SrsSharedPtrMessage* pop();
};

/**
* live streaming source.
*/
//直播流资源类
class SrsSource : public ISrsReloadHandler
{
private:
    static std::map<std::string, SrsSource*> pool;//全局资源池，当资源类处于不使用状态30秒后，会被清除
public:
    /**
    * find stream by vhost/app/stream.
    * @param r the client request.
    * @param h the event handler for source.
    * @param hh the event handler for hls.
    * @param pps the matched source, if success never be NULL.
    */
    // 申请一个新的资源
    static int create(SrsRequest* r, ISrsSourceHandler* h, ISrsHlsHandler* hh, SrsSource** pps);
    /**
    * get the exists source, NULL when not exists.
    * update the request and return the exists source.
    */
    // 根据srs请求寻找相应的source资源
    static SrsSource* fetch(SrsRequest* r);
    /**
    * get the exists source by stream info(vhost, app, stream), NULL when not exists.
    */
    static SrsSource* fetch(std::string vhost, std::string app, std::string stream);
    /**
     * dispose and cycle all sources.
     */
    static void dispose_all();
    static int cycle_all();
private:
    static int do_cycle_all();
public:
    /**
    * when system exit, destroy the sources,
    * for gmc to analysis mem leaks.
    */
    static void destroy();
private:
    // source id,
    // for publish, it's the publish client id.
    // for edge, it's the edge ingest id.
    // when source id changed, for example, the edge reconnect,
    // invoke the on_source_id_changed() to let all clients know.
    int _source_id;
    // previous source id.
    int _pre_source_id;
    // deep copy of client request.
    // 该值会不断更新，总为最近使用到该资源的客户端的req
    SrsRequest* _req;
    // to delivery stream to clients.
    std::vector<SrsConsumer*> consumers; //消费者，该直播流收到数据后需要转发这些play客户端
    // the time jitter algorithm for vhost.
    // 该配置会修改音视频消息时间戳，以此来达到使时间戳排序单增的目的，而不是通过调整音视频消息的发包顺序
    SrsRtmpJitterAlgorithm jitter_algorithm;
    // whether use interlaced/mixed algorithm to correct timestamp.
    // 是否使用交错/混合算法去调整音视频消息的处理顺序，以此来达到使时间戳排序单增的目的
    // 该配置并不会修改消息的时间戳，只是调整处理的顺序
    bool mix_correct;
    SrsMixQueue* mix_queue;
    // whether stream is monotonically increase.
    // 音视频流时间戳是否单增标志位，即流单增标志位
    bool is_monotonically_increase;
	// 最近收到的一个消息的未矫正前的真实时间戳
    int64_t last_packet_time;
    // hls handler.
#ifdef SRS_AUTO_HLS
    SrsHls* hls;
#endif
    // dvr handler.
#ifdef SRS_AUTO_DVR
    SrsDvr* dvr;
#endif
    // transcoding handler.
#ifdef SRS_AUTO_TRANSCODE
    SrsEncoder* encoder;
#endif
#ifdef SRS_AUTO_HDS
    SrsHds *hds;
#endif
    // edge control service
    // 边缘播放类，里面会启动向源站拉流的线程
    SrsPlayEdge* play_edge;
	// 边缘推流到源
    SrsPublishEdge* publish_edge;
    // gop cache for client fast startup.
    // gop缓存类，用于实现客户端秒开
    SrsGopCache* gop_cache;
    // to forward stream to other servers
    std::vector<SrsForwarder*> forwarders;
    // for aggregate message
    SrsStream* aggregate_stream;
    // the event handler.
    // 一般指向这个类SrsServer
    ISrsSourceHandler* handler;
private:
    /**
    * can publish, true when is not streaming
    */
    bool _can_publish;
    /**
    * atc whether atc(use absolute time and donot adjust time),
    * directly use msg time and donot adjust if atc is true,
    * otherwise, adjust msg time to start from 0 to make flash happy.
    */
    // TODO: FIXME: to support reload atc.
    bool atc;
    // last die time, when all consumers quit and no publisher,
    // we will remove the source when source die.
    int64_t die_at;
private:
    SrsSharedPtrMessage* cache_metadata;
    // the cached video sequence header.
    SrsSharedPtrMessage* cache_sh_video;
    // the cached audio sequence header.
    // 用于缓存音频序号头
    SrsSharedPtrMessage* cache_sh_audio;
public:
    SrsSource();
    virtual ~SrsSource();
public:
    virtual void dispose();
    virtual int cycle();
    // remove source when expired.
    virtual bool expired();
// initialize, get and setter.
public:
    /**
    * initialize the hls with handlers.
    */
    virtual int initialize(SrsRequest* r, ISrsSourceHandler* h, ISrsHlsHandler* hh);
// interface ISrsReloadHandler
public:
    virtual int on_reload_vhost_atc(std::string vhost);
    virtual int on_reload_vhost_gop_cache(std::string vhost);
    virtual int on_reload_vhost_queue_length(std::string vhost);
    virtual int on_reload_vhost_time_jitter(std::string vhost);
    virtual int on_reload_vhost_mix_correct(std::string vhost);
    virtual int on_reload_vhost_forward(std::string vhost);
    virtual int on_reload_vhost_hls(std::string vhost);
    virtual int on_reload_vhost_hds(std::string vhost);
    virtual int on_reload_vhost_dvr(std::string vhost);
    virtual int on_reload_vhost_transcode(std::string vhost);
	virtual int on_reload_vhost_origin(std::string vhost);
// for the tools callback
public:
    // for the SrsForwarder to callback to request the sequence headers.
    virtual int on_forwarder_start(SrsForwarder* forwarder);
    // for the SrsHls to callback to request the sequence headers.
    virtual int on_hls_start();
    // for the SrsDvr to callback to request the sequence headers.
    virtual int on_dvr_request_sh();
    // source id changed.
    virtual int on_source_id_changed(int id);
    // get current source id.
    virtual int source_id();
    virtual int pre_source_id();
// logic data methods
public:
    virtual bool can_publish(bool is_edge);
    virtual int on_meta_data(SrsCommonMessage* msg, SrsOnMetaDataPacket* metadata);
public:
    virtual int on_audio(SrsCommonMessage* audio);
private:
    virtual int on_audio_imp(SrsSharedPtrMessage* audio);
public:
    virtual int on_video(SrsCommonMessage* video);
private:
    virtual int on_video_imp(SrsSharedPtrMessage* video);
public:
    virtual int on_aggregate(SrsCommonMessage* msg);
    /**
    * publish stream event notify.
    * @param _req the request from client, the source will deep copy it,
    *         for when reload the request of client maybe invalid.
    */
    virtual int on_publish();
    virtual void on_unpublish();
// consumer methods
public:
    /**
    * create consumer and dumps packets in cache.
    * @param consumer, output the create consumer.
    * @param ds, whether dumps the sequence header.
    * @param dm, whether dumps the metadata.
    * @param dg, whether dumps the gop cache.
    */
    virtual int create_consumer(
        SrsConnection* conn, SrsConsumer*& consumer,
        bool ds = true, bool dm = true, bool dg = true
    );
    virtual void on_consumer_destroy(SrsConsumer* consumer);
    virtual void set_cache(bool enabled);
    virtual SrsRtmpJitterAlgorithm jitter();
// internal
public:
    // for edge, when publish edge stream, check the state
    virtual int on_edge_start_publish();
    // for edge, proxy the publish
    virtual int on_edge_proxy_publish(SrsCommonMessage* msg);
    // for edge, proxy stop publish
    virtual void on_edge_proxy_unpublish();
private:
    virtual int create_forwarders();
    virtual void destroy_forwarders();

public:
    virtual int send_play_source_invalid();

};

#endif
