/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef CCNET_DB_H
#define CCNET_DB_H

#include "db.h"

#ifdef CCNET_SERVER

#define DB_SQLITE "sqlite"
#define DB_MYSQL "mysql"
#define DB_PGSQL "pgsql"

enum {
    CCNET_DB_TYPE_SQLITE,
    CCNET_DB_TYPE_MYSQL,
    CCNET_DB_TYPE_PGSQL,
};


typedef struct CcnetDB CcnetDB;
typedef struct CcnetDBRow CcnetDBRow;

typedef gboolean (*CcnetDBRowFunc) (CcnetDBRow *, void *);

CcnetDB *
ccnet_db_new_mysql (const char *host,
                    const char *user,
                    const char *passwd,
                    const char *db,
                    const char *unix_socket);

CcnetDB *
ccnet_db_new_pgsql (const char *host,
                    const char *user,
                    const char *passwd,
                    const char *db,
                    const char *unix_socket);

CcnetDB *
ccnet_db_new_sqlite (const char *db_path);

void
ccnet_db_free (CcnetDB *db);

int
ccnet_db_type (CcnetDB *db);

int
ccnet_db_query (CcnetDB *db, const char *sql);

gboolean
ccnet_db_check_for_existence (CcnetDB *db, const char *sql);

int
ccnet_db_foreach_selected_row (CcnetDB *db, const char *sql,
                               CcnetDBRowFunc callback, void *data);

const char *
ccnet_db_row_get_column_text (CcnetDBRow *row, guint32 idx);

int
ccnet_db_row_get_column_int (CcnetDBRow *row, guint32 idx);

gint64
ccnet_db_row_get_column_int64 (CcnetDBRow *row, guint32 idx);

int
ccnet_db_get_int (CcnetDB *db, const char *sql);

gint64 
ccnet_db_get_int64 (CcnetDB *db, const char *sql);

char *
ccnet_db_get_string (CcnetDB *db, const char *sql);

#else

#define CcnetDB sqlite3
#define CcnetDBRow sqlite3_stmt

#define ccnet_db_query sqlite_query_exec
#define ccnet_db_free sqlite_close_db
#define ccnet_db_begin_transaction sqlite_begin_transaction
#define ccnet_db_end_transaction sqlite_end_transaction
#define ccnet_db_check_for_existence sqlite_check_for_existence
#define ccnet_db_foreach_selected_row sqlite_foreach_selected_row
#define ccnet_db_row_get_column_text  sqlite3_column_text
#define ccnet_db_row_get_column_int   sqlite3_column_int
#define ccnet_db_row_get_column_int64 sqlite3_column_int64
#define ccnet_db_get_int sqlite_get_int
#define ccnet_db_get_int64 sqlite_get_int64
#define ccnet_db_get_string sqlite_get_string

#define ccnet_sql_printf sqlite3_mprintf
#define ccnet_sql_free sqlite3_free

#endif // CCNET_SERVER

#endif
