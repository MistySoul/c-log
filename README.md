c-log
---
a  log lib for C/C++ language on **Linux** Platform

How to Use
---
```C
   #include <stdio.h>
   #include "log.h"
   
   #ifndef NDEBUG
   #define DEFAULT_LOG_LEVEL info
   #else
   #define DEFAULT_LOG_LEVEL debug
   #endif
   
   #define DEFAULT_LOG_PATH "./log"
   PROGRAM_LOG_CLASS(main.c);
   
   int main(){
          log_init(DEFAULT_LOG_PATH, "mylog", DEFAULT_LOG_LEVEL, C_TRUE, C_TRUE, 
                50, C_TRUE, 5, 128 * 1024);
                
          int i;
          i = 10;
          char* str = "hello world";
          PROGRAM_LOG_INFO("%s, %d\", str, i);
          log_fini();
          return 0
   }
```
