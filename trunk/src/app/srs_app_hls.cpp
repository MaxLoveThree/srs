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

#include <srs_app_hls.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <algorithm>
#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_rtmp_amf0.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_protocol_buffer.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_http_hooks.hpp>

// drop the segment when duration of ts too small.
#define SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS 100
// when hls timestamp jump, reset it.
#define SRS_AUTO_HLS_SEGMENT_TIMESTAMP_JUMP_MS 300

// fragment plus the deviation percent.
#define SRS_HLS_FLOOR_REAP_PERCENT 0.3
// reset the piece id when deviation overflow this.
#define SRS_JUMP_WHEN_PIECE_DEVIATION 20

/**
 * * the HLS section, only available when HLS enabled.
 * */
#ifdef SRS_AUTO_HLS

SrsHlsCacheWriter::SrsHlsCacheWriter(bool write_cache, bool write_file)
{
    should_write_cache = write_cache;
    should_write_file = write_file;
}

SrsHlsCacheWriter::~SrsHlsCacheWriter()
{
}

int SrsHlsCacheWriter::open(string file)
{
    if (!should_write_file) {
        return ERROR_SUCCESS;
    }

    return impl.open(file);
}

void SrsHlsCacheWriter::close()
{
    if (!should_write_file) {
        return;
    }

    impl.close();
}

bool SrsHlsCacheWriter::is_open()
{
    if (!should_write_file) {
        return true;
    }

    return impl.is_open();
}

int64_t SrsHlsCacheWriter::tellg()
{
    if (!should_write_file) {
        return 0;
    }

    return impl.tellg();
}

int SrsHlsCacheWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    if (should_write_cache) {
        if (count > 0) {
            data.append((char*)buf, count);
        }
    }

    if (should_write_file) {
        return impl.write(buf, count, pnwrite);
    }

    return ERROR_SUCCESS;
}

string SrsHlsCacheWriter::cache()
{
    return data;
}

SrsHlsSegment::SrsHlsSegment(SrsTsContext* c, bool write_cache, bool write_file, SrsCodecAudio ac, SrsCodecVideo vc)
{
    duration = 0;
    sequence_no = 0;
    segment_start_dts = 0;
    is_sequence_header = false;
	status = SegmentStatus_Normal;
    writer = new SrsHlsCacheWriter(write_cache, write_file);
    muxer = new SrsTSMuxer(writer, c, ac, vc);
}

SrsHlsSegment::~SrsHlsSegment()
{
    srs_freep(muxer);
    srs_freep(writer);
}
// 更新切片内容持续时间
void SrsHlsSegment::update_duration(int64_t current_frame_dts)
{
    // we use video/audio to update segment duration,
    // so when reap segment, some previous audio frame will
    // update the segment duration, which is nagetive,
    // just ignore it.
    // 当前时间小于开始时间，无视
    if (current_frame_dts < segment_start_dts) {
        // for atc and timestamp jump, reset the start dts.
        if (current_frame_dts < segment_start_dts - SRS_AUTO_HLS_SEGMENT_TIMESTAMP_JUMP_MS * 90) {
            srs_warn("hls timestamp jump %"PRId64"=>%"PRId64, segment_start_dts, current_frame_dts);
            segment_start_dts = current_frame_dts;
        }
        return;
    }
    
    duration = (current_frame_dts - segment_start_dts) / 90000.0;
    srs_assert(duration >= 0);
    
    return;
}

SrsDvrAsyncCallOnHls::SrsDvrAsyncCallOnHls(int c, SrsRequest* r, string p, string t, string m, string mu, int s, double d)
{
    req = r->copy();
    cid = c;
    path = p;
    ts_url = t;
    m3u8 = m;
    m3u8_url = mu;
    seq_no = s;
    duration = d;
}

SrsDvrAsyncCallOnHls::~SrsDvrAsyncCallOnHls()
{
    srs_freep(req);
}

int SrsDvrAsyncCallOnHls::call()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_CALLBACK
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return ret;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_hls(req->vhost);
        
        if (!conf) {
            srs_info("ignore the empty http callback: on_hls");
            return ret;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((ret = SrsHttpHooks::on_hls(cid, url, req, path, ts_url, m3u8, m3u8_url, seq_no, duration)) != ERROR_SUCCESS) {
            srs_error("hook client on_hls failed. url=%s, ret=%d", url.c_str(), ret);
            return ret;
        }
    }
#endif
    
    return ret;
}

string SrsDvrAsyncCallOnHls::to_string()
{
    return "on_hls: " + path;
}

SrsDvrAsyncCallOnHlsNotify::SrsDvrAsyncCallOnHlsNotify(int c, SrsRequest* r, string u)
{
    cid = c;
    req = r->copy();
    ts_url = u;
}

SrsDvrAsyncCallOnHlsNotify::~SrsDvrAsyncCallOnHlsNotify()
{
    srs_freep(req);
}

int SrsDvrAsyncCallOnHlsNotify::call()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_CALLBACK
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return ret;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_hls_notify(req->vhost);
        
        if (!conf) {
            srs_info("ignore the empty http callback: on_hls_notify");
            return ret;
        }
        
        hooks = conf->args;
    }
    
    int nb_notify = _srs_config->get_vhost_hls_nb_notify(req->vhost);
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((ret = SrsHttpHooks::on_hls_notify(cid, url, req, ts_url, nb_notify)) != ERROR_SUCCESS) {
            srs_error("hook client on_hls_notify failed. url=%s, ret=%d", url.c_str(), ret);
            return ret;
        }
    }
#endif
    
    return ret;
}

string SrsDvrAsyncCallOnHlsNotify::to_string()
{
    return "on_hls_notify: " + ts_url;
}

SrsHlsMuxer::SrsHlsMuxer()
{
    req = NULL;
    hls_fragment = hls_window = 0;
    hls_aof_ratio = 1.0;
    deviation_ts = 0;
    hls_cleanup = true;
    hls_wait_keyframe = true;
    previous_floor_ts = 0;
    accept_floor_ts = 0;
    hls_ts_floor = false;
    max_td = 0;
    _sequence_no = 0;
    current = NULL;
    acodec = SrsCodecAudioReserved1;
    should_write_cache = false;
    should_write_file = true;
    async = new SrsAsyncCallWorker();
    context = new SrsTsContext();
	memset(&hls_publish_time, 0, sizeof(hls_publish_time));
}

SrsHlsMuxer::~SrsHlsMuxer()
{
    std::vector<SrsHlsSegment*>::iterator it;
	// 清理过期的ts切片信息容器
	for (it = expired_segments.begin(); it != expired_segments.end(); ++it) {
        SrsHlsSegment* expired_segment = *it;
        srs_freep(expired_segment);
    }
    expired_segments.clear();
	
    for (it = segments.begin(); it != segments.end(); ++it) {
        SrsHlsSegment* segment = *it;
        srs_freep(segment);
    }
    segments.clear();
    
    srs_freep(current);
    srs_freep(req);
    srs_freep(async);
    srs_freep(context);
}
// 清除所有ts, m3u8缓存文件
// 当hls_dispose配置不为0时，推流停止hls_dispose时间后，会调用该接口
void SrsHlsMuxer::dispose()
{
	// 是否允许写文件，一般都是true
    if (should_write_file) {
        std::vector<SrsHlsSegment*>::iterator it;
		// 清理过期的ts切片信息保存容器
        for (it = expired_segments.begin(); it != expired_segments.end(); ++it) {
            SrsHlsSegment* expired_segment = *it;
			if ((expired_segment->status != SegmentStatus_Vod) && (expired_segment->status != SegmentStatus_Vod_Left))
			{
				if (unlink(expired_segment->full_path.c_str()) < 0) {
					srs_warn("dispose unlink path failed, file=%s.", expired_segment->full_path.c_str());
				}
			}
            srs_freep(expired_segment);
        }
        expired_segments.clear();
		
		// 删除m3u8文件中记录的ts文件
        for (it = segments.begin(); it != segments.end(); ++it) {
            SrsHlsSegment* segment = *it;
			// 如果配置了点播的vod m3u8路径，则不删除ts切片
			if ((segment->status != SegmentStatus_Vod) && (segment->status != SegmentStatus_Vod_Left))
			{
				if (unlink(segment->full_path.c_str()) < 0) {
					srs_warn("dispose unlink path failed, file=%s.", segment->full_path.c_str());
				}
			}
			
            srs_freep(segment);
        }
        segments.clear();
        
        if (current) {
            std::string path = current->full_path + ".tmp";
			// 删除ts 临时文件
            if (unlink(path.c_str()) < 0) {
                srs_warn("dispose unlink path failed, file=%s", path.c_str());
            }
            srs_freep(current);
        }
        // 删除m3u8文件
        if (unlink(m3u8.c_str()) < 0) {
            srs_warn("dispose unlink path failed. file=%s", m3u8.c_str());
        }
    }
    
    // TODO: FIXME: support hls dispose in HTTP cache.
    
    srs_trace("gracefully dispose hls %s", req? req->get_stream_url().c_str() : "");
}

void SrsHlsMuxer::update_segments_status()
{
	// 是否允许写文件，一般都是true
    if (should_write_file) {
        std::vector<SrsHlsSegment*>::iterator it;
		// 清理过期的ts切片信息保存容器
        for (it = expired_segments.begin(); it != expired_segments.end(); ++it) {
            SrsHlsSegment* expired_segment = *it;
			if (expired_segment->status == SegmentStatus_Vod)
			{
				expired_segment->status = SegmentStatus_Vod_Left;
			}
        }

		for (it = segments.begin(); it != segments.end(); ++it) {
            SrsHlsSegment* segment = *it;
			if (segment->status == SegmentStatus_Vod)
			{
				segment->status = SegmentStatus_Vod_Left;
			}			
        }
    }
    
    // TODO: FIXME: support hls dispose in HTTP cache.
    
    srs_trace("updata_segments_status %s", req? req->get_stream_url().c_str() : "");
}

int SrsHlsMuxer::sequence_no()
{
    return _sequence_no;
}

string SrsHlsMuxer::ts_url()
{
    return current? current->uri:"";
}

double SrsHlsMuxer::duration()
{
    return current? current->duration:0;
}

int SrsHlsMuxer::deviation()
{
    // no floor, no deviation.
    if (!hls_ts_floor) {
        return 0;
    }
    
    return deviation_ts;
}

int SrsHlsMuxer::initialize()
{
    int ret = ERROR_SUCCESS;
    if ((ret = async->start()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsHlsMuxer::update_config(SrsRequest* r, string entry_prefix,
    string path, string m3u8_file, string vod_m3u8_file, string ts_file, double fragment, double window,
    bool ts_floor, double aof_ratio, bool cleanup, bool wait_keyframe, timeval publish_time
) {
    int ret = ERROR_SUCCESS;
    
    srs_freep(req);
    req = r->copy();

    hls_entry_prefix = entry_prefix;
    hls_path = path;
    hls_ts_file = ts_file;
    hls_fragment = fragment;
    hls_aof_ratio = aof_ratio;
    hls_ts_floor = ts_floor;
    hls_cleanup = cleanup;
    hls_wait_keyframe = wait_keyframe;
    previous_floor_ts = 0;
    accept_floor_ts = 0;
    hls_window = window;
    deviation_ts = 0;
    hls_publish_time = publish_time;

    // generate the m3u8 dir and path.
    m3u8_url = srs_path_build_stream(m3u8_file, req->vhost, req->app, req->stream);
    m3u8 = path + "/" + m3u8_url;
    // when update config, reset the history target duration.
    max_td = (int)(fragment * _srs_config->get_hls_td_ratio(r->vhost));
    
    // TODO: FIXME: refine better for SRS2 only support disk.
    should_write_cache = false;
    should_write_file = true;
    
    // create m3u8 dir once.
    m3u8_dir = srs_path_dirname(m3u8);
    if (should_write_file && (ret = srs_create_dir_recursively(m3u8_dir)) != ERROR_SUCCESS) {
        srs_error("create app dir %s failed. ret=%d", m3u8_dir.c_str(), ret);
        return ret;
    }
    srs_info("create m3u8 dir %s ok", m3u8_dir.c_str());
	// 先将vod_m3u8_file赋值给vod_m3u8_url，否则is_vod_enabled接口判断有问题，总为false
	vod_m3u8_url = vod_m3u8_file;
	// 根据配置判断是否创建 vod m3u8文件保存目录
	if (is_vod_enabled())
	{
		vod_m3u8_url = srs_path_build_stream(vod_m3u8_url, req->vhost, req->app, req->stream);
		vod_m3u8_url = srs_path_build_timestamp(vod_m3u8_url, TimestampType_StreamStart, &hls_publish_time);
		// 生成点播m3u8文件保存路径
		vod_m3u8 = path + "/" + vod_m3u8_url;
		// 生成点播m3u8文件保存目录
		vod_m3u8_dir = srs_path_dirname(vod_m3u8);
		// 创建点播m3u8文件保存目录
	    if (should_write_file && (ret = srs_create_dir_recursively(vod_m3u8_dir)) != ERROR_SUCCESS) {
	        srs_error("create app vod m3u8 dir %s failed. ret=%d", vod_m3u8_dir.c_str(), ret);
	        return ret;
	    }
	    srs_info("create vod m3u8 dir %s ok", vod_m3u8_dir.c_str());
	}
    return ret;
}

int SrsHlsMuxer::segment_open(int64_t segment_start_dts)
{
    int ret = ERROR_SUCCESS;
    
    if (current) {
        srs_warn("ignore the segment open, for segment is already open.");
        return ret;
    }
    
    // when segment open, the current segment must be NULL.
    srs_assert(!current);

    // load the default acodec from config.
    // hls 切片音频类型获取
    SrsCodecAudio default_acodec = SrsCodecAudioAAC;
    if (true) {
        std::string default_acodec_str = _srs_config->get_hls_acodec(req->vhost);
        if (default_acodec_str == "mp3") {
            default_acodec = SrsCodecAudioMP3;
            srs_info("hls: use default mp3 acodec");
        } else if (default_acodec_str == "aac") {
            default_acodec = SrsCodecAudioAAC;
            srs_info("hls: use default aac acodec");
        } else if (default_acodec_str == "an") {
            default_acodec = SrsCodecAudioDisabled;
            srs_info("hls: use default an acodec for pure video");
        } else {
            srs_warn("hls: use aac for other codec=%s", default_acodec_str.c_str());
        }
    }

    // load the default vcodec from config.
    // hls 切片视频类型获取
    SrsCodecVideo default_vcodec = SrsCodecVideoAVC;
    if (true) {
        std::string default_vcodec_str = _srs_config->get_hls_vcodec(req->vhost);
        if (default_vcodec_str == "h264") {
            default_vcodec = SrsCodecVideoAVC;
            srs_info("hls: use default h264 vcodec");
        } else if (default_vcodec_str == "vn") {
            default_vcodec = SrsCodecVideoDisabled;
            srs_info("hls: use default vn vcodec for pure audio");
        } else {
            srs_warn("hls: use h264 for other codec=%s", default_vcodec_str.c_str());
        }
    }
    
    // new segment.
    current = new SrsHlsSegment(context, should_write_cache, should_write_file, default_acodec, default_vcodec);
    current->sequence_no = _sequence_no++;
    current->segment_start_dts = segment_start_dts;
    current->status = is_vod_enabled() ? SegmentStatus_Vod : SegmentStatus_Normal;
    // generate filename.
    std::string ts_file = hls_ts_file;
    ts_file = srs_path_build_stream(ts_file, req->vhost, req->app, req->stream);
    if (hls_ts_floor) {
        // accept the floor ts for the first piece.
        int64_t current_floor_ts = (int64_t)(srs_update_system_time_ms() / (1000 * hls_fragment));
        if (!accept_floor_ts) {
            accept_floor_ts = current_floor_ts - 1;
        } else {
            accept_floor_ts++;
        }
        
        // jump when deviation more than 10p
        if (accept_floor_ts - current_floor_ts > SRS_JUMP_WHEN_PIECE_DEVIATION) {
            srs_warn("hls: jmp for ts deviation, current=%"PRId64", accept=%"PRId64, current_floor_ts, accept_floor_ts);
            accept_floor_ts = current_floor_ts - 1;
        }
        
        // when reap ts, adjust the deviation.
        deviation_ts = (int)(accept_floor_ts - current_floor_ts);
        
        // dup/jmp detect for ts in floor mode.
        if (previous_floor_ts && previous_floor_ts != current_floor_ts - 1) {
            srs_warn("hls: dup/jmp ts, previous=%"PRId64", current=%"PRId64", accept=%"PRId64", deviation=%d",
                     previous_floor_ts, current_floor_ts, accept_floor_ts, deviation_ts);
        }
        previous_floor_ts = current_floor_ts;
        
        // we always ensure the piece is increase one by one.
        std::stringstream ts_floor;
        ts_floor << accept_floor_ts;
        ts_file = srs_string_replace(ts_file, "[timestamp]", ts_floor.str());
        
        // TODO: FIMXE: we must use the accept ts floor time to generate the hour variable.
        ts_file = srs_path_build_timestamp(ts_file, TimestampType_StreamStart, &hls_publish_time);
        ts_file = srs_path_build_timestamp(ts_file, TimestampType_TsFileStart);
    } else {
		// 处理ts文件的文件名及部分路径名
        ts_file = srs_path_build_timestamp(ts_file, TimestampType_StreamStart, &hls_publish_time);
        ts_file = srs_path_build_timestamp(ts_file, TimestampType_TsFileStart);
    }
    if (true) {
        std::stringstream ss;
        ss << current->sequence_no;
        ts_file = srs_string_replace(ts_file, "[seq]", ss.str());
    }
	// 生成ts文件保存的路径，保存于current->full_path
    current->full_path = hls_path + "/" + ts_file;
    srs_info("hls: generate ts path %s, tmpl=%s, floor=%d", ts_file.c_str(), hls_ts_file.c_str(), hls_ts_floor);

	// 直播m3u8文件中的ts文件索引路径生成
    // the ts url, relative or absolute url or http url.
    if (!hls_entry_prefix.empty() && !srs_string_ends_with(hls_entry_prefix, "/")) {
		// ts文件在直播m3u8文件中的索引以http形式体现
		current->uri += hls_entry_prefix;
        current->uri += "/";
		current->uri += ts_file;
    }
	else
	{
		std::string ts_url;
		if (srs_string_starts_with(current->full_path, m3u8_dir)) {
			// ts文件在直播m3u8文件中的索引以相对路径体现
	        ts_url = current->full_path.substr(m3u8_dir.length() + 1);
	    }
		else
		{
			// ts文件在直播m3u8文件中的索引以绝对路径体现
			ts_url = "/" + ts_file;
		}
		// 生成ts文件存放于直播m3u8文件内的url索引
		current->uri += ts_url;
	}

	if (is_vod_enabled())
	{
		// 点播m3u8文件中的ts文件索引路径生成
		// the ts url, relative or absolute url or http url.
		if (!hls_entry_prefix.empty() && !srs_string_ends_with(hls_entry_prefix, "/")) {
			// ts文件在点播m3u8文件中的索引以http形式体现
			current->vod_uri += hls_entry_prefix;
			current->vod_uri += "/";
			current->vod_uri += ts_file;
		}
		else
		{
			std::string vod_ts_url;
			if (srs_string_starts_with(current->full_path, vod_m3u8_dir)) {
				// ts文件在点播m3u8文件中的索引以相对路径体现
				vod_ts_url = current->full_path.substr(m3u8_dir.length() + 1);
			}
			else
			{
				// ts文件在点播m3u8文件中的索引以绝对路径体现
				vod_ts_url = "/" + ts_file;
			}
			// 生成ts文件存放于点播m3u8文件内的url索引
			current->vod_uri += vod_ts_url;
		}
	}
    
    // create dir recursively for hls.
    // 创建ts文件存储的目录
    std::string ts_dir = srs_path_dirname(current->full_path);
    if (should_write_file && (ret = srs_create_dir_recursively(ts_dir)) != ERROR_SUCCESS) {
        srs_error("create app dir %s failed. ret=%d", ts_dir.c_str(), ret);
        return ret;
    }
    srs_info("create ts dir %s ok", ts_dir.c_str());
    
    // open temp ts file.
    // 打开ts临时文件
    std::string tmp_file = current->full_path + ".tmp";
    if ((ret = current->muxer->open(tmp_file.c_str())) != ERROR_SUCCESS) {
        srs_error("open hls muxer failed. ret=%d", ret);
        return ret;
    }
    srs_info("open HLS muxer success. path=%s, tmp=%s",
        current->full_path.c_str(), tmp_file.c_str());

    // set the segment muxer audio codec.
    // TODO: FIXME: refine code, use event instead.
    if (acodec != SrsCodecAudioReserved1) {
        current->muxer->update_acodec(acodec);
    }
    
    return ret;
}

int SrsHlsMuxer::on_sequence_header()
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(current);
    
    // set the current segment to sequence header,
    // when close the segement, it will write a discontinuity to m3u8 file.
    current->is_sequence_header = true;
    
    return ret;
}

bool SrsHlsMuxer::is_segment_overflow()
{
    srs_assert(current);
    
    // to prevent very small segment.
    if (current->duration * 1000 < 2 * SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS) {
        return false;
    }
    
    // use N% deviation, to smoother.
    double deviation = hls_ts_floor? SRS_HLS_FLOOR_REAP_PERCENT * deviation_ts * hls_fragment : 0.0;
    srs_info("hls: dur=%.2f, tar=%.2f, dev=%.2fms/%dp, frag=%.2f",
        current->duration, hls_fragment + deviation, deviation, deviation_ts, hls_fragment);
    
    return current->duration >= hls_fragment + deviation;
}

bool SrsHlsMuxer::wait_keyframe()
{
    return hls_wait_keyframe;
}
// 切片是否过大溢出判断
bool SrsHlsMuxer::is_segment_absolutely_overflow()
{
    // @see https://github.com/ossrs/srs/issues/151#issuecomment-83553950
    srs_assert(current);
    
    // to prevent very small segment.
    if (current->duration * 1000 < 2 * SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS) {
        return false;
    }
    
    // use N% deviation, to smoother.
    double deviation = hls_ts_floor? SRS_HLS_FLOOR_REAP_PERCENT * deviation_ts * hls_fragment : 0.0;
    srs_info("hls: dur=%.2f, tar=%.2f, dev=%.2fms/%dp, frag=%.2f",
             current->duration, hls_fragment + deviation, deviation, deviation_ts, hls_fragment);
    
    return current->duration >= hls_aof_ratio * hls_fragment + deviation;
}
// 更新ts音频编码格式
int SrsHlsMuxer::update_acodec(SrsCodecAudio ac)
{
    srs_assert(current);
    srs_assert(current->muxer);
    acodec = ac;
    return current->muxer->update_acodec(ac);
}
// 获取是否是纯音频标志
bool SrsHlsMuxer::pure_audio()
{
    return current && current->muxer && current->muxer->video_codec() == SrsCodecVideoDisabled;
}

int SrsHlsMuxer::flush_audio(SrsTsCache* cache)
{
    int ret = ERROR_SUCCESS;

    // if current is NULL, segment is not open, ignore the flush event.
    if (!current) {
        srs_warn("flush audio ignored, for segment is not open.");
        return ret;
    }
    
    if (!cache->audio || cache->audio->payload->length() <= 0) {
        return ret;
    }
    
    // update the duration of segment.
    // 更新切片持续时间
    current->update_duration(cache->audio->pts);
    // 将音频缓存数据编码并写入ts文件
    if ((ret = current->muxer->write_audio(cache->audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the msg
    srs_freep(cache->audio);

    return ret;
}

int SrsHlsMuxer::flush_video(SrsTsCache* cache)
{
    int ret = ERROR_SUCCESS;

    // if current is NULL, segment is not open, ignore the flush event.
    if (!current) {
        srs_warn("flush video ignored, for segment is not open.");
        return ret;
    }
    
    if (!cache->video || cache->video->payload->length() <= 0) {
        return ret;
    }
    
    srs_assert(current);
    
    // update the duration of segment.
    current->update_duration(cache->video->dts);
    
    if ((ret = current->muxer->write_video(cache->video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the msg
    srs_freep(cache->video);
    
    return ret;
}

int SrsHlsMuxer::segment_close(string log_desc)
{
    int ret = ERROR_SUCCESS;
    
    if (!current) {
        srs_warn("ignore the segment close, for segment is not open.");
        return ret;
    }
    
    // when close current segment, the current segment must not be NULL.
    srs_assert(current);

    // assert segment duplicate.
    std::vector<SrsHlsSegment*>::iterator it;
    it = std::find(segments.begin(), segments.end(), current);
    srs_assert(it == segments.end());

    // valid, add to segments if segment duration is ok
    // when too small, it maybe not enough data to play.
    // when too large, it maybe timestamp corrupt.
    // make the segment more acceptable, when in [min, max_td * 2], it's ok.
    if (current->duration * 1000 >= SRS_AUTO_HLS_SEGMENT_MIN_DURATION_MS && (int)current->duration <= max_td * 2) {
        segments.push_back(current);
        
        // use async to call the http hooks, for it will cause thread switch.
        // hookback on_hls消息
        if ((ret = async->execute(new SrsDvrAsyncCallOnHls(
            _srs_context->get_id(), req,
            current->full_path, current->uri, m3u8, m3u8_url,
            current->sequence_no, current->duration))) != ERROR_SUCCESS)
        {
            return ret;
        }
        
        // use async to call the http hooks, for it will cause thread switch.
        // hookback on_hls_notify消息
        if ((ret = async->execute(new SrsDvrAsyncCallOnHlsNotify(_srs_context->get_id(), req, current->uri))) != ERROR_SUCCESS) {
            return ret;
        }
    
        srs_info("%s reap ts segment, sequence_no=%d, uri=%s, duration=%.2f, start=%"PRId64,
            log_desc.c_str(), current->sequence_no, current->uri.c_str(), current->duration, 
            current->segment_start_dts);
    
        // close the muxer of finished segment.
        srs_freep(current->muxer);
        std::string full_path = current->full_path;
        current = NULL;
        
        // rename from tmp to real path
        // 将临时ts文件重命名为正式文件
        std::string tmp_file = full_path + ".tmp";
        if (should_write_file && rename(tmp_file.c_str(), full_path.c_str()) < 0) {
            ret = ERROR_HLS_WRITE_FAILED;
            srs_error("rename ts file failed, %s => %s. ret=%d", 
                tmp_file.c_str(), full_path.c_str(), ret);
            return ret;
        }
    } else {
        // reuse current segment index.
        // ts切片时间很短的时候就reload，会进这个流程
        _sequence_no--;
        
        srs_trace("%s drop ts segment, sequence_no=%d, uri=%s, duration=%.2f, start=%"PRId64"",
            log_desc.c_str(), current->sequence_no, current->uri.c_str(), current->duration, 
            current->segment_start_dts);
        
        // rename from tmp to real path
        std::string tmp_file = current->full_path + ".tmp";
        if (should_write_file) {
            if (unlink(tmp_file.c_str()) < 0) {
                srs_warn("ignore unlink path failed, file=%s.", tmp_file.c_str());
            }
        }
        
        srs_freep(current);
    }
    
    // the segments to remove
    // 待清除的切片
    std::vector<SrsHlsSegment*> segment_to_remove;
    
    // shrink the segments.
    double duration = 0;
    int remove_index = -1;
    for (int i = (int)segments.size() - 1; i >= 0; i--) {
        SrsHlsSegment* segment = segments[i];
        duration += segment->duration;
        
        if ((int)duration > hls_window) {
            remove_index = i;
            break;
        }
    }
    for (int i = 0; i < remove_index && !segments.empty(); i++) {
        SrsHlsSegment* segment = *segments.begin();
        segments.erase(segments.begin());
        segment_to_remove.push_back(segment);
    }
    
    // refresh the m3u8, donot contains the removed ts
    // 更新m3u8文件信息
    ret = refresh_m3u8();

    // remove the ts file.
    // 删除超过window周期的ts文件
    for (int i = 0; i < (int)segment_to_remove.size(); i++) {
        SrsHlsSegment* segment = segment_to_remove[i];
        // 若配置了点播的vod m3u8文件路径，则不删除过期的ts切片
        if (hls_cleanup && should_write_file && (SegmentStatus_Vod != segment->status) && (SegmentStatus_Vod_Left != segment->status) ) {
            if (unlink(segment->full_path.c_str()) < 0) {
                srs_warn("cleanup unlink path failed, file=%s.", segment->full_path.c_str());
            }
        }
        
        if (SegmentStatus_Vod == segment->status)
    	{
    		// 如果点播使能，则不删除过期的切片信息，用于后期产生点播的m3u8
    		expired_segments.push_back(segment);
    	}
		else
		{
			srs_freep(segment);
		}
    }
    segment_to_remove.clear();
    
    // check ret of refresh m3u8
    if (ret != ERROR_SUCCESS) {
        srs_error("refresh m3u8 failed. ret=%d", ret);
        return ret;
    }
	// 刷新点播m3u8文件，内部会判断点播是否使能
	ret = refresh_vod_m3u8();
	if (ret != ERROR_SUCCESS) {
        srs_error("refresh vod m3u8 failed. ret=%d", ret);
        return ret;
    }
	
    return ret;
}

bool SrsHlsMuxer::is_vod_enabled()
{
	return vod_m3u8_url != "";
}

int SrsHlsMuxer::refresh_m3u8()
{
    int ret = ERROR_SUCCESS;
    
    // no segments, also no m3u8, return.
    // 没有正式切片，则不生成m3u8文件
    if (segments.size() == 0) {
        return ret;
    }
    
    std::string temp_m3u8 = m3u8 + ".temp";
    if ((ret = _refresh_m3u8(temp_m3u8)) == ERROR_SUCCESS) {
        if (should_write_file && rename(temp_m3u8.c_str(), m3u8.c_str()) < 0) {
            ret = ERROR_HLS_WRITE_FAILED;
            srs_error("rename m3u8 file failed. %s => %s, ret=%d", temp_m3u8.c_str(), m3u8.c_str(), ret);
        }
    }
    
    // remove the temp file.
    if (srs_path_exists(temp_m3u8)) {
        if (unlink(temp_m3u8.c_str()) < 0) {
            srs_warn("ignore remove m3u8 failed, %s", temp_m3u8.c_str());
        }
    }
    
    return ret;
}

int SrsHlsMuxer::_refresh_m3u8(string m3u8_file)
{
    int ret = ERROR_SUCCESS;
    
    // no segments, return.
    if (segments.size() == 0) {
        return ret;
    }

    SrsHlsCacheWriter writer(should_write_cache, should_write_file);
    if ((ret = writer.open(m3u8_file)) != ERROR_SUCCESS) {
        srs_error("open m3u8 file %s failed. ret=%d", m3u8_file.c_str(), ret);
        return ret;
    }
    srs_info("open m3u8 file %s success.", m3u8_file.c_str());
    
    // #EXTM3U\n
    // #EXT-X-VERSION:3\n
    // #EXT-X-ALLOW-CACHE:YES\n
    std::stringstream ss;
    ss << "#EXTM3U" << SRS_CONSTS_LF
        << "#EXT-X-VERSION:3" << SRS_CONSTS_LF
        << "#EXT-X-ALLOW-CACHE:YES" << SRS_CONSTS_LF;
    srs_verbose("write m3u8 header success.");
    
    // #EXT-X-MEDIA-SEQUENCE:4294967295\n
    SrsHlsSegment* first = *segments.begin();
    ss << "#EXT-X-MEDIA-SEQUENCE:" << first->sequence_no << SRS_CONSTS_LF;
    srs_verbose("write m3u8 sequence success.");
    
    // iterator shared for td generation and segemnts wrote.
    std::vector<SrsHlsSegment*>::iterator it;
    
    // #EXT-X-TARGETDURATION:4294967295\n
    /**
    * @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 25
    * The Media Playlist file MUST contain an EXT-X-TARGETDURATION tag.
    * Its value MUST be equal to or greater than the EXTINF duration of any
    * media segment that appears or will appear in the Playlist file,
    * rounded to the nearest integer. Its value MUST NOT change. A
    * typical target duration is 10 seconds.
    */
    // @see https://github.com/ossrs/srs/issues/304#issuecomment-74000081
    int target_duration = 0;
    for (it = segments.begin(); it != segments.end(); ++it) {
        SrsHlsSegment* segment = *it;
        target_duration = srs_max(target_duration, (int)ceil(segment->duration));
    }
    target_duration = srs_max(target_duration, max_td);
    
    ss << "#EXT-X-TARGETDURATION:" << target_duration << SRS_CONSTS_LF;
    srs_verbose("write m3u8 duration success.");
    
    // write all segments
    for (it = segments.begin(); it != segments.end(); ++it) {
        SrsHlsSegment* segment = *it;
        
        if (segment->is_sequence_header) {
            // #EXT-X-DISCONTINUITY\n 表示属性是否发生变化，可用于区分是否是同一次推流
            ss << "#EXT-X-DISCONTINUITY" << SRS_CONSTS_LF;
            srs_verbose("write m3u8 segment discontinuity success.");
        }
        
        // "#EXTINF:4294967295.208,\n"
        ss.precision(3);
        ss.setf(std::ios::fixed, std::ios::floatfield);
        ss << "#EXTINF:" << segment->duration << ", no desc" << SRS_CONSTS_LF;
        srs_verbose("write m3u8 segment info success.");
        
        // {file name}\n
        ss << segment->uri << SRS_CONSTS_LF;
        srs_verbose("write m3u8 segment uri success.");
    }

    // write m3u8 to writer.
    std::string m3u8 = ss.str();
    if ((ret = writer.write((char*)m3u8.c_str(), (int)m3u8.length(), NULL)) != ERROR_SUCCESS) {
        srs_error("write m3u8 failed. ret=%d", ret);
        return ret;
    }
    srs_info("write m3u8 %s success.", m3u8_file.c_str());
    
    return ret;
}

int SrsHlsMuxer::refresh_vod_m3u8()
{
    int ret = ERROR_SUCCESS;

	if (false == is_vod_enabled())
	{
		return ret;
	}
	
    // no segments, also no m3u8, return.
    // 没有正式切片，则不生成m3u8文件
    if (segments.size() == 0 && expired_segments.size() == 0) {
        return ret;
    }
    
    std::string temp_vod_m3u8 = vod_m3u8 + ".temp";
    if ((ret = _refresh_vod_m3u8(temp_vod_m3u8)) == ERROR_SUCCESS) {
        if (should_write_file && rename(temp_vod_m3u8.c_str(), vod_m3u8.c_str()) < 0) {
            ret = ERROR_HLS_WRITE_FAILED;
            srs_error("rename vod m3u8 file failed. %s => %s, ret=%d", temp_vod_m3u8.c_str(), vod_m3u8.c_str(), ret);
        }
    }
    
    // remove the temp file.
    if (srs_path_exists(temp_vod_m3u8)) {
        if (unlink(temp_vod_m3u8.c_str()) < 0) {
            srs_warn("ignore remove vod m3u8 failed, %s", temp_vod_m3u8.c_str());
        }
    }
    
    return ret;
}

int SrsHlsMuxer::_refresh_vod_m3u8(string vod_m3u8_file)
{
    int ret = ERROR_SUCCESS;

	if (false == is_vod_enabled())
	{
		return ret;
	}
	
    // no segments, return.
    if (segments.size() == 0 && expired_segments.size() == 0) {
        return ret;
    }
	
    SrsHlsSegment* first = NULL;

    // iterator shared for td generation and segemnts wrote.
    std::vector<SrsHlsSegment*>::iterator it;
	for (it = expired_segments.begin(); it != expired_segments.end(); ++it) {
        SrsHlsSegment* expired_segment = *it;
		if (expired_segment->status == SegmentStatus_Vod)
		{
			first = expired_segment;
			break;
		}
    }

	if (it == expired_segments.end())
	{
		for (it = segments.begin(); it != segments.end(); ++it) {
	        SrsHlsSegment* segment = *it;
			if (segment->status == SegmentStatus_Vod)
			{
				first = segment;
				break;
			}
    	}
		// 没有新的点播切片，返回
		if (it == segments.end())
		{
			return ret;
		}
	}
	
    SrsHlsCacheWriter writer(should_write_cache, should_write_file);
    if ((ret = writer.open(vod_m3u8_file)) != ERROR_SUCCESS) {
        srs_error("open vod m3u8 file %s failed. ret=%d", vod_m3u8_file.c_str(), ret);
        return ret;
    }
    srs_info("open vod m3u8 file %s success.", vod_m3u8_file.c_str());
    
    // #EXTM3U\n
    // #EXT-X-VERSION:3\n
    // #EXT-X-ALLOW-CACHE:YES\n
    std::stringstream ss;
    ss << "#EXTM3U" << SRS_CONSTS_LF
        << "#EXT-X-VERSION:3" << SRS_CONSTS_LF
        << "#EXT-X-ALLOW-CACHE:YES" << SRS_CONSTS_LF;
    srs_verbose("write vod m3u8 header success.");
    
    // #EXT-X-MEDIA-SEQUENCE:4294967295\n
    ss << "#EXT-X-MEDIA-SEQUENCE:" << first->sequence_no << SRS_CONSTS_LF;
    srs_verbose("write vod m3u8 sequence success.");
        
    // #EXT-X-TARGETDURATION:4294967295\n
    /**
    * @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 25
    * The Media Playlist file MUST contain an EXT-X-TARGETDURATION tag.
    * Its value MUST be equal to or greater than the EXTINF duration of any
    * media segment that appears or will appear in the Playlist file,
    * rounded to the nearest integer. Its value MUST NOT change. A
    * typical target duration is 10 seconds.
    */
    // @see https://github.com/ossrs/srs/issues/304#issuecomment-74000081
    int target_duration = 0;
	
	for (it = expired_segments.begin(); it != expired_segments.end(); ++it) {
        SrsHlsSegment* expired_segment = *it;
		if (expired_segment->status != SegmentStatus_Vod)
		{
			continue;
		}
		
		target_duration = srs_max(target_duration, (int)ceil(expired_segment->duration));
    }
	
    for (it = segments.begin(); it != segments.end(); ++it) {
        SrsHlsSegment* segment = *it;
		if (segment->status != SegmentStatus_Vod)
		{
			continue;
		}
		
		target_duration = srs_max(target_duration, (int)ceil(segment->duration));
    }
	
    target_duration = srs_max(target_duration, max_td);
    
    ss << "#EXT-X-TARGETDURATION:" << target_duration << SRS_CONSTS_LF;
    srs_verbose("write vod m3u8 duration success.");
    
    // write all segments
    for (it = expired_segments.begin(); it != expired_segments.end(); ++it) {
        SrsHlsSegment* expired_segment = *it;

		if (expired_segment->status != SegmentStatus_Vod)
		{
			continue;
		}

        if (expired_segment->is_sequence_header) {
            // #EXT-X-DISCONTINUITY\n 表示属性是否发生变化，可用于区分是否是同一次推流
            ss << "#EXT-X-DISCONTINUITY" << SRS_CONSTS_LF;
            srs_verbose("write vod m3u8 segment discontinuity success.");
        }
        
        // "#EXTINF:4294967295.208,\n"
        ss.precision(3);
        ss.setf(std::ios::fixed, std::ios::floatfield);
        ss << "#EXTINF:" << expired_segment->duration << ", no desc" << SRS_CONSTS_LF;
        srs_verbose("write vod m3u8 segment info success.");
        
        // {file name}\n
        ss << expired_segment->vod_uri << SRS_CONSTS_LF;
        srs_verbose("write vod m3u8 segment uri success.");
    }
	
    for (it = segments.begin(); it != segments.end(); ++it) {
        SrsHlsSegment* segment = *it;
        
		if (segment->status != SegmentStatus_Vod)
		{
			continue;
		}

        if (segment->is_sequence_header) {
            // #EXT-X-DISCONTINUITY\n 表示属性是否发生变化，可用于区分是否是同一次推流
            ss << "#EXT-X-DISCONTINUITY" << SRS_CONSTS_LF;
            srs_verbose("write vod m3u8 segment discontinuity success.");
        }
        
        // "#EXTINF:4294967295.208,\n"
        ss.precision(3);
        ss.setf(std::ios::fixed, std::ios::floatfield);
        ss << "#EXTINF:" << segment->duration << ", no desc" << SRS_CONSTS_LF;
        srs_verbose("write vod m3u8 segment info success.");
        
        // {file name}\n
        ss << segment->vod_uri << SRS_CONSTS_LF;
        srs_verbose("write vod m3u8 segment uri success.");
    }

	// 加上结束标志，表明是点播
	ss << "#EXT-X-ENDLIST" << SRS_CONSTS_LF;
	
    // write vod m3u8 to writer.
    std::string vod_m3u8 = ss.str();
    if ((ret = writer.write((char*)vod_m3u8.c_str(), (int)vod_m3u8.length(), NULL)) != ERROR_SUCCESS) {
        srs_error("write vod m3u8 failed. ret=%d", ret);
        return ret;
    }
    srs_info("write vod m3u8 %s success.", vod_m3u8_file.c_str());
    
    return ret;
}

SrsHlsCache::SrsHlsCache()
{
    cache = new SrsTsCache();
}

SrsHlsCache::~SrsHlsCache()
{
    srs_freep(cache);
}
// 获取hls配置信息并保存，生成m3u8文件保存的目录，生成第一个ts文件
int SrsHlsCache::on_publish(SrsHlsMuxer* muxer, SrsRequest* req, int64_t segment_start_dts)
{
    int ret = ERROR_SUCCESS;

    std::string vhost = req->vhost;
    std::string stream = req->stream;
    std::string app = req->app;
    
    double hls_fragment = _srs_config->get_hls_fragment(vhost);
    double hls_window = _srs_config->get_hls_window(vhost);
    
    // get the hls m3u8 ts list entry prefix config
    std::string entry_prefix = _srs_config->get_hls_entry_prefix(vhost);
    // get the hls path config
    std::string path = _srs_config->get_hls_path(vhost);
    std::string m3u8_file = _srs_config->get_hls_m3u8_file(vhost);
	std::string vod_m3u8_file = _srs_config->get_hls_vod_m3u8_file(vhost);
    std::string ts_file = _srs_config->get_hls_ts_file(vhost);
    bool cleanup = _srs_config->get_hls_cleanup(vhost);
    bool wait_keyframe = _srs_config->get_hls_wait_keyframe(vhost);
    // the audio overflow, for pure audio to reap segment.
    double hls_aof_ratio = _srs_config->get_hls_aof_ratio(vhost);
    // whether use floor(timestamp/hls_fragment) for variable timestamp
    bool ts_floor = _srs_config->get_hls_ts_floor(vhost);
    // the seconds to dispose the hls.
    int hls_dispose = _srs_config->get_hls_dispose(vhost);
	// 获取推流开始时间
	timeval publish_time;
	if ((ret = srs_gettimeofday(publish_time)) != ERROR_SUCCESS)
	{
        srs_error("m3u8 muxer gettimeofday failed. ret=%d", ret);
        return ret;
	}
	
    // TODO: FIXME: support load exists m3u8, to continue publish stream.
    // for the HLS donot requires the EXT-X-MEDIA-SEQUENCE be monotonically increase.
    
    // open muxer
    // 更新传入的信息，并生成m3u8文件存储的路径
    if ((ret = muxer->update_config(req, entry_prefix,
        path, m3u8_file, vod_m3u8_file, ts_file, hls_fragment, hls_window, ts_floor, hls_aof_ratio,
        cleanup, wait_keyframe, publish_time)) != ERROR_SUCCESS
    ) {
        srs_error("m3u8 muxer update config failed. ret=%d", ret);
        return ret;
    }
    // 生成ts分段文件，此时segment_start_dts值为0
    if ((ret = muxer->segment_open(segment_start_dts)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer open segment failed. ret=%d", ret);
        return ret;
    }
    srs_trace("hls: win=%.2f, frag=%.2f, prefix=%s, path=%s, m3u8=%s, vod_m3u8=%s, ts=%s, aof=%.2f, floor=%d, clean=%d, waitk=%d, dispose=%d",
        hls_window, hls_fragment, entry_prefix.c_str(), path.c_str(), m3u8_file.c_str(), vod_m3u8_file.c_str(), 
        ts_file.c_str(), hls_aof_ratio, ts_floor, cleanup, wait_keyframe, hls_dispose);
    
    return ret;
}
// 将缓存数据写入ts文件，并关闭ts，m3u8文件
int SrsHlsCache::on_unpublish(SrsHlsMuxer* muxer)
{
    int ret = ERROR_SUCCESS;
    // 将缓存数据写入文件
    if ((ret = muxer->flush_audio(cache)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush audio failed. ret=%d", ret);
        return ret;
    }
    // 关闭ts, m3u8等分段切片文件
    if ((ret = muxer->segment_close("unpublish")) != ERROR_SUCCESS) {
        return ret;
    }

	muxer->update_segments_status();
	
    return ret;
}

int SrsHlsCache::on_sequence_header(SrsHlsMuxer* muxer)
{
    // TODO: support discontinuity for the same stream
    // currently we reap and insert discontinity when encoder republish,
    // but actually, event when stream is not republish, the 
    // sequence header may change, for example,
    // ffmpeg ingest a external rtmp stream and push to srs,
    // when the sequence header changed, the stream is not republish.
    return muxer->on_sequence_header();
}

int SrsHlsCache::write_audio(SrsAvcAacCodec* codec, SrsHlsMuxer* muxer, int64_t pts, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    // write audio to cache.
    // 将音频数据写到ts缓存
    if ((ret = cache->cache_audio(codec, pts, sample)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // reap when current source is pure audio.
    // it maybe changed when stream info changed,
    // for example, pure audio when start, audio/video when publishing,
    // pure audio again for audio disabled.
    // so we reap event when the audio incoming when segment overflow.
    // @see https://github.com/ossrs/srs/issues/151
    // we use absolutely overflow of segment to make jwplayer/ffplay happy
    // @see https://github.com/ossrs/srs/issues/151#issuecomment-71155184
    // 若切片过大溢出，则需要启动新的切片
    if (cache->audio && muxer->is_segment_absolutely_overflow()) {
        srs_info("hls: absolute audio reap segment.");
		// 分割切片
        if ((ret = reap_segment("audio", muxer, cache->audio->pts)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    // for pure audio, aggregate some frame to one.
    // 如果当前只有音频数据，则等音频数据积攒到一定量再写入ts文件
    if (muxer->pure_audio() && cache->audio) {
        if (pts - cache->audio->start_pts < SRS_CONSTS_HLS_PURE_AUDIO_AGGREGATE) {
            return ret;
        }
    }
    
    // directly write the audio frame by frame to ts,
    // it's ok for the hls overload, or maybe cause the audio corrupt,
    // which introduced by aggregate the audios to a big one.
    // @see https://github.com/ossrs/srs/issues/512
    // 将缓存的音频数据写入ts文件
    if ((ret = muxer->flush_audio(cache)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}
    
int SrsHlsCache::write_video(SrsAvcAacCodec* codec, SrsHlsMuxer* muxer, int64_t dts, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    // write video to cache.
    // 将codec里的数据转为ts格式保存在cache中
    if ((ret = cache->cache_video(codec, dts, sample)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // when segment overflow, reap if possible.
    // 判断切片是否溢出
    if (muxer->is_segment_overflow()) {
        // do reap ts if any of:
        //      a. wait keyframe and got keyframe.
        //      b. always reap when not wait keyframe.
        // 
        if (!muxer->wait_keyframe() || sample->frame_type == SrsCodecVideoAVCFrameKeyFrame) {
            // reap the segment, which will also flush the video.
            // 先切片，并将音频和视频缓存写入新的ts文件
            if ((ret = reap_segment("video", muxer, cache->video->dts)) != ERROR_SUCCESS) {
                return ret;
            }
        }
    }
    
    // flush video when got one
    // 将视频数据写入ts文件，并清空cache中的视频缓存
    if ((ret = muxer->flush_video(cache)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush video failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}
// 分割切片
int SrsHlsCache::reap_segment(string log_desc, SrsHlsMuxer* muxer, int64_t segment_start_dts)
{
    int ret = ERROR_SUCCESS;
    
    // TODO: flush audio before or after segment?
    // TODO: fresh segment begin with audio or video?

    // close current ts.
    if ((ret = muxer->segment_close(log_desc)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer close segment failed. ret=%d", ret);
        return ret;
    }
    
    // open new ts.
    if ((ret = muxer->segment_open(segment_start_dts)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer open segment failed. ret=%d", ret);
        return ret;
    }
    
    // segment open, flush video first.
    // 将ts缓存数据写入ts文件
    if ((ret = muxer->flush_video(cache)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush video failed. ret=%d", ret);
        return ret;
    }
    
    // segment open, flush the audio.
    // @see: ngx_rtmp_hls_open_fragment
    /* start fragment with audio to make iPhone happy */
    if ((ret = muxer->flush_audio(cache)) != ERROR_SUCCESS) {
        srs_error("m3u8 muxer flush audio failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsHls::SrsHls()
{
    _req = NULL;
    source = NULL;
    
    hls_enabled = false;
    hls_can_dispose = false;
    last_update_time = 0;

    codec = new SrsAvcAacCodec();
    sample = new SrsCodecSample();
    jitter = new SrsRtmpJitter();
    
    muxer = new SrsHlsMuxer();
    hls_cache = new SrsHlsCache();
		
    pprint = SrsPithyPrint::create_hls();
    stream_dts = 0;
}

SrsHls::~SrsHls()
{
    srs_freep(_req);
    srs_freep(codec);
    srs_freep(sample);
    srs_freep(jitter);
    
    srs_freep(muxer);
    srs_freep(hls_cache);
    
    srs_freep(pprint);
}
// hls清除ts，m3u8缓存文件接口
void SrsHls::dispose()
{
	// 如果当前在推流，则先调用unpublish
    if (hls_enabled) {
		// 关闭缓存文件
        on_unpublish();
    }
    
	// 清除所有ts, m3u8缓存文件    
    muxer->dispose();
}
// 该接口会在主循环中被循环调用，SrsSource::cycle_all，每隔1000ms调用一次
// 下面的代码逻辑实际上是有问题的，当直播时间大于dispose时间时，直播一停止，缓存就会被清除
int SrsHls::cycle()
{
    int ret = ERROR_SUCCESS;
    
    srs_info("hls cycle for source %d", source->source_id());
    
    if (last_update_time <= 0) {
		// 更新保存系统时间
        last_update_time = srs_get_system_time_ms();
    }
    
    if (!_req) {
        return ret;
    }
    // 获取hls缓存文件ts，m3u8是否需要定时清除，以及清除等待时间
    int hls_dispose = _srs_config->get_hls_dispose(_req->vhost) * 1000;
	// 0表示不需要清除，返回
    if (hls_dispose <= 0) {
        return ret;
    }
	// 未到清除时间，返回，收到音视频消息都会更新last_update_time
	// 当推流时，一般都在这里返回
    if (srs_get_system_time_ms() - last_update_time <= hls_dispose) {
        return ret;
    }
    last_update_time = srs_get_system_time_ms();
    // 判断当前状态是否可以进行缓存清除操作
    if (!hls_can_dispose) {
        return ret;
    }
    hls_can_dispose = false;
    
    srs_trace("hls cycle to dispose hls %s, timeout=%dms", _req->get_stream_url().c_str(), hls_dispose);
	// 清除ts, m3u8缓存文件
    dispose();
    
    return ret;
}

int SrsHls::initialize(SrsSource* s)
{
    int ret = ERROR_SUCCESS;

    source = s;
	// 内部会启一个st线程，主要完成hook callback的on_hls和notify消息的发送
    if ((ret = muxer->initialize()) != ERROR_SUCCESS) {
        return ret;
    }
    return ret;
}
// fetch_sequence_header 正常推流时，该值为false，reload配置时，该值为true
int SrsHls::on_publish(SrsRequest* req, bool fetch_sequence_header)
{
    int ret = ERROR_SUCCESS;
    
    srs_freep(_req);
    _req = req->copy();
    
    // update the hls time, for hls_dispose.
    // 获取最近更新的系统时间，并保存
    last_update_time = srs_get_system_time_ms();
    
    // support multiple publish.
    // 如果已经在推流，直接返回成功
    if (hls_enabled) {
        return ret;
    }
    
    std::string vhost = req->vhost;
	// 判断hls是否使能
    if (!_srs_config->get_hls_enabled(vhost)) {
        return ret;
    }
    // 生成ts临时文件，此时stream_dts值还是为0
    // 如果是直播中reload了，则stream_dts不为0
    if ((ret = hls_cache->on_publish(muxer, req, stream_dts)) != ERROR_SUCCESS) {
        return ret;
    }
	
    // if enabled, open the muxer.
    // 可以处理生成hls文件
    hls_enabled = true;
    
    // ok, the hls can be dispose, or need to be dispose.
    // 使能缓存清理标志
    hls_can_dispose = true;
    
    // when publish, don't need to fetch sequence header, which is old and maybe corrupt.
    // when reload, we must fetch the sequence header from source cache.
    // 正常推流时，该值为false，reload配置时，该值为true
    if (fetch_sequence_header) {
        // notice the source to get the cached sequence header.
        // when reload to start hls, hls will never get the sequence header in stream,
        // use the SrsSource.on_hls_start to push the sequence header to HLS.
        if ((ret = source->on_hls_start()) != ERROR_SUCCESS) {
            srs_error("callback source hls start failed. ret=%d", ret);
            return ret;
        }
    }

    return ret;
}

void SrsHls::on_unpublish()
{
    int ret = ERROR_SUCCESS;
    
    // support multiple unpublish.
    // 判断当前是否在推流
    if (!hls_enabled) {
        return;
    }
	// 将缓存文件写入ts，并关闭ts，m3u8文件
    if ((ret = hls_cache->on_unpublish(muxer)) != ERROR_SUCCESS) {
        srs_error("ignore m3u8 muxer flush/close audio failed. ret=%d", ret);
    }
	
    hls_enabled = false;
}

int SrsHls::on_meta_data(SrsAmf0Object* metadata)
{
    int ret = ERROR_SUCCESS;

    if (!metadata) {
        srs_trace("no metadata persent, hls ignored it.");
        return ret;
    }
    
    if (metadata->count() <= 0) {
        srs_trace("no metadata persent, hls ignored it.");
        return ret;
    }
    
    return ret;
}
// hls处理音频数据
int SrsHls::on_audio(SrsSharedPtrMessage* shared_audio)
{
    int ret = ERROR_SUCCESS;
    
    if (!hls_enabled) {
        return ret;
    }
    
    // update the hls time, for hls_dispose.
    // 更新最后收到消息的时间，SrsHls::cycle接口中有用
    last_update_time = srs_get_system_time_ms();

    SrsSharedPtrMessage* audio = shared_audio->copy();
    SrsAutoFree(SrsSharedPtrMessage, audio);
    
    sample->clear();
	// 解析音频消息，将结果通过sample返回
    if ((ret = codec->audio_aac_demux(audio->payload, audio->size, sample)) != ERROR_SUCCESS) {
        if (ret != ERROR_HLS_TRY_MP3) {
            srs_error("hls aac demux audio failed. ret=%d", ret);
            return ret;
        }
		// 看这代码尿性，貌似还支持mp3音频格式
        if ((ret = codec->audio_mp3_demux(audio->payload, audio->size, sample)) != ERROR_SUCCESS) {
            srs_error("hls mp3 demux audio failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("audio decoded, type=%d, codec=%d, cts=%d, size=%d, time=%"PRId64, 
        sample->frame_type, codec->audio_codec_id, sample->cts, audio->size, audio->timestamp);
    SrsCodecAudio acodec = (SrsCodecAudio)codec->audio_codec_id;
    
    // ts support audio codec: aac/mp3
    // ts支持aac/mp3的音频格式
    if (acodec != SrsCodecAudioAAC && acodec != SrsCodecAudioMP3) {
        return ret;
    }

    // when codec changed, write new header.
	// 更新ts音频编码格式
    if ((ret = muxer->update_acodec(acodec)) != ERROR_SUCCESS) {
        srs_error("http: ts audio write header failed. ret=%d", ret);
        return ret;
    }
	
    // ignore sequence header
    // 如果是音频序号头消息，则忽略消息，只标注下已经收到音频序号头
    if (acodec == SrsCodecAudioAAC && sample->aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
        return hls_cache->on_sequence_header(muxer);
    }
    
    // TODO: FIXME: config the jitter of HLS.
    // 时间矫正
    if ((ret = jitter->correct(audio, SrsRtmpJitterAlgorithmOFF)) != ERROR_SUCCESS) {
        srs_error("rtmp jitter correct audio failed. ret=%d", ret);
        return ret;
    }
    
    // the dts calc from rtmp/flv header.
    // 计算时戳
    int64_t dts = audio->timestamp * 90;
    
    // for pure audio, we need to update the stream dts also.
    stream_dts = dts;
    // 将缓存中的音频数据写入muxer中的ts文件中，并清空音频缓存
    if ((ret = hls_cache->write_audio(codec, muxer, dts, sample)) != ERROR_SUCCESS) {
        srs_error("hls cache write audio failed. ret=%d", ret);
        return ret;
    }
	
    return ret;
}

int SrsHls::on_video(SrsSharedPtrMessage* shared_video, bool is_sps_pps)
{
    int ret = ERROR_SUCCESS;
    
    if (!hls_enabled) {
        return ret;
    }
    
    // update the hls time, for hls_dispose.
    // 更新最后收到消息的时间，SrsHls::cycle接口中有用
    last_update_time = srs_get_system_time_ms();

    SrsSharedPtrMessage* video = shared_video->copy();
    SrsAutoFree(SrsSharedPtrMessage, video);
    
    // user can disable the sps parse to workaround when parse sps failed.
    // @see https://github.com/ossrs/srs/issues/474
    if (is_sps_pps) {
        codec->avc_parse_sps = _srs_config->get_parse_sps(_req->vhost);
    }
    
    sample->clear();
	// 解析flv的video数据，并将解析结果存于codec和sample中
    if ((ret = codec->video_avc_demux(video->payload, video->size, sample)) != ERROR_SUCCESS) {
        srs_error("hls codec demux video failed. ret=%d", ret);
        return ret;
    }
    srs_info("video decoded, type=%d, codec=%d, avc=%d, cts=%d, size=%d, time=%"PRId64, 
        sample->frame_type, codec->video_codec_id, sample->avc_packet_type, sample->cts, video->size, video->timestamp);
    
    // ignore info frame,
    // @see https://github.com/ossrs/srs/issues/288#issuecomment-69863909
    if (sample->frame_type == SrsCodecVideoAVCFrameVideoInfoFrame) {
        return ret;
    }
    
    if (codec->video_codec_id != SrsCodecVideoAVC) {
        return ret;
    }
    
    // ignore sequence header
    // 如果是音频序号头消息，则忽略消息，只标注下已经收到音频序号头
    if (sample->frame_type == SrsCodecVideoAVCFrameKeyFrame
         && sample->avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
        return hls_cache->on_sequence_header(muxer);
    }
    
    // TODO: FIXME: config the jitter of HLS.
    // 时间矫正
    if ((ret = jitter->correct(video, SrsRtmpJitterAlgorithmOFF)) != ERROR_SUCCESS) {
        srs_error("rtmp jitter correct video failed. ret=%d", ret);
        return ret;
    }
    
    int64_t dts = video->timestamp * 90;
    stream_dts = dts;
    // 将缓存中的视频数据写入muxer中的ts文件中，并清空视频缓存
    if ((ret = hls_cache->write_video(codec, muxer, dts, sample)) != ERROR_SUCCESS) {
        srs_error("hls cache write video failed. ret=%d", ret);
        return ret;
    }

    // pithy print message.
    hls_show_mux_log();
    
    return ret;
}

void SrsHls::hls_show_mux_log()
{
    pprint->elapse();

    // reportable
    if (pprint->can_print()) {
        // the run time is not equals to stream time,
        // @see: https://github.com/ossrs/srs/issues/81#issuecomment-48100994
        // it's ok.
        srs_trace("-> "SRS_CONSTS_LOG_HLS" time=%"PRId64", stream dts=%"PRId64"(%"PRId64"ms), sno=%d, ts=%s, dur=%.2f, dva=%dp",
            pprint->age(), stream_dts, stream_dts / 90, muxer->sequence_no(), muxer->ts_url().c_str(),
            muxer->duration(), muxer->deviation());
    }
}

#endif


