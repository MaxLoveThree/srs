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

#ifndef SRS_APP_LOG_HPP
#define SRS_APP_LOG_HPP

/*
#include <srs_app_log.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_st.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_reload.hpp>

#include <string.h>

#include <string>
#include <map>

/**
* st thread context, get_id will get the st-thread id, 
* which identify the client.
*/
// st 线程环境类，可以获得st线程的id，以便找到对应的客户端
class SrsThreadContext : public ISrsThreadContext
{
private:
	// cache 为每个st线程分配了相应的id号，可用于一一映射，该id可能是client id
    std::map<st_thread_t, int> cache;
public:
    SrsThreadContext();
    virtual ~SrsThreadContext();
public:
    virtual int generate_id();
    virtual int get_id();
    virtual int set_id(int v);
public:
    virtual void clear_cid();
};

/**
* we use memory/disk cache and donot flush when write log.
* it's ok to use it without config, which will log to console, and default trace level.
* when you want to use different level, override this classs, set the protected _level.
*/
// 日志操作类，实现了日志的输出
class SrsFastLog : public ISrsLog, public ISrsReloadHandler
{
// for utest to override
protected:
    // defined in SrsLogLevel. 
    //满足输出标准的日志等级
    int _level;
private:
	// 日志单行数据缓存
    char* log_data;
    // log to file if specified srs_log_file
    // 日志文件句柄
    int fd;
    // whether log to file tank 
    //日志记录模式，true: 输出到屏幕，false: 输出到日志
    bool log_to_file_tank;
    // whether use utc time. 
    //日志时间获取方式，false: 用localtime()，true: 用gmtime()
    bool utc;
public:
    SrsFastLog();
    virtual ~SrsFastLog();
public:
    virtual int initialize();
    virtual void verbose(const char* tag, int context_id, const char* fmt, ...);
    virtual void info(const char* tag, int context_id, const char* fmt, ...);
    virtual void trace(const char* tag, int context_id, const char* fmt, ...);
    virtual void warn(const char* tag, int context_id, const char* fmt, ...);
    virtual void error(const char* tag, int context_id, const char* fmt, ...);
// interface ISrsReloadHandler.
public:
    virtual int on_reload_utc_time();
    virtual int on_reload_log_tank();
    virtual int on_reload_log_level();
    virtual int on_reload_log_file();
private:
    virtual bool generate_header(bool error, const char* tag, int context_id, const char* level_name, int* header_size);
    virtual void write_log(int& fd, char* str_log, int size, int level);
    virtual void open_log_file();
};

#endif

