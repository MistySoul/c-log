#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>

#include "log.h"

#define STRING_LENGTH 1024		//log_t结构体中字符串变量长度(path, prefix, file_name)
#define DIR_ACCESS 0777
#define LOG_FORMAT "[TID:%ld] %04d-%02d-%02d %02d:%02d:%02d|%s "
#define LOG_CACHE_FLUSH_INTERVAL (60 * 1000 * 1000)		//microsecond
#define _NR_gettid 186

static char* log_level_enum[] = {"unknown", "fatal", "error", "warn", "info", "trace", "debug"};    //日志级别

uint64_t get_current_microsecond()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

struct tm* get_local_time()
{
	time_t t;
	struct tm *lt;
	time(&t);	//获取时间
	//The localtime() function converts the calendar time timep to 
	//broken-time representation, expressed relative to the user’s 
	//specified timezone.The return value points to a statically 
	//allocated  struct  which might be overwritten by subsequent 
	//calls to any of the date and time functions.
	lt = localtime(&t);	//转为年月日时分秒结构
	return lt;
}

/**
 ** Global Method
 **/
static C_BOOL isDir(const char* dir)
{
    struct stat buf;
    if (stat(dir, &buf) != 0){
        return C_FALSE;
    }
    return S_ISDIR(buf.st_mode);
}

static int make_dir(const char* dir)
{
	char path[STRING_LENGTH] = {0};
	int len;
	char *ptr;

	strncpy(path, dir, sizeof(path) - 1);
	len = (int)strlen(path);
	if(path[len - 1] != '/'){
		path[len] = '/';
		path[len + 1] ='\0';
	}

	if(isDir(path)){
		return 0;
	}

	ptr = path;
	while( ptr && (*ptr == '/' || *ptr == '.'))
		++ptr;

	while(ptr && (ptr = strchr(ptr, '/')) != NULL){
		*ptr = '\0';
		if(!isDir(path)){
			if( mkdir(path, DIR_ACCESS) == -1){
				fprintf(stderr, "mkdir %s error\n", path);
				return -1;
			}
		}
		*ptr++ = '/';
	}

	if(ptr && (mkdir(path, DIR_ACCESS) == -1))
		return -1;

	return 0;
}

static void get_log_file_name(int index)
{
	//按大小切割
	if(gLogger->split_by_size){
		char buf[STRING_LENGTH] = {0};

		if(index == 0)
			snprintf(buf, sizeof(buf), "%s_log_current.log", gLogger->prefix);
		else
			snprintf(buf, sizeof(buf), "%s_log_%d.log", gLogger->prefix, index);

		strcpy(gLogger->file_name, buf);
	}else{
		//按时间切割
		struct tm* lt;
		lt = get_local_time();
		char buf[STRING_LENGTH] = {0};
		snprintf(buf, sizeof(buf), "%s_log_%04d%02d%02d.log", gLogger->prefix, 
			lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday - index);

		strcpy(gLogger->file_name, buf);
	}
}

log_t* get_global_logger()
{
	static log_t logger;
	//初始化Posix互斥锁
	pthread_mutex_init(&(logger.mutex), NULL);
	return &logger;
}

//外部加锁保护
void flush_cache()
{
	if(gLogger->fp == NULL)
		return;

	uint64_t begin_time = get_current_microsecond();
	gLogger->flush_time = begin_time;

	//待写缓存中的数据长度
	size_t nmemb = gLogger->cache_cur_ptr - gLogger->cache_ptr;

	fwrite(gLogger->cache_ptr, 1, nmemb, gLogger->fp);
	
	uint64_t end_time = get_current_microsecond();
	uint64_t write_cost = end_time - begin_time;

	// 大于500ms就打印日志
	if( write_cost > 500000 ) {
		char tmp[256] = {0};
		struct tm* lt;
		lt = get_local_time();
		snprintf(tmp, sizeof(tmp), LOG_FORMAT "Log.c::%s(%d) write log cost %lu microsecond\n",
			(long int)syscall(_NR_gettid), lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min,
			lt->tm_sec, log_level_enum[(int)warn], __FUNCTION__, __LINE__, write_cost);

		fwrite(tmp, 1, strlen(tmp), gLogger->fp);
	}
}

void free_string_memory()
{
	if(gLogger->path)
		free(gLogger->path);
	if(gLogger->prefix)
		free(gLogger->prefix);
	if(gLogger->file_name)
		free(gLogger->file_name);

	gLogger->path = NULL;
	gLogger->prefix = NULL;
	gLogger->file_name = NULL;
}

//param[out]:target
void path_join_file(char* target)
{
	size_t path_len, file_name_len;
	path_len = strlen(gLogger->path);
	file_name_len = strlen(gLogger->file_name);

	//区分path是否以/结尾
	if(gLogger->path[path_len - 1] == '/'){
		strncpy(target, gLogger->path, path_len);
		strncpy(target + path_len, gLogger->file_name, file_name_len);
		target[path_len + file_name_len] = '\0';
	}else{
		strncpy(target, gLogger->path, path_len);
		target[path_len] = '/';
		strncpy(target + path_len + 1, gLogger->file_name, file_name_len);
		target[path_len + file_name_len + 1] ='\0';
	}
}

uint32_t get_current_log_size()
{
	char buf[STRING_LENGTH * 2] = {0};
	path_join_file(buf);
	struct stat stat_buf;
	stat(buf, &stat_buf);
	return (uint32_t)stat_buf.st_size;
}

void log_file_rolling()
{
	C_BOOL need_rolling;
	need_rolling = C_FALSE;

	if(gLogger->split_by_size){
		if(get_current_log_size() > gLogger->split_size){
			need_rolling = C_TRUE;
			if(fclose(gLogger->fp) != 0){
				fprintf(stderr, "close %s error\n", gLogger->file_name);
				return;
			}

			uint32_t i;
			//依次对已有的日志文件执行rename操作
			for(i = gLogger->recycle_num; i >= 0; i--){
				char old_file_path[STRING_LENGTH * 2] = {0};
				char new_file_path[STRING_LENGTH * 2] = {0};

				get_log_file_name(i);
				path_join_file(old_file_path);
				get_log_file_name(i + 1);
				path_join_file(new_file_path);

				rename(old_file_path, new_file_path);
			}

			char file_absolute_path[STRING_LENGTH * 2] = {0};
			get_log_file_name(0);
			path_join_file(file_absolute_path);

			gLogger->fp = fopen(file_absolute_path, "a+");
			if(gLogger->fp == NULL){
				fprintf(stderr, "open file %s error\n", file_absolute_path);
				return;
			}
		}
	}else{
		//记录切换前文件名称
		char tmp[STRING_LENGTH] = {0};
		strcpy(tmp, gLogger->file_name);
		get_log_file_name(0);

		//文件名称发生变化，即日期变化
		if(strcmp(gLogger->file_name, tmp) != 0){
			need_rolling = C_TRUE;
			if(fclose(gLogger->fp) != 0){
				fprintf(stderr, "close %s error\n", gLogger->file_name);
				return;
			}

			char file_absolute_path[STRING_LENGTH * 2] = {0};
			path_join_file(file_absolute_path);

			gLogger->fp = fopen(file_absolute_path, "a+");
			if(gLogger->fp == NULL){
				fprintf(stderr, "open file %s error\n", file_absolute_path);
				return;
			}
		}
	}

	//删除多余的日志文件，仅存在一个多余的文件
	if(need_rolling){
		char need_remove_file[STRING_LENGTH * 2] = {0};
		get_log_file_name(gLogger->recycle_num + 1);
		path_join_file(need_remove_file);
		remove(need_remove_file);
	}
}

void log_init(const char* path, const char* prefix, uint8_t level,
				C_BOOL attach_stdout, C_BOOL split_by_size, uint32_t split_size,
				C_BOOL enable_cache, uint32_t recycle_num, uint32_t cache_size )
{
	pthread_mutex_lock(&(gLogger->mutex));
	
	if(gLogger->path != NULL && gLogger->prefix != NULL){
		if(strcmp(path, gLogger->path) == 0 ||
			strcmp(prefix, gLogger->prefix) == 0 ||
			level == gLogger->level ||
			split_size == gLogger->split_size ||
			recycle_num == gLogger->recycle_num ||
			cache_size == gLogger->cache_size){
			pthread_mutex_unlock(&(gLogger->mutex));
			return;
		}

		if((strcmp(path, gLogger->path) != 0 || strcmp(prefix, gLogger->prefix) != 0) && gLogger->fp != NULL)
		{
			flush_cache();
			fclose(gLogger->fp);
		}
	}

	//字符串变量内存申请初始化
	gLogger->path = (char*) malloc(STRING_LENGTH);
	gLogger->prefix = (char*) malloc(STRING_LENGTH);
	gLogger->file_name = (char*) malloc(STRING_LENGTH);
	memset(gLogger->path, 0, STRING_LENGTH);
	memset(gLogger->prefix, 0, STRING_LENGTH);
	memset(gLogger->file_name, 0, STRING_LENGTH);

	//初始化
	strcpy(gLogger->path, path);
	strcpy(gLogger->prefix, prefix);
	gLogger->level = level;
	gLogger->attach_stdout = attach_stdout;
	gLogger->split_by_size = split_by_size;
	gLogger->split_size = split_size > 1024 ? 1024 * 1024 * 1024 : split_size * 1024 * 1024;
	gLogger->enable_cache = enable_cache;
	gLogger->recycle_num = recycle_num;
	gLogger->cache_size = cache_size;

	make_dir(gLogger->path);

	get_log_file_name(0);
	char file_absolute_path[STRING_LENGTH * 2] = {0};
	path_join_file(file_absolute_path);

	gLogger->fp = fopen(file_absolute_path, "a+");
	if(gLogger->fp == NULL){
		fprintf(stderr, "open file %s error\n", file_absolute_path);
		free_string_memory();
		pthread_mutex_unlock(&(gLogger->mutex));
		return;
	}

	if(fseek(gLogger->fp, 0, SEEK_END) == -1){
		fprintf(stderr, "fseek %s error\n", file_absolute_path);
		free_string_memory();
		pthread_mutex_unlock(&(gLogger->mutex));
		return;
	}

	if(gLogger->cache_ptr != NULL){
		free(gLogger->cache_ptr);
		gLogger->cache_ptr = (char*) malloc(gLogger->cache_size);
		gLogger->cache_cur_ptr = gLogger->cache_ptr;
		memset(gLogger->cache_ptr, 0, gLogger->cache_size);
	}

	gLogger->cache_ptr = (char*) malloc(gLogger->cache_size);
	gLogger->cache_cur_ptr = gLogger->cache_ptr;
	memset(gLogger->cache_ptr, 0, gLogger->cache_size);
	log_file_rolling();
	pthread_mutex_unlock(&(gLogger->mutex));
}

void set_no_cache(C_BOOL no_cache)
{
	gLogger->enable_cache = !no_cache;

	pthread_mutex_lock(&(gLogger->mutex));
	flush_cache();
	pthread_mutex_unlock(&(gLogger->mutex));
}

int log_to_file(char* log_cache)
{
	assert(log_cache != NULL);
	assert(gLogger->cache_ptr != NULL);
	assert(gLogger->cache_cur_ptr != NULL);

	C_BOOL need_rolling;
	need_rolling = C_FALSE;

	int len = strlen(log_cache);

	if(gLogger->enable_cache){
		do{
			if(gLogger->cache_cur_ptr + len > gLogger->cache_ptr + gLogger->cache_size ||
				(get_current_microsecond() - gLogger->flush_time) > LOG_CACHE_FLUSH_INTERVAL)
			{
				flush_cache();
				need_rolling = C_TRUE;
			}

			if(len > gLogger->cache_size)
			{
				fwrite(log_cache, 1, len, gLogger->fp);
				need_rolling = C_TRUE;
				break;
			}else{
				memcpy(gLogger->cache_cur_ptr, log_cache, len);
				gLogger->cache_cur_ptr += len;
				break;
			}
		}while(1);
	}else{
		fwrite(log_cache, 1, len, gLogger->fp);
		need_rolling = C_TRUE;
	}

	if(need_rolling)
		log_file_rolling();

	return len;
}

int logging_internal(int level, C_BOOL time_stamp, const char* fmt, va_list args)
{
	assert(level >= unknown);
	assert(level <= debug);
	assert(fmt != NULL);

	if( level > gLogger->level)
		return 0;

	pthread_mutex_lock(&(gLogger->mutex));

	char buffer[STRING_LENGTH * 8] = {0};
	if(time_stamp){
		//并发调用时，频繁调用获取系统时间将会导致CPU,锁占用非常高
		//采用间隔500ms之后，才真正调用获取系统时间，日志打印精度不需要特别高
		static struct tm* lt ;
		lt = get_local_time();
		static uint64_t pre_time;
		pre_time = get_current_microsecond();
		uint64_t cur_time = get_current_microsecond();
		if(cur_time >= (pre_time + 500000)){
			lt = get_local_time();
			pre_time = cur_time;
		}

		snprintf(buffer, sizeof(buffer), LOG_FORMAT, (long int)syscall(_NR_gettid),
			lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour,
			lt->tm_min, lt->tm_sec, log_level_enum[level]);
	}

	size_t len = strlen(buffer); //TID和时间戳前缀
	vsnprintf(buffer + len , sizeof(buffer) - len, fmt, args);

	if(gLogger->attach_stdout)
		fprintf(stdout, buffer);

	int ret = log_to_file(buffer);
	pthread_mutex_unlock(&(gLogger->mutex));

	return ret;
}

int logging(int level, C_BOOL time_stamp, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int ret = logging_internal(level, time_stamp, fmt, args);
	va_end(args);

	return ret;
}

void log_fini()
{
	flush_cache();
	if(gLogger->fp != NULL){
		if(fclose(gLogger->fp) != 0){
			fprintf(stderr, "close %s error\n", gLogger->file_name);
			return;
		}
	}

	free_string_memory();

	if(gLogger->cache_ptr != NULL)
		free(gLogger->cache_ptr);

	gLogger->cache_cur_ptr = NULL;
	gLogger->cache_ptr = NULL;
}

#define LOG_MACRO_VA(name, logLevel) \
int name(const char* fmt, ...) \
{ \
    va_list args; \
    va_start(args, fmt); \
    int ret = logging_internal( logLevel, C_TRUE, fmt, args ); \
    va_end(args); \
    return ret; \
} 

LOG_MACRO_VA(log_debug, debug)
LOG_MACRO_VA(log_trace, trace)
LOG_MACRO_VA(log_info, info)
LOG_MACRO_VA(log_warn, warn)
LOG_MACRO_VA(log_error, error)
LOG_MACRO_VA(log_fatal, fatal)
