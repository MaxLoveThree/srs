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

#ifndef SRS_KERNEL_AAC_HPP
#define SRS_KERNEL_AAC_HPP

/*
#include <srs_kernel_aac.hpp>
*/
#include <srs_core.hpp>

#if !defined(SRS_EXPORT_LIBRTMP)

#include <string>

#include <srs_kernel_codec.hpp>

class SrsStream;
class SrsFileWriter;
class SrsFileReader;

/**
* encode data to aac file.
*/
// 编码aac数据到文件中
class SrsAacEncoder
{
private:
	// 待写入数据的文件句柄
    SrsFileWriter* _fs;
private:
	// aac类型
    SrsAacObjectType aac_object;
	// aac采样率
    int8_t aac_sample_rate;
	// aac通道数
    int8_t aac_channels;
	// 是否获取了音频序号头
    bool got_sequence_header;
private:
    SrsStream* tag_stream;
public:
    SrsAacEncoder();
    virtual ~SrsAacEncoder();
public:
    /**
    * initialize the underlayer file stream.
    * @remark user can initialize multiple times to encode multiple aac files.
    * @remark, user must free the fs, aac encoder never close/free it.
    */
    // 初始化
    virtual int initialize(SrsFileWriter* fs);
public:
    /**
    * write audio/video packet.
    * @remark assert data is not NULL.
    */
    // 写入音频数据，data为flv的aac数据格式，在内部转换为ADTS的aac文件格式
    virtual int write_audio(int64_t timestamp, char* data, int size);
};

#endif

#endif

