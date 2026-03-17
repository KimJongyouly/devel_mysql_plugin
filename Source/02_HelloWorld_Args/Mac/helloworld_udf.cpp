#include <mysql.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 초기화 함수
 */
bool helloworld_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
    if (args->arg_count != 1 || args->arg_type[0] != STRING_RESULT)
    {
        strcpy(message, "helloworld() requires exactly one string argument (name)");
        return 1;
    }

    initid->maybe_null = 1;
    initid->const_item = 0;
    initid->max_length = 300; /* "Welcome " + name + ", HelloWorld Mysql Plugin" */
    initid->ptr = NULL;

    return 0;
}

/*
 * 메인 함수
 */
char *helloworld(UDF_INIT *initid,
                 UDF_ARGS *args,
                 char *result,
                 unsigned long *length,
                 char *is_null,
                 char *error)
{
    if (args->args[0] == NULL)
    {
        *is_null = 1;
        return NULL;
    }

    const char *name = args->args[0];
    unsigned long name_len = args->lengths[0];

    /* "Welcome " (8) + name + ", HelloWorld Mysql Plugin" (25) + null */
    unsigned long buf_len = 8 + name_len + 25 + 1;

    if (initid->ptr)
        free(initid->ptr);

    initid->ptr = (char *)malloc(buf_len);
    if (!initid->ptr)
    {
        *error = 1;
        return NULL;
    }

    *length = (unsigned long)snprintf(initid->ptr, buf_len,
                                      "Welcome %.*s, HelloWorld Mysql Plugin",
                                      (int)name_len, name);

    return initid->ptr;
}

/*
 * 종료 함수
 */
void helloworld_deinit(UDF_INIT *initid)
{
    if (initid->ptr)
    {
        free(initid->ptr);
        initid->ptr = NULL;
    }
}

#ifdef __cplusplus
}
#endif
