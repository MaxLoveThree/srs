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

#ifndef SRS_PROTOCOL_KBPS_HPP
#define SRS_PROTOCOL_KBPS_HPP

/*
#include <srs_protocol_kbps.hpp>
*/

#include <srs_core.hpp>

#include <srs_rtmp_io.hpp>

/**
* a kbps sample, for example, 1minute kbps, 
* 10minute kbps sample.
*/
//采样类，包含了时间，速率，字节数
class SrsKbpsSample
{
public:
    int64_t bytes;//采样时的字节数
    int64_t time;//采样时的时间
    int kbps;//采样时的速率
public:
    SrsKbpsSample();
};

/**
* a slice of kbps statistic, for input or output.
* a slice contains a set of sessions, which has a base offset of bytes,
* where a slice is:
*       starttime(oldest session startup time)
*               bytes(total bytes of previous sessions)
*               io_bytes_base(bytes offset of current session)
*                       last_bytes(bytes of current session)
* so, the total send bytes now is:
*       send_bytes = bytes + last_bytes - io_bytes_base
* so, the bytes sent duration current session is:
*       send_bytes = last_bytes - io_bytes_base
* @remark use set_io to start new session.
* @remakr the slice is a data collection object driven by SrsKbps.
*/
//速率切片类
class SrsKbpsSlice
{
private:
    union slice_io {
        ISrsProtocolStatistic* in;
        ISrsProtocolStatistic* out;
    };
public:
    // the slice io used for SrsKbps to invoke,
    // the SrsKbpsSlice itself never use it.
    //用于保存统计时使用的流统计类指针，主要在SrsKbps类中被调用
    slice_io io;
    // session startup bytes
    // @remark, use total_bytes() to get the total bytes of slice.
    // 切片类开始统计前已经统计过的字节数，只有调用SrsKbps::set_io时被设置一次
    int64_t bytes;
    // slice starttime, the first time to record bytes.
    // 第一次记录字节数的时间，该值很少变化
    int64_t starttime;
    // session startup bytes number for io when set it,
    // the base offset of bytes for io.
    // 开始统计前输入/输出统计类原本拥有的输入/输出的总字节数，只有调用SrsKbps::set_io时被设置一次
    int64_t io_bytes_base;
    // last updated bytes number,
    // cache for io maybe freed.
    // 上次采样时的总字节数
    int64_t last_bytes;
    // samples
    SrsKbpsSample sample_1s;// 1s的采样
    SrsKbpsSample sample_30s;// 30s的采样
    SrsKbpsSample sample_1m;// 1分钟的采样
    SrsKbpsSample sample_5m;// 5分钟的采样
    SrsKbpsSample sample_60m;// 60分钟的采样
public:
    // for the delta bytes.
    int64_t delta_bytes;// 增加的总字节数
public:
    SrsKbpsSlice();
    virtual ~SrsKbpsSlice();
public:
    // Get current total bytes, not depend on sample().
    virtual int64_t get_total_bytes();
    // Resample the slice to calculate the kbps.
    virtual void sample();
};

/**
* the interface which provices delta of bytes.
* for a delta, for example, a live stream connection, we can got the delta by:
*       IKbpsDelta* delta = ...;
*       delta->resample();
*       kbps->add_delta(delta);
*       delta->cleanup();
*/
class IKbpsDelta
{
public:
    IKbpsDelta();
    virtual ~IKbpsDelta();
public:
    /**
    * resample to generate the value of delta bytes.
    */
    virtual void resample() = 0;
    /**
    * get the send or recv bytes delta.
    */
    virtual int64_t get_send_bytes_delta() = 0;
    virtual int64_t get_recv_bytes_delta() = 0;
    /**
    * cleanup the value of delta bytes.
    */
    virtual void cleanup() = 0;
};

/**
 * to statistic the kbps of io.
 * itself can be a statistic source, for example, used for SRS bytes stat.
 * there are some usage scenarios:	这里有多种使用的情况
 * 1. connections to calc kbps by sample():		客户端链接时，该类的大致用法
 *       SrsKbps* kbps = ...;
 *       kbps->set_io(in, out)		输入有效的输入输出统计类
 *       kbps->sample()
 *       kbps->get_xxx_kbps().
 *   the connections know how many bytes already send/recv.
 * 2. server to calc kbps by add_delta():	服务计算增量时，该类的大致用法
 *       SrsKbps* kbps = ...;
 *       kbps->set_io(NULL, NULL)
 *       for each connection in connections:
 *           IKbpsDelta* delta = connection; // where connection implements IKbpsDelta
 *           delta->resample()
 *           kbps->add_delta(delta)
 *           delta->cleanup()
 *       kbps->sample()
 *       kbps->get_xxx_kbps().
 * 3. kbps used as IKbpsDelta, to provides delta bytes:
 *      SrsKbps* kbps = ...;
 *      kbps->set_io(in, out);
 *      IKbpsDelta* delta = (IKbpsDelta*)kbps;
 *      delta->resample();
 *      printf("delta is %d/%d", delta->get_send_bytes_delta(), delta->get_recv_bytes_delta());
 *      delta->cleanup();
 * 4. kbps used as ISrsProtocolStatistic, to provides raw bytes:
 *      SrsKbps* kbps = ...;
 *      kbps->set_io(in, out);
 *      // both kbps->get_recv_bytes() and kbps->get_send_bytes() are available.
 *       // we can use the kbps as the data source of another kbps:
 *      SrsKbps* user = ...;
 *      user->set_io(kbps, kbps);
 *   the server never know how many bytes already send/recv, for the connection maybe closed.
 */
 // 该类使用较复杂，可以看上面英文说明
 // 主要是一个类被当做多种不同的用途来使用，所以理解起来略坑
class SrsKbps : public virtual ISrsProtocolStatistic, public virtual IKbpsDelta
{
private:
    SrsKbpsSlice is;//输入，recv数据信息存储
    SrsKbpsSlice os;//输出，send数据信息存储
public:
    SrsKbps();
    virtual ~SrsKbps();
public:
    /**
     * set io to start new session.
     * set the underlayer reader/writer,
     * if the io destroied, for instance, the forwarder reconnect,
     * user must set the io of SrsKbps to NULL to continue to use the kbps object.
     * @param in the input stream statistic. can be NULL. //参数in:输入流统计
     * @param out the output stream statistic. can be NULL. //参数out:输出流统计
     * @remark if in/out is NULL, use the cached data for kbps.
     * @remark User must set_io(NULL, NULL) then free the in and out.
     */
	//设置底层的读写统计类
    virtual void set_io(ISrsProtocolStatistic* in, ISrsProtocolStatistic* out);
public:
    /**
    * get total kbps, duration is from the startup of io.
    * @remark, use sample() to update data.
    */
    virtual int get_send_kbps();
    virtual int get_recv_kbps();
	// 1s
    virtual int get_send_kbps_1s();
    virtual int get_recv_kbps_1s();
    // 30s
    virtual int get_send_kbps_30s();
    virtual int get_recv_kbps_30s();
    // 5m
    virtual int get_send_kbps_5m();
    virtual int get_recv_kbps_5m();
// interface ISrsProtocolStatistic
public:
    virtual int64_t get_send_bytes();
    virtual int64_t get_recv_bytes();
// interface IKbpsDelta
public:
    virtual void resample();
    virtual int64_t get_send_bytes_delta();
    virtual int64_t get_recv_bytes_delta();
    virtual void cleanup();
public:
    /**
    * add delta to kbps clac mechenism.
    * we donot know the total bytes, but know the delta, for instance, 
    * for rtmp server to calc total bytes and kbps.
    * @remark user must invoke sample() to calc result after invoke this method.
    * @param delta, assert should never be NULL.
    */
    virtual void add_delta(IKbpsDelta* delta);
    /**
    * resample all samples, ignore if in/out is NULL.
    * used for user to calc the kbps, to sample new kbps value.
    * @remark if user, for instance, the rtmp server to calc the total bytes,
    *       use the add_delta() is better solutions.
    */
    // 采样
    virtual void sample();
// interface ISrsMemorySizer
public:
    virtual int size_memory();
};

#endif
