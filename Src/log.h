#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#ifndef C_BOOL
#define C_BOOL uint8_t
#endif

#ifndef C_TRUE
#define C_TRUE 1
#endif

#ifndef C_FALSE
#define C_FALSE 0
#endif

enum log_level{
	unknown= 0, ///< 未知打印等级类型
	fatal,	///< fatal等级，当设置为此等级时，有一种打印输出（fatal）都有输出
	error,	///< error等级，当设置为此等级时，有两种打印输出（fatal，error）都有输出
	warn,	///< warn等级，当设置为此等级时，有三种打印输出（fatal，error，warn）都有输出
	info,	///< info等级，当设置为此等级时，有四种打印输出（fatal，error，warn，info）都有输出
	trace,	///< Trace等级，当设置为此等级时，有五种打印输出（fatal，error，warn，info，trace）都有输出
	debug,	///< Debug等级，当设置为此等级时，以上六种打印（fatal，error，warn，info，trace，debug）都有输出
};

typedef struct _log_s_{
	pthread_mutex_t mutex;
	char* path;				//日志路径
	char* prefix;			//日志前缀
	FILE* fp;				//当前操作日志文件
	char* file_name;		//当前日志文件名称
	uint8_t level;			//日志级别
	C_BOOL attach_stdout;	//是否向标准输出流打印
	C_BOOL split_by_size;	//是否按照日志文件大小切分
	uint32_t split_size;	//文件切割大小
	C_BOOL enable_cache;	//是否开启缓存
	uint32_t cache_size;	//缓存大小
	char* cache_ptr;		//缓存指针
	char* cache_cur_ptr;	//缓存当前操作位置
	uint64_t flush_time;	//日志刷新时间
	uint32_t recycle_num;	//日志保留文件数
}log_t;

log_t* get_global_logger();

#define gLogger get_global_logger()

/**
 \ param [in] path 输出日志路径，自动mkdir.
 \ param [in] prefix 日志文件模块名.
 \ param [in] level 日志等级
 \ param [in] attach_stdout 是否同时打印到标准输出
 \ param [in] split_by_size 是否按大小切割,true表示按大小切割，false表示按天切割.
 \ param [in] split_size 日志按多大切割,单位MB，如果按天切割，该参数会被忽略.
 \ param [in] enable_cache 是否使能缓存
 \ param [in] recycle_num 日志回滚的个数，0表示表示不回滚.
 \ param [in] cache_size 日志缓存大小.
 */
void log_init(const char* path, const char* prefix, uint8_t level,
				C_BOOL attach_stdout, C_BOOL split_by_size, uint32_t split_size,
				C_BOOL enable_cache, uint32_t recycle_num, uint32_t cache_size );

/**
 \ param [in] no_cache 关闭缓存
*/
void set_no_cache(C_BOOL no_cache);

int logging(int level, C_BOOL time_stamp, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

/**
 关闭日志，释放资源
*/
void log_fini();


int log_debug(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
int log_trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
int log_info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
int log_warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
int log_error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
int log_fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
