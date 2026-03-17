#include <mysql.h>
#include <string.h>
#include <stdio.h>

extern "C" {
    // 1. The main function logic
    char* hello_world(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* error) {
        if (args->arg_count != 1 || args->arg_type[0] != STRING_RESULT) {
            strcpy(result, "Usage: hello_world('string')");
            *length = strlen(result);
            return result;
        }

        const char* input = args->args[0];
        unsigned long input_len = args->lengths[0];

        // Format: "Hello World, " + input + "!"
        sprintf(result, "Hello World, %.*s!", (int)input_len, input);
        *length = strlen(result);
        *is_null = 0;
        return result;
    }

    // 2. Initialization function (checks arguments)
    my_bool hello_world_init(UDF_INIT* initid, UDF_ARGS* args, char* message) {
        if (args->arg_count != 1 || args->arg_type[0] != STRING_RESULT) {
            strcpy(message, "hello_world requires exactly one string argument");
            return 1;
        }
        initid->max_length = 255; // Set max return length
        return 0;
    }

    // 3. De-initialization function
    void hello_world_deinit(UDF_INIT* initid) {
        // Cleanup if needed
    }
}
