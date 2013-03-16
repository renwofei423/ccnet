/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include <zdb.h>
#include "ccnet-db.h"

#ifdef WIN32
#include <windows.h>
#define sleep(n) Sleep(1000 * (n))
#endif

#define MAX_GET_CONNECTION_RETRIES 3

struct CcnetDB {
    int type;
    ConnectionPool_T pool;
};

struct CcnetDBRow {
    ResultSet_T res;
};

CcnetDB *
ccnet_db_new_mysql (const char *host, 
                    const char *user, 
                    const char *passwd,
                    const char *db_name,
                    const char *unix_socket)
{
    CcnetDB *db;
    GString *url;
    URL_T zdb_url;

    db = g_new0 (CcnetDB, 1);
    if (!db) {
        g_warning ("Failed to alloc db structure.\n");
        return NULL;
    }

    url = g_string_new ("");
    g_string_append_printf (url, "mysql://%s:%s@%s/", user, passwd, host);
    if (db_name)
        g_string_append (url, db_name);
    if (unix_socket)
        g_string_append_printf (url, "?unix-socket=%s", unix_socket);

    zdb_url = URL_new (url->str);
    db->pool = ConnectionPool_new (zdb_url);
    if (!db->pool) {
        g_warning ("Failed to create db connection pool.\n");
        g_string_free (url, TRUE);
        g_free (db);
        return NULL;
    }

    ConnectionPool_start (db->pool);
    db->type = CCNET_DB_TYPE_MYSQL;

    return db;
}

CcnetDB *
ccnet_db_new_sqlite (const char *db_path)
{
    CcnetDB *db;
    GString *url;
    URL_T zdb_url;

    db = g_new0 (CcnetDB, 1);
    if (!db) {
        g_warning ("Failed to alloc db structure.\n");
        return NULL;
    }

    url = g_string_new ("");
    g_string_append_printf (url, "sqlite://%s", db_path);
    zdb_url = URL_new (url->str);
    db->pool = ConnectionPool_new (zdb_url);
    if (!db->pool) {
        g_warning ("Failed to create db connection pool.\n");
        g_string_free (url, TRUE);
        g_free (db);
        return NULL;
    }

    ConnectionPool_start (db->pool);
    db->type = CCNET_DB_TYPE_SQLITE;

    return db;
}

void
ccnet_db_free (CcnetDB *db)
{
    ConnectionPool_stop (db->pool);
    ConnectionPool_free (&db->pool);
    g_free (db);
}

int
ccnet_db_type (CcnetDB *db)
{
    return db->type;
}

static Connection_T
get_db_connection (CcnetDB *db)
{
    Connection_T conn;
    int retries = 0;

    conn = ConnectionPool_getConnection (db->pool);
    /* If max_connections of the pool has been reached, retry 3 times
     * and then return NULL.
     */
    while (!conn) {
        if (retries++ == MAX_GET_CONNECTION_RETRIES) {
            g_warning ("Too many concurrent connections. "
                       "Failed to create new connection.\n");
            goto out;
        }
        sleep (1);
        conn = ConnectionPool_getConnection (db->pool);
    }

    if (!conn)
        g_warning ("Failed to create new connection.\n");

out:
    return conn;
}

int
ccnet_db_query (CcnetDB *db, const char *sql)
{
    Connection_T conn = get_db_connection (db);
    if (!conn)
        return -1;

    /* Handle zdb "exception"s. */
    TRY
        Connection_execute (conn, "%s", sql);
        Connection_close (conn);
        RETURN (0);
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return -1;
    END_TRY;

    /* Should not be reached. */
    return 0;
}

gboolean
ccnet_db_check_for_existence (CcnetDB *db, const char *sql)
{
    Connection_T conn;
    ResultSet_T result;
    gboolean ret = TRUE;

    conn = get_db_connection (db);
    if (!conn) {
        return FALSE;
    }

    TRY
        result = Connection_executeQuery (conn, "%s", sql);
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return FALSE;
    END_TRY;

    TRY
        if (!ResultSet_next (result))
            ret = FALSE;
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return FALSE;
    END_TRY;

    Connection_close (conn);

    return ret;
}

int
ccnet_db_foreach_selected_row (CcnetDB *db, const char *sql, 
                               CcnetDBRowFunc callback, void *data)
{
    Connection_T conn;
    ResultSet_T result;
    CcnetDBRow ccnet_row;
    int n_rows = 0;

    conn = get_db_connection (db);
    if (!conn)
        return -1;

    TRY
        result = Connection_executeQuery (conn, "%s", sql);
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return -1;
    END_TRY;

    ccnet_row.res = result;
    TRY
        while (ResultSet_next (result)) {
            n_rows++;
            if (!callback (&ccnet_row, data))
                break;
        }
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return -1;
    END_TRY;

    Connection_close (conn);
    return n_rows;
}

const char *
ccnet_db_row_get_column_text (CcnetDBRow *row, guint32 idx)
{
    g_assert (idx < ResultSet_getColumnCount(row->res));

    return ResultSet_getString (row->res, idx+1);
}

int
ccnet_db_row_get_column_int (CcnetDBRow *row, guint32 idx)
{
    g_assert (idx < ResultSet_getColumnCount(row->res));

    return ResultSet_getInt (row->res, idx+1);
}

gint64
ccnet_db_row_get_column_int64 (CcnetDBRow *row, guint32 idx)
{
    g_assert (idx < ResultSet_getColumnCount(row->res));

    return ResultSet_getLLong (row->res, idx+1);
}

int
ccnet_db_get_int (CcnetDB *db, const char *sql)
{
    int ret = -1;
    Connection_T conn;
    ResultSet_T result;
    CcnetDBRow ccnet_row;

    conn = get_db_connection (db);
    if (!conn)
        return -1;

    TRY
        result = Connection_executeQuery (conn, "%s", sql);
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return -1;
    END_TRY;

    ccnet_row.res = result;

    TRY
        if (ResultSet_next (result))
            ret = ccnet_db_row_get_column_int (&ccnet_row, 0);
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return -1;
    END_TRY;

    Connection_close (conn);
    return ret;
}

gint64
ccnet_db_get_int64 (CcnetDB *db, const char *sql)
{
    gint64 ret = -1;
    Connection_T conn;
    ResultSet_T result;
    CcnetDBRow ccnet_row;

    conn = get_db_connection (db);
    if (!conn)
        return -1;

    TRY
        result = Connection_executeQuery (conn, "%s", sql);
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return -1;
    END_TRY;

    ccnet_row.res = result;

    TRY
        if (ResultSet_next (result))
            ret = ccnet_db_row_get_column_int64 (&ccnet_row, 0);
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return -1;
    END_TRY;

    Connection_close (conn);
    return ret;
}

char *
ccnet_db_get_string (CcnetDB *db, const char *sql)
{
    char *ret = NULL;
    const char *s;
    Connection_T conn;
    ResultSet_T result;
    CcnetDBRow ccnet_row;

    conn = get_db_connection (db);
    if (!conn)
        return NULL;

    TRY
        result = Connection_executeQuery (conn, "%s", sql);
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return NULL;
    END_TRY;

    ccnet_row.res = result;
    
    TRY
        if (ResultSet_next (result)) {
            s = ccnet_db_row_get_column_text (&ccnet_row, 0);
            ret = g_strdup(s);
        }
    CATCH (SQLException)
        g_warning ("Error exec query %s: %s.\n", sql, Exception_frame.message);
        Connection_close (conn);
        return NULL;
    END_TRY;

    Connection_close (conn);
    return ret;
}
