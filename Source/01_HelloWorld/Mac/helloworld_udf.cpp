#include <mysql.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 초기화 함수
 */
bool helloworld_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
    if (args->arg_count != 0)
    {
        strcpy(message, "helloworld() requires no arguments");
        return 1;
    }

    initid->maybe_null = 0;
    initid->const_item = 1;
    initid->max_length = 29; /* "Hello World Mysql Plugin-UDF" + null terminator */

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
    const char *msg = "Hello World Mysql Plugin-UDF";
    *length = strlen(msg);

    memcpy(result, msg, *length);

    return result;
}

/*
 * 종료 함수
 */
void helloworld_deinit(UDF_INIT *initid)
{
    /* nothing to free */
}

#ifdef __cplusplus
}
#endif
