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

#include <srs_protocol_kbps.hpp>

#include <srs_kernel_utility.hpp>

#include <srs_kernel_log.hpp>

SrsKbpsSample::SrsKbpsSample()
{
    bytes = time = 0;
    kbps = 0;
}

SrsKbpsSlice::SrsKbpsSlice()
{
    io.in = NULL;
    io.out = NULL;
    last_bytes = io_bytes_base = starttime = bytes = delta_bytes = 0;
}

SrsKbpsSlice::~SrsKbpsSlice()
{
}
//获取send/recv的总字节数
int64_t SrsKbpsSlice::get_total_bytes()
{
    return bytes + last_bytes - io_bytes_base;
}

// 该接口是根据切片现有的数据来更新各个采样样本的值，sample_1s，sample_30s，sample_1m，sample_5m，sample_60m
void SrsKbpsSlice::sample()
{
    int64_t now = srs_get_system_time_ms();
    int64_t total_bytes = get_total_bytes();

	if (sample_1s.time <= 0) {
        sample_1s.kbps = 0;
        sample_1s.time = now;
        sample_1s.bytes = total_bytes;
    }
    if (sample_30s.time <= 0) {
        sample_30s.kbps = 0;
        sample_30s.time = now;
        sample_30s.bytes = total_bytes;
    }
    if (sample_1m.time <= 0) {
        sample_1m.kbps = 0;
        sample_1m.time = now;
        sample_1m.bytes = total_bytes;
    }
    if (sample_5m.time <= 0) {
        sample_5m.kbps = 0;
        sample_5m.time = now;
        sample_5m.bytes = total_bytes;
    }
    if (sample_60m.time <= 0) {
        sample_60m.kbps = 0;
        sample_60m.time = now;
        sample_60m.bytes = total_bytes;
    }

	if (now - sample_1s.time > 1 * 1000) {
        sample_1s.kbps = (int)((total_bytes - sample_1s.bytes) * 8 / (now - sample_1s.time));
        sample_1s.time = now;
        sample_1s.bytes = total_bytes;
    }
    if (now - sample_30s.time > 30 * 1000) {
        sample_30s.kbps = (int)((total_bytes - sample_30s.bytes) * 8 / (now - sample_30s.time));
        sample_30s.time = now;
        sample_30s.bytes = total_bytes;
    }
    if (now - sample_1m.time > 60 * 1000) {
        sample_1m.kbps = (int)((total_bytes - sample_1m.bytes) * 8 / (now - sample_1m.time));
        sample_1m.time = now;
        sample_1m.bytes = total_bytes;
    }
    if (now - sample_5m.time > 300 * 1000) {
        sample_5m.kbps = (int)((total_bytes - sample_5m.bytes) * 8 / (now - sample_5m.time));
        sample_5m.time = now;
        sample_5m.bytes = total_bytes;
    }
    if (now - sample_60m.time > 3600 * 1000) {
        sample_60m.kbps = (int)((total_bytes - sample_60m.bytes) * 8 / (now - sample_60m.time));
        sample_60m.time = now;
        sample_60m.bytes = total_bytes;
    }
}

IKbpsDelta::IKbpsDelta()
{
}

IKbpsDelta::~IKbpsDelta()
{
}

SrsKbps::SrsKbps()
{
}

SrsKbps::~SrsKbps()
{
}
// 该接口用于设置统计时使用的输入，输出统计类
// in为用于统计输入的类
// out为用于统计输出的类
void SrsKbps::set_io(ISrsProtocolStatistic* in, ISrsProtocolStatistic* out)
{
    // set input stream		设置输入流
    // now, set start time.	设置输入采样切片的开始时间
    if (is.starttime == 0) {
        is.starttime = srs_get_system_time_ms();
    }
    // save the old in bytes.	保存之前已经统计过的输入字节数
    if (is.io.in) {
        is.bytes += is.last_bytes - is.io_bytes_base;
    }
    // use new io.		输入切片类使用新的输入统计类
    is.io.in = in;
    is.last_bytes = is.io_bytes_base = 0;
    if (in) {
        is.last_bytes = is.io_bytes_base = in->get_recv_bytes();
    }
    // resample	重新设置输入采样切片后，先采样一把，更新输入切片的样本值
    is.sample();
    
    // set output stream	设置输出流
    // now, set start time.	设置输出采样切片的开始时间
    if (os.starttime == 0) {
        os.starttime = srs_get_system_time_ms();
    }
    // save the old in bytes.	保存之前已经统计过的输出字节数
    if (os.io.out) {
        os.bytes += os.last_bytes - os.io_bytes_base;
    }
    // use new io.		输出切片类使用新的输出统计类
    os.io.out = out;
    os.last_bytes = os.io_bytes_base = 0;
    if (out) {
        os.last_bytes = os.io_bytes_base = out->get_send_bytes();
    }
    // resample	重新设置输出采样切片后，先采样一把，更新输出切片的样本值
    os.sample();
}
// 获取全程统计的平均发送速率
int SrsKbps::get_send_kbps()
{
    int64_t duration = srs_get_system_time_ms() - is.starttime;
    if (duration <= 0) {
        return 0;
    }
    int64_t bytes = get_send_bytes();
    return (int)(bytes * 8 / duration);
}
// 获取全程统计的平均接收速率
int SrsKbps::get_recv_kbps()
{
    int64_t duration = srs_get_system_time_ms() - os.starttime;
    if (duration <= 0) {
        return 0;
    }
    int64_t bytes = get_recv_bytes();
    return (int)(bytes * 8 / duration);
}
// 获取最近1s内的发送速率
int SrsKbps::get_send_kbps_1s()
{
    return os.sample_1s.kbps;
}
// 获取最近1s内的接收速率
int SrsKbps::get_recv_kbps_1s()
{
    return is.sample_1s.kbps;
}
// 获取最近30s内的平均发送速率
int SrsKbps::get_send_kbps_30s()
{
    return os.sample_30s.kbps;
}
// 获取最近30s内的平均接收速率
int SrsKbps::get_recv_kbps_30s()
{
    return is.sample_30s.kbps;
}
// 获取最近5min内的平均发送速率
int SrsKbps::get_send_kbps_5m()
{
    return os.sample_5m.kbps;
}
// 获取最近5min内的平均接收速率
int SrsKbps::get_recv_kbps_5m()
{
    return is.sample_5m.kbps;
}
// 获取发送的总字节数
int64_t SrsKbps::get_send_bytes()
{
    return os.get_total_bytes();
}
// 获取接收的总字节数
int64_t SrsKbps::get_recv_bytes()
{
    return is.get_total_bytes();
}
// 采样
void SrsKbps::resample()
{
    sample();
}
// 获得字节数发送增量
int64_t SrsKbps::get_send_bytes_delta()
{
    int64_t delta = os.get_total_bytes() - os.delta_bytes;
    return delta;
}
// 获得字节数接收增量
int64_t SrsKbps::get_recv_bytes_delta()
{
    int64_t delta = is.get_total_bytes() - is.delta_bytes;
    return delta;
}
// 采样完后，用于同步数据
void SrsKbps::cleanup()
{
    os.delta_bytes = os.get_total_bytes();
    is.delta_bytes = is.get_total_bytes();
}
// 增加收发字节数
void SrsKbps::add_delta(IKbpsDelta* delta)
{
    srs_assert(delta);
    
    // update the total bytes
    // 更新收发总字节数
    is.last_bytes += delta->get_recv_bytes_delta();
    os.last_bytes += delta->get_send_bytes_delta();
    // we donot sample, please use sample() to do resample.
}
// 采样
void SrsKbps::sample()
{
    // update the total bytes
    // 更新收发总字节数
    if (os.io.out) {
		// 在此处更新发送的数据字节数
        os.last_bytes = os.io.out->get_send_bytes();//调用的可能是SrsStSocket类相应的接口
    }
    
    if (is.io.in) {
		// 在此处更新接收的数据字节数
        is.last_bytes = is.io.in->get_recv_bytes();//调用的可能是SrsStSocket类相应的接口
    }
    
    // resample
    // 更新样本值
    is.sample();
    os.sample();
}

