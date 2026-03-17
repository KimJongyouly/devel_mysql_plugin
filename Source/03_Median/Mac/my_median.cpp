#include <mysql/udf_registration_types.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <new>

// Aggregate UDF context: stores all values for the current group
struct MedianContext {
    std::vector<double>* values;
};

extern "C" {

// 1. 초기화 함수 — UDF 최초 호출 시 1회 실행
bool my_median_init(UDF_INIT* initid, UDF_ARGS* args, char* message) {
    if (args->arg_count != 1) {
        strcpy(message, "my_median() requires exactly one argument");
        return true;
    }
    if (args->arg_type[0] != REAL_RESULT &&
        args->arg_type[0] != INT_RESULT &&
        args->arg_type[0] != DECIMAL_RESULT) {
        strcpy(message, "my_median() requires a numeric argument");
        return true;
    }

    // MySQL이 인자를 REAL_RESULT(double)로 자동 변환하도록 요청
    args->arg_type[0] = REAL_RESULT;

    MedianContext* ctx = new (std::nothrow) MedianContext();
    if (!ctx) {
        strcpy(message, "my_median(): out of memory");
        return true;
    }
    ctx->values = new (std::nothrow) std::vector<double>();
    if (!ctx->values) {
        delete ctx;
        strcpy(message, "my_median(): out of memory");
        return true;
    }

    initid->ptr        = reinterpret_cast<char*>(ctx);
    initid->maybe_null = true;   // 빈 그룹이면 NULL 반환
    initid->const_item = false;
    return false;
}

// 2. 종료 함수 — 쿼리 완료 후 1회 실행, 메모리 해제
void my_median_deinit(UDF_INIT* initid) {
    MedianContext* ctx = reinterpret_cast<MedianContext*>(initid->ptr);
    if (ctx) {
        delete ctx->values;
        delete ctx;
        initid->ptr = nullptr;
    }
}

// 3. 그룹 초기화 함수 — GROUP BY 시 각 그룹 시작마다 호출
//    없으면 이전 그룹의 값이 누적되어 잘못된 결과를 반환함
void my_median_clear(UDF_INIT* initid, unsigned char* is_null, unsigned char* error) {
    MedianContext* ctx = reinterpret_cast<MedianContext*>(initid->ptr);
    ctx->values->clear();
    *is_null = 0;
}

// 4. 첫 번째 로우 처리 — clear 후 첫 로우를 add하는 역할
void my_median_reset(UDF_INIT* initid, UDF_ARGS* args,
                     unsigned char* is_null, unsigned char* error) {
    my_median_clear(initid, is_null, error);
    if (args->args[0]) {
        MedianContext* ctx = reinterpret_cast<MedianContext*>(initid->ptr);
        ctx->values->push_back(*reinterpret_cast<double*>(args->args[0]));
    }
}

// 5. 값 추가 함수 — 그룹 내 두 번째 로우부터 각 로우마다 호출
void my_median_add(UDF_INIT* initid, UDF_ARGS* args,
                   unsigned char* is_null, unsigned char* error) {
    if (args->args[0]) {
        MedianContext* ctx = reinterpret_cast<MedianContext*>(initid->ptr);
        ctx->values->push_back(*reinterpret_cast<double*>(args->args[0]));
    }
}

// 6. 결과 반환 함수 — 그룹의 최종 중앙값 계산 후 반환
double my_median(UDF_INIT* initid, UDF_ARGS* args,
                 unsigned char* is_null, unsigned char* error) {
    MedianContext* ctx = reinterpret_cast<MedianContext*>(initid->ptr);

    if (ctx->values->empty()) {
        *is_null = 1;
        return 0.0;
    }

    std::vector<double>& v = *(ctx->values);
    size_t n = v.size();
    std::sort(v.begin(), v.end());

    if (n % 2 == 0) {
        return (v[n / 2 - 1] + v[n / 2]) / 2.0;
    } else {
        return v[n / 2];
    }
}

} // extern "C"
