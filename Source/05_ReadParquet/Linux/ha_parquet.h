/* Copyright (c) 2024, Study Project.
   READ_PARQUET storage engine — reads a local .parquet file as a MariaDB table.

   Usage:
     1. Copy / symlink your .parquet file next to the table data directory,
        OR specify an absolute path in the COMMENT clause:
          CREATE TABLE t (id INT, name VARCHAR(64))
            ENGINE=READ_PARQUET
            COMMENT='/absolute/path/to/file.parquet';
     2. The engine opens the file on every table open and loads it with
        Apache Arrow into memory.  All writes / deletes are rejected.

   Build requirements:
     - Apache Arrow C++ library  (libarrow, libparquet)
     - Arrow Parquet reader       (parquet/arrow/reader.h)
     - MariaDB server dev headers (mariadb-server-dev)
*/

#pragma once

#include <cstdint>
#include <memory>

/* ── MariaDB server headers ───────────────────────────────────────────────── */
/* my_global.h 는 반드시 모든 MariaDB 헤더보다 먼저 포함해야 합니다.         */
#include "my_global.h"     // MUST be first — defines C_MODE_START/END, MYSQL_PLUGIN_IMPORT, ...
#include "my_base.h"       // ha_rows, HA_*
#include "mysql_time.h"    // MYSQL_TIME, MYSQL_TIMESTAMP_DATE
#include "thr_lock.h"      // THR_LOCK, THR_LOCK_DATA
#include "handler.h"       // handler, Handler_share, handlerton
                           // TABLE, TABLE_SHARE also pulled in via handler.h

/* Forward-declare Arrow types to avoid pulling all Arrow headers into .h */
namespace arrow { class Table; }

/* ── Per-table shared state ──────────────────────────────────────────────── */
class Parquet_share : public Handler_share {
 public:
  THR_LOCK lock;                  /* MariaDB table-level lock object          */
  char     table_name[FN_REFLEN]; /* Canonical table path (no extension)      */

  Parquet_share()  { thr_lock_init(&lock); }
  ~Parquet_share() override { thr_lock_delete(&lock); }
};

/* ── Storage engine handler ──────────────────────────────────────────────── */
class ha_parquet : public handler {
 public:
  ha_parquet(handlerton *hton, TABLE_SHARE *table_arg);
  ~ha_parquet() override = default;

  /* ── handler identity ───────────────────────────────────────────────── */
  const char *table_type() const override { return "READ_PARQUET"; }

  ulonglong table_flags() const override {
    return HA_NO_TRANSACTIONS        /* no BEGIN/COMMIT support             */
         | HA_BINLOG_STMT_CAPABLE;   /* can log in statement-based binlog   */
  }

  ulong index_flags(uint /*inx*/, uint /*part*/, bool /*all*/) const override {
    return 0; /* no index support */
  }

  uint max_supported_keys()        const override { return 0; }
  uint max_supported_key_length()  const override { return 0; }
  uint max_supported_record_length() const override { return HA_MAX_REC_LENGTH; }

  /* ── lifecycle ──────────────────────────────────────────────────────── */
  /* MariaDB does not use the dd::Table Data Dictionary parameter.        */
  int  open(const char *name, int mode, uint test_if_locked) override;
  int  close() override;
  int  create(const char *name, TABLE *form,
              HA_CREATE_INFO *create_info) override;
  int  delete_table(const char *name) override;

  /* ── full-table scan (the only read path) ───────────────────────────── */
  int  rnd_init(bool scan) override;
  int  rnd_next(uchar *buf) override;
  int  rnd_end() override;
  void position(const uchar *record) override;
  int  rnd_pos(uchar *buf, uchar *pos) override;

  /* ── optimizer / server callbacks ──────────────────────────────────── */
  int  info(uint flag) override;
  int  extra(enum ha_extra_function) override { return 0; }
  int  external_lock(THD *thd, int lock_type) override;
  THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
                             enum thr_lock_type lock_type) override;

  /* ── write operations — all rejected (read-only engine) ─────────────── */
  int write_row(const uchar *)              override { return HA_ERR_WRONG_COMMAND; }
  int update_row(const uchar *, const uchar *) override { return HA_ERR_WRONG_COMMAND; }
  int delete_row(const uchar *)             override { return HA_ERR_WRONG_COMMAND; }
  int delete_all_rows()                     override { return HA_ERR_WRONG_COMMAND; }

 private:
  /* ── internal state ─────────────────────────────────────────────────── */
  THR_LOCK_DATA lock;                         /* per-instance lock data       */
  Parquet_share *share{nullptr};              /* shared lock/name object      */

  std::shared_ptr<arrow::Table> arrow_table; /* in-memory Arrow table         */
  int64_t current_row{0};                    /* cursor for rnd_next()         */
  int64_t total_rows{0};                     /* total rows in the parquet file*/

  /* ── helpers ────────────────────────────────────────────────────────── */
  Parquet_share *get_share();

  /**
   * Read the absolute parquet path stored in the sidecar .prq file.
   * @param table_path  Base path (= `name` from open/create, no extension).
   * @param out_path    Output buffer of size FN_REFLEN.
   * @return  true on success.
   */
  static bool read_prq_meta(const char *table_path, char *out_path);

  /**
   * Fill one MariaDB row buffer from row @p row_idx of arrow_table.
   * Maps Arrow column types to MySQL Field::store() calls.
   */
  int fill_row(uchar *buf, int64_t row_idx);

  /**
   * Convert days-since-Unix-epoch to a MYSQL_TIME (DATE) struct.
   * Uses the Gregorian calendar algorithm by Howard Hinnant.
   */
  static void days_to_mysql_date(int32_t days, MYSQL_TIME *out);
};
