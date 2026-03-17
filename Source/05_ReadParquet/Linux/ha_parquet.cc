/* Copyright (c) 2024, Study Project.

   READ_PARQUET storage engine implementation — MariaDB 10.6 version.

   Design overview
   ───────────────
   • create()   – Writes a tiny sidecar file (<tablepath>.prq) that stores the
                  absolute path to the .parquet file (from COMMENT clause, or
                  defaults to <tablepath>.parquet).
   • open()     – Reads the sidecar, opens the parquet file with Apache Arrow,
                  loads the entire table into an arrow::Table in memory.
   • rnd_init() – Resets the row cursor to 0.
   • rnd_next() – Calls fill_row() to map one Arrow row into the MariaDB buffer,
                  advances cursor; returns HA_ERR_END_OF_FILE when done.
   • fill_row() – Type-dispatches every Arrow column type to the appropriate
                  MySQL Field::store() variant.
   • close()    – Releases the arrow::Table (shared_ptr → automatic).

   Supported Arrow → MySQL type mappings
   ──────────────────────────────────────
   Arrow INT8/16/32/64, UINT8/16/32/64 → integer fields
   Arrow FLOAT / DOUBLE                → float / double fields
   Arrow STRING / LARGE_STRING         → VARCHAR / TEXT (UTF-8)
   Arrow BINARY / LARGE_BINARY         → BLOB / VARBINARY
   Arrow BOOL                          → TINYINT(1)
   Arrow DATE32                        → DATE
   Arrow DATE64                        → DATE  (milliseconds → days)
   Arrow TIMESTAMP(s/ms/us/ns)         → DATETIME(6)
   Arrow DECIMAL128                    → DECIMAL / VARCHAR fallback
   Arrow others                        → VARCHAR (Arrow scalar ToString)

   Dependencies (link with):
     -larrow  -lparquet
*/

#define MYSQL_SERVER 1

/* ── Apache Arrow / Parquet headers ─────────────────────────────────────── */
/*
 * Arrow/Parquet 헤더를 MariaDB 헤더보다 먼저 포함해야 합니다.
 * MariaDB의 sql_i_s.h 가 Show::Type 을 정의하는데, 이것이 parquet/types.h 의
 * parquet::Type 과 네임스페이스 충돌을 일으킵니다.
 * include 순서를 Arrow/Parquet → MariaDB 로 고정해 충돌을 방지합니다.
 */
#include <arrow/api.h>                 // arrow::Table, arrow::Status, ...
#include <arrow/io/api.h>              // arrow::io::ReadableFile
#include <arrow/type.h>                // arrow::Type, arrow::TimestampType
#include <arrow/type_traits.h>
#include <parquet/arrow/reader.h>      // parquet::arrow::OpenFile / FileReader

/* ── Typed array includes (one per primitive type we handle) ─────────────── */
#include <arrow/array/array_primitive.h>   // Int8Array … DoubleArray, BooleanArray
#include <arrow/array/array_binary.h>      // StringArray, BinaryArray, ...
#include <arrow/array/array_dict.h>
#include <arrow/scalar.h>                  // GetScalar (fallback)
#include <parquet/exception.h>

/* ── MariaDB server headers ─────────────────────────────────────────────── */
#include "ha_parquet.h"

#include "my_dbug.h"           // DBUG_ENTER / DBUG_RETURN
#include "my_sys.h"            // my_error
#include "mysql/plugin.h"      // mysql_declare_plugin / maria_declare_plugin
#include "sql_class.h"         // THD
#include "field.h"             // Field (full definition for store/set_null)
#include "log.h"               // sql_print_error
#include "m_string.h"          // my_stpcpy

/* ── C / POSIX headers ──────────────────────────────────────────────────── */
#include <cstdio>
#include <cstring>
#include <ctime>               // gmtime_r

using std::string;

/* ═══════════════════════════════════════════════════════════════════════════
 * Plugin globals
 * ═══════════════════════════════════════════════════════════════════════════ */

static handlerton *parquet_hton;

/* ═══════════════════════════════════════════════════════════════════════════
 * Sidecar file helpers  (<tablepath>.prq)
 * ═══════════════════════════════════════════════════════════════════════════ */

/** Build the sidecar (.prq) path from the base table path. */
static void make_prq_path(const char *table_path, char *out, size_t out_len) {
  snprintf(out, out_len, "%s.prq", table_path);
}

/** Write the parquet file path into the sidecar. Returns true on success. */
static bool write_prq_meta(const char *table_path, const char *parquet_path) {
  char prq[FN_REFLEN];
  make_prq_path(table_path, prq, sizeof(prq));

  FILE *f = fopen(prq, "w");
  if (!f) return false;
  fprintf(f, "%s\n", parquet_path);
  fclose(f);
  return true;
}

/* static */ bool ha_parquet::read_prq_meta(const char *table_path,
                                            char *out_path) {
  char prq[FN_REFLEN];
  make_prq_path(table_path, prq, sizeof(prq));

  FILE *f = fopen(prq, "r");
  if (!f) return false;
  bool ok = (fgets(out_path, FN_REFLEN, f) != nullptr);
  fclose(f);

  if (ok) {
    /* Strip trailing newline */
    size_t len = strlen(out_path);
    if (len > 0 && out_path[len - 1] == '\n') out_path[len - 1] = '\0';
  }
  return ok;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Date helper  (Howard Hinnant's civil-from-days algorithm)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* static */
void ha_parquet::days_to_mysql_date(int32_t days, MYSQL_TIME *out) {
  memset(out, 0, sizeof(*out));
  out->time_type = MYSQL_TIMESTAMP_DATE;

  /* Civil date from days-since-epoch (1970-01-01 = day 0) */
  int32_t z   = days + 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = static_cast<uint32_t>(z - era * 146097);                 /* [0, 146096] */
  uint32_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;         /* [0, 399]    */
  int32_t  y   = static_cast<int32_t>(yoe) + era * 400;
  uint32_t doy = doe - (365*yoe + yoe/4 - yoe/100);                       /* [0, 365]    */
  uint32_t mp  = (5*doy + 2) / 153;                                        /* [0, 11]     */
  uint32_t d   = doy - (153*mp + 2) / 5 + 1;                              /* [1, 31]     */
  uint32_t m   = (mp < 10) ? mp + 3 : mp - 9;                             /* [1, 12]     */
  y += (m <= 2);

  out->year  = static_cast<uint>(y);
  out->month = m;
  out->day   = d;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Shared state (Parquet_share)
 * ═══════════════════════════════════════════════════════════════════════════ */

Parquet_share *ha_parquet::get_share() {
  Parquet_share *tmp_share;

  lock_shared_ha_data();
  tmp_share = static_cast<Parquet_share *>(get_ha_share_ptr());
  if (!tmp_share) {
    tmp_share = new Parquet_share();
    if (!tmp_share) goto err;
    set_ha_share_ptr(static_cast<Handler_share *>(tmp_share));
  }
err:
  unlock_shared_ha_data();
  return tmp_share;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Constructor
 * ═══════════════════════════════════════════════════════════════════════════ */

ha_parquet::ha_parquet(handlerton *hton, TABLE_SHARE *table_arg)
    : handler(hton, table_arg) {
  /* ref_length is used by position() / rnd_pos() to store a row index */
  ref_length = sizeof(int64_t);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * create()  –  Called when user runs CREATE TABLE ... ENGINE=READ_PARQUET
 *
 * MariaDB API: no dd::Table* parameter (Data Dictionary is MySQL 8.x only).
 * ═══════════════════════════════════════════════════════════════════════════ */

int ha_parquet::create(const char *name, TABLE * /*form*/,
                       HA_CREATE_INFO *create_info) {
  DBUG_ENTER("ha_parquet::create");

  char parquet_path[FN_REFLEN];

  /*
   * Determine the parquet file path.
   * Priority:
   *   1) COMMENT '/absolute/or/relative/path.parquet'
   *   2) Default: <MariaDB data dir table path>.parquet
   */
  if (create_info->comment.str && create_info->comment.length > 0) {
    /* User provided an explicit path in COMMENT */
    size_t len = std::min(create_info->comment.length, (size_t)FN_REFLEN - 1);
    memcpy(parquet_path, create_info->comment.str, len);
    parquet_path[len] = '\0';
  } else {
    /* Default: same name as the MariaDB table, with .parquet extension */
    snprintf(parquet_path, sizeof(parquet_path), "%s.parquet", name);
  }

  if (!write_prq_meta(name, parquet_path)) {
    my_error(ER_CANT_CREATE_TABLE, MYF(0), name, errno);
    DBUG_RETURN(HA_ERR_CRASHED_ON_USAGE);
  }

  DBUG_RETURN(0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * open()  –  Opens the table; loads the parquet file into arrow_table
 *
 * MariaDB API: no dd::Table* parameter.
 * ═══════════════════════════════════════════════════════════════════════════ */

int ha_parquet::open(const char *name, int /*mode*/, uint /*test_if_locked*/) {
  DBUG_ENTER("ha_parquet::open");

  /* ── shared state ── */
  if (!(share = get_share())) DBUG_RETURN(HA_ERR_OUT_OF_MEM);
  thr_lock_data_init(&share->lock, &lock, nullptr);

  /* ── read sidecar to get parquet path ── */
  char parquet_path[FN_REFLEN];
  if (!read_prq_meta(name, parquet_path)) {
    /* Sidecar missing — try <name>.parquet as a last resort */
    snprintf(parquet_path, sizeof(parquet_path), "%s.parquet", name);
  }

  /* ── open the parquet file via Apache Arrow ── */
  auto file_result = arrow::io::ReadableFile::Open(parquet_path);
  if (!file_result.ok()) {
    sql_print_error("READ_PARQUET: cannot open '%s': %s", parquet_path,
                    file_result.status().ToString().c_str());
    DBUG_RETURN(HA_ERR_NO_SUCH_TABLE);
  }

  auto reader_result = parquet::arrow::OpenFile(
      *file_result, arrow::default_memory_pool());
  if (!reader_result.ok()) {
    sql_print_error("READ_PARQUET: cannot parse '%s': %s", parquet_path,
                    reader_result.status().ToString().c_str());
    DBUG_RETURN(HA_ERR_CRASHED_ON_USAGE);
  }
  std::unique_ptr<parquet::arrow::FileReader> reader =
      std::move(reader_result).ValueUnsafe();

  /* Read the entire file as a single in-memory Arrow Table */
  auto table_result = reader->ReadTable();
  if (!table_result.ok()) {
    sql_print_error("READ_PARQUET: ReadTable failed for '%s': %s", parquet_path,
                    table_result.status().ToString().c_str());
    DBUG_RETURN(HA_ERR_CRASHED_ON_USAGE);
  }
  std::shared_ptr<arrow::Table> tbl = std::move(table_result).ValueUnsafe();

  arrow_table = tbl;
  total_rows  = tbl->num_rows();
  current_row = 0;

  DBUG_RETURN(0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * close()
 * ═══════════════════════════════════════════════════════════════════════════ */

int ha_parquet::close() {
  DBUG_ENTER("ha_parquet::close");
  arrow_table.reset();   /* Release Arrow memory */
  total_rows  = 0;
  current_row = 0;
  DBUG_RETURN(0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * delete_table()  –  Remove the sidecar .prq file
 *
 * MariaDB API: no dd::Table* parameter.
 * ═══════════════════════════════════════════════════════════════════════════ */

int ha_parquet::delete_table(const char *name) {
  DBUG_ENTER("ha_parquet::delete_table");
  char prq[FN_REFLEN];
  make_prq_path(name, prq, sizeof(prq));
  ::remove(prq);   /* best-effort; ignore error */
  DBUG_RETURN(0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * fill_row()  –  Map one Arrow row into the MariaDB row buffer
 *
 * The MariaDB row buffer layout is:
 *   [ null_bytes | field0_data | field1_data | ... ]
 *
 * We iterate over every MariaDB Field, find the matching Arrow column by
 * position (col 0 → field 0, etc.), then call the appropriate
 * Field::store() variant depending on the Arrow array type.
 * ═══════════════════════════════════════════════════════════════════════════ */

int ha_parquet::fill_row(uchar *buf, int64_t row_idx) {
  /* Zero-out null bytes */
  memset(buf, 0, table->s->null_bytes);

  /* For each MariaDB column … */
  for (uint col = 0; col < table->s->fields; ++col) {
    Field *field = table->field[col];

    /* More MariaDB columns than Arrow columns → NULL */
    if (static_cast<int>(col) >= arrow_table->num_columns()) {
      field->set_null();
      continue;
    }

    /* ── locate the right Arrow chunk and intra-chunk offset ── */
    const std::shared_ptr<arrow::ChunkedArray> &chunked =
        arrow_table->column(static_cast<int>(col));

    int64_t remaining = row_idx;
    std::shared_ptr<arrow::Array> arr;
    int64_t arr_offset = 0;

    for (int c = 0; c < chunked->num_chunks(); ++c) {
      auto candidate = chunked->chunk(c);
      if (remaining < candidate->length()) {
        arr        = candidate;
        arr_offset = remaining;
        break;
      }
      remaining -= candidate->length();
    }

    if (!arr) {
      field->set_null();
      continue;
    }

    /* Arrow null value → MariaDB NULL */
    if (arr->IsNull(arr_offset)) {
      field->set_null();
      continue;
    }

    field->set_notnull();

    /* ── type dispatch ──────────────────────────────────────────────── */
    arrow::Type::type tid = arr->type_id();

    switch (tid) {

      /* ── Boolean ─────────────────────────────────────────────────── */
      case arrow::Type::BOOL: {
        auto *a = static_cast<arrow::BooleanArray *>(arr.get());
        field->store(static_cast<longlong>(a->Value(arr_offset) ? 1 : 0),
                     /*unsigned=*/false);
        break;
      }

      /* ── Signed integers ─────────────────────────────────────────── */
      case arrow::Type::INT8: {
        auto *a = static_cast<arrow::Int8Array *>(arr.get());
        field->store(static_cast<longlong>(a->Value(arr_offset)), false);
        break;
      }
      case arrow::Type::INT16: {
        auto *a = static_cast<arrow::Int16Array *>(arr.get());
        field->store(static_cast<longlong>(a->Value(arr_offset)), false);
        break;
      }
      case arrow::Type::INT32: {
        auto *a = static_cast<arrow::Int32Array *>(arr.get());
        field->store(static_cast<longlong>(a->Value(arr_offset)), false);
        break;
      }
      case arrow::Type::INT64: {
        auto *a = static_cast<arrow::Int64Array *>(arr.get());
        field->store(static_cast<longlong>(a->Value(arr_offset)), false);
        break;
      }

      /* ── Unsigned integers ───────────────────────────────────────── */
      case arrow::Type::UINT8: {
        auto *a = static_cast<arrow::UInt8Array *>(arr.get());
        field->store(static_cast<longlong>(a->Value(arr_offset)), true);
        break;
      }
      case arrow::Type::UINT16: {
        auto *a = static_cast<arrow::UInt16Array *>(arr.get());
        field->store(static_cast<longlong>(a->Value(arr_offset)), true);
        break;
      }
      case arrow::Type::UINT32: {
        auto *a = static_cast<arrow::UInt32Array *>(arr.get());
        field->store(static_cast<longlong>(a->Value(arr_offset)), true);
        break;
      }
      case arrow::Type::UINT64: {
        auto *a = static_cast<arrow::UInt64Array *>(arr.get());
        field->store(static_cast<longlong>(a->Value(arr_offset)), true);
        break;
      }

      /* ── Floating point ──────────────────────────────────────────── */
      case arrow::Type::HALF_FLOAT: {
        /*
         * Arrow stores HALF_FLOAT as uint16_t (IEEE 754 binary16).
         * Convert manually to float32 then to double.
         */
        auto *a = static_cast<arrow::HalfFloatArray *>(arr.get());
        uint16_t hf  = a->Value(arr_offset);
        uint32_t exp  = (hf >> 10) & 0x1f;
        uint32_t mant = hf & 0x3ff;
        uint32_t sign = (hf >> 15) & 1u;
        uint32_t f32;
        if (exp == 0)
          f32 = (sign << 31) | (mant << 13);               /* subnormal */
        else if (exp == 31)
          f32 = (sign << 31) | 0x7f800000u | (mant << 13); /* inf / nan */
        else
          f32 = (sign << 31) | ((exp + 112) << 23) | (mant << 13);
        float fval;
        memcpy(&fval, &f32, sizeof(fval));
        field->store(static_cast<double>(fval));
        break;
      }
      case arrow::Type::FLOAT: {
        auto *a = static_cast<arrow::FloatArray *>(arr.get());
        field->store(static_cast<double>(a->Value(arr_offset)));
        break;
      }
      case arrow::Type::DOUBLE: {
        auto *a = static_cast<arrow::DoubleArray *>(arr.get());
        field->store(a->Value(arr_offset));
        break;
      }

      /* ── String / text ───────────────────────────────────────────── */
      case arrow::Type::STRING: {
        auto *a = static_cast<arrow::StringArray *>(arr.get());
        auto sv = a->GetView(arr_offset);
        field->store(sv.data(), sv.size(), &my_charset_utf8mb4_general_ci);
        break;
      }
      case arrow::Type::LARGE_STRING: {
        auto *a = static_cast<arrow::LargeStringArray *>(arr.get());
        auto sv = a->GetView(arr_offset);
        field->store(sv.data(), sv.size(), &my_charset_utf8mb4_general_ci);
        break;
      }

      /* ── Binary ──────────────────────────────────────────────────── */
      case arrow::Type::BINARY: {
        auto *a = static_cast<arrow::BinaryArray *>(arr.get());
        auto sv = a->GetView(arr_offset);
        field->store(sv.data(), sv.size(), &my_charset_bin);
        break;
      }
      case arrow::Type::LARGE_BINARY: {
        auto *a = static_cast<arrow::LargeBinaryArray *>(arr.get());
        auto sv = a->GetView(arr_offset);
        field->store(sv.data(), sv.size(), &my_charset_bin);
        break;
      }

      /* ── Date (days since Unix epoch 1970-01-01) ─────────────────── */
      case arrow::Type::DATE32: {
        auto *a = static_cast<arrow::Date32Array *>(arr.get());
        MYSQL_TIME mt;
        days_to_mysql_date(a->Value(arr_offset), &mt);
        field->store_time(&mt);
        break;
      }
      case arrow::Type::DATE64: {
        /* DATE64 is milliseconds since epoch; divide to get days */
        auto *a = static_cast<arrow::Date64Array *>(arr.get());
        int32_t days = static_cast<int32_t>(a->Value(arr_offset) / 86400000LL);
        MYSQL_TIME mt;
        days_to_mysql_date(days, &mt);
        field->store_time(&mt);
        break;
      }

      /* ── Timestamp ───────────────────────────────────────────────── */
      case arrow::Type::TIMESTAMP: {
        auto *a  = static_cast<arrow::TimestampArray *>(arr.get());
        int64_t  ts_val = a->Value(arr_offset);
        auto     ts_type = std::static_pointer_cast<arrow::TimestampType>(
                               arr->type());

        /* Normalise to seconds + microseconds */
        int64_t secs, usecs;
        switch (ts_type->unit()) {
          case arrow::TimeUnit::SECOND:
            secs = ts_val; usecs = 0; break;
          case arrow::TimeUnit::MILLI:
            secs  = ts_val / 1000LL;
            usecs = (ts_val % 1000LL) * 1000LL;
            break;
          case arrow::TimeUnit::MICRO:
            secs  = ts_val / 1000000LL;
            usecs = ts_val % 1000000LL;
            break;
          case arrow::TimeUnit::NANO:
            secs  = ts_val / 1000000000LL;
            usecs = (ts_val % 1000000000LL) / 1000LL;
            break;
          default:
            secs = ts_val; usecs = 0; break;
        }

        /* Convert Unix seconds to broken-down UTC time */
        MYSQL_TIME mt;
        memset(&mt, 0, sizeof(mt));
        mt.time_type  = MYSQL_TIMESTAMP_DATETIME;
        mt.second_part = static_cast<ulong>(usecs);

        time_t t = static_cast<time_t>(secs);
        struct tm tm_buf;
        gmtime_r(&t, &tm_buf);
        mt.year   = static_cast<uint>(tm_buf.tm_year + 1900);
        mt.month  = static_cast<uint>(tm_buf.tm_mon  + 1);
        mt.day    = static_cast<uint>(tm_buf.tm_mday);
        mt.hour   = static_cast<uint>(tm_buf.tm_hour);
        mt.minute = static_cast<uint>(tm_buf.tm_min);
        mt.second = static_cast<uint>(tm_buf.tm_sec);
        field->store_time(&mt);
        break;
      }

      /* ── Decimal ─────────────────────────────────────────────────── */
      case arrow::Type::DECIMAL128: {
        auto *a = static_cast<arrow::Decimal128Array *>(arr.get());
        std::string s = a->FormatValue(arr_offset);
        field->store(s.c_str(), s.size(), &my_charset_latin1);
        break;
      }
      case arrow::Type::DECIMAL256: {
        auto *a = static_cast<arrow::Decimal256Array *>(arr.get());
        std::string s = a->FormatValue(arr_offset);
        field->store(s.c_str(), s.size(), &my_charset_latin1);
        break;
      }

      /* ── Fallback: use Arrow's generic scalar→string conversion ──── */
      default: {
        auto scalar_res = arr->GetScalar(arr_offset);
        if (scalar_res.ok()) {
          std::string s = scalar_res.ValueOrDie()->ToString();
          field->store(s.c_str(), s.size(), &my_charset_utf8mb4_general_ci);
        } else {
          field->set_null();
        }
        break;
      }
    } /* switch (tid) */
  }   /* for each field */

  return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Full-table scan
 * ═══════════════════════════════════════════════════════════════════════════ */

int ha_parquet::rnd_init(bool /*scan*/) {
  DBUG_ENTER("ha_parquet::rnd_init");
  current_row = 0;
  DBUG_RETURN(0);
}

int ha_parquet::rnd_next(uchar *buf) {
  DBUG_ENTER("ha_parquet::rnd_next");
  if (current_row >= total_rows) DBUG_RETURN(HA_ERR_END_OF_FILE);

  int rc = fill_row(buf, current_row);
  if (rc == 0) {
    /* Store the current row index into handler::ref so position() works */
    my_store_ptr(ref, ref_length, static_cast<my_off_t>(current_row));
    ++current_row;
  }
  DBUG_RETURN(rc);
}

int ha_parquet::rnd_end() {
  DBUG_ENTER("ha_parquet::rnd_end");
  DBUG_RETURN(0);
}

/* ── Random access by saved position ─────────────────────────────────────── */

void ha_parquet::position(const uchar * /*record*/) {
  DBUG_ENTER("ha_parquet::position");
  /* ref was already written in rnd_next(); nothing to do here */
  DBUG_VOID_RETURN;
}

int ha_parquet::rnd_pos(uchar *buf, uchar *pos) {
  DBUG_ENTER("ha_parquet::rnd_pos");
  int64_t row_idx = static_cast<int64_t>(my_get_ptr(pos, ref_length));
  if (row_idx < 0 || row_idx >= total_rows) DBUG_RETURN(HA_ERR_END_OF_FILE);
  DBUG_RETURN(fill_row(buf, row_idx));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Optimizer / server callbacks
 * ═══════════════════════════════════════════════════════════════════════════ */

int ha_parquet::info(uint flag) {
  DBUG_ENTER("ha_parquet::info");
  stats = ha_statistics();
  stats.records = static_cast<ha_rows>(total_rows);
  if (stats.records < 2) stats.records = 2;  /* avoid single-row optimizer tricks */
  if (flag & HA_STATUS_AUTO) stats.auto_increment_value = 1;
  DBUG_RETURN(0);
}

int ha_parquet::external_lock(THD * /*thd*/, int /*lock_type*/) {
  DBUG_ENTER("ha_parquet::external_lock");
  DBUG_RETURN(0);
}

THR_LOCK_DATA **ha_parquet::store_lock(THD *thd, THR_LOCK_DATA **to,
                                       enum thr_lock_type lock_type) {
  DBUG_ENTER("ha_parquet::store_lock");
  /*
   * Downgrade write locks to concurrent-read locks: the engine is read-only,
   * so we never need an exclusive write lock.
   */
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK) {
    if (lock_type >= TL_WRITE_CONCURRENT_INSERT && lock_type <= TL_WRITE &&
        !thd_in_lock_tables(thd))
      lock_type = TL_WRITE_ALLOW_WRITE;

    if (lock_type == TL_READ_NO_INSERT && !thd_in_lock_tables(thd))
      lock_type = TL_READ;

    lock.type = lock_type;
  }
  *to++ = &lock;
  DBUG_RETURN(to);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Plugin registration
 * ═══════════════════════════════════════════════════════════════════════════ */

/* MariaDB create handler: no 'bool partitioned' parameter */
static handler *parquet_create_handler(handlerton *hton, TABLE_SHARE *table,
                                       MEM_ROOT *mem_root) {
  return new (mem_root) ha_parquet(hton, table);
}

static int parquet_init(void *p) {
  parquet_hton         = static_cast<handlerton *>(p);
  parquet_hton->create = parquet_create_handler;
  parquet_hton->flags  = HTON_CAN_RECREATE;
  return 0;
}

static int parquet_fini(void * /*p*/) {
  return 0;
}

struct st_mysql_storage_engine parquet_storage_engine = {
    MYSQL_HANDLERTON_INTERFACE_VERSION};

/*
 * MariaDB plugin descriptor — visible as ENGINE=READ_PARQUET
 *
 * After building and installing the plugin:
 *   INSTALL PLUGIN read_parquet SONAME 'ha_parquet.so';
 *
 * Example usage:
 *   CREATE TABLE sales (
 *     order_id   INT,
 *     product    VARCHAR(128),
 *     amount     DOUBLE,
 *     order_date DATE
 *   ) ENGINE=READ_PARQUET
 *     COMMENT='/data/warehouse/sales_2024.parquet';
 *
 *   SELECT product, SUM(amount) FROM sales GROUP BY product;
 */
maria_declare_plugin(read_parquet)
{
    MYSQL_STORAGE_ENGINE_PLUGIN,
    &parquet_storage_engine,
    "READ_PARQUET",
    "Study Project",
    "Read-only storage engine that maps a local .parquet file to a MariaDB table",
    PLUGIN_LICENSE_GPL,
    parquet_init,   /* Plugin Init   */
    parquet_fini,   /* Plugin Deinit */
    0x0100 /* 1.0 */,
    nullptr,        /* status variables  */
    nullptr,        /* system variables  */
    "1.0",          /* string version    */
    MariaDB_PLUGIN_MATURITY_STABLE
}
maria_declare_plugin_end;
