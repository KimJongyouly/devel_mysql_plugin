#include <mysql.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <iostream>

extern "C" {

    // 상태를 저장할 구조체 (std::vector를 힙에 할당하여 관리)
    typedef std::vector<double> MedianState;

    // 1. 초기화 함수 (메모리 할당)
    my_bool my_median_init(UDF_INIT* initid, UDF_ARGS* args, char* message) {
        if (args->arg_count != 1) {
            strcpy(message, "median() requires one argument");
            return 1;
        }
        // 데이터 저장을 위한 vector 생성
        initid->ptr = (char*)new MedianState();
        return 0;
    }

    // 2. 값 추가 함수
    void my_median_add(UDF_INIT* initid, UDF_ARGS* args, char* is_null, char* error) {
        if (args->args[0] == NULL) return; // NULL 값은 무시
        MedianState* state = (MedianState*)initid->ptr;
        state->push_back(*(double*)args->args[0]);
    }

    // 3. 값 초기화 함수 (새 그룹 처리 시 호출)
    void my_median_clear(UDF_INIT* initid, char* is_null, char* error) {
        MedianState* state = (MedianState*)initid->ptr;
        state->clear();
    }

    // 4. 결과 계산 함수
    double my_median(UDF_INIT* initid, UDF_ARGS* args, char* is_null, char* error) {
        MedianState* state = (MedianState*)initid->ptr;
        if (state->empty()) {
            *is_null = 1;
            return 0.0;
        }

        std::sort(state->begin(), state->end());

        size_t size = state->size();
        if (size % 2 == 0) {
            return ((*state)[size / 2 - 1] + (*state)[size / 2]) / 2.0;
        } else {
            return (*state)[size / 2];
        }
    }

    // 5. 종료 함수 (메모리 해제)
    void my_median_deinit(UDF_INIT* initid) {
        delete (MedianState*)initid->ptr;
    }

}