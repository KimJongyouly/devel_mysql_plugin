#include <mysql.h>
#include <string.h>

extern "C" {
    my_bool hello_world_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
    void hello_world_deinit(UDF_INIT *initid);
    char *hello_world(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, unsigned char *is_null, unsigned char *error);
}

my_bool hello_world_init(UDF_INIT *initid, UDF_ARGS *args, char *message) {
    return 0; // Return 0 for success
}

void hello_world_deinit(UDF_INIT *initid) {}

char *hello_world(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, unsigned char *is_null, unsigned char *error) {
    const char *str = "Hello World from UDF!";
    *length = strlen(str);
    return (char *)str;
}
