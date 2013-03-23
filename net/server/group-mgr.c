/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "common.h"

#include "server-session.h"

#include "ccnet-db.h"
#include "group-mgr.h"
#include "org-mgr.h"

#include "utils.h"
#include "log.h"

struct _CcnetGroupManagerPriv {
    CcnetDB	*db;
};

static int open_db (CcnetGroupManager *manager);
static int check_db_table (CcnetDB *db);

CcnetGroupManager* ccnet_group_manager_new (CcnetSession *session)
{
    CcnetGroupManager *manager = g_new0 (CcnetGroupManager, 1);

    manager->session = session;
    manager->priv = g_new0 (CcnetGroupManagerPriv, 1);

    return manager;
}

int
ccnet_group_manager_prepare (CcnetGroupManager *manager)
{
    return open_db(manager);
}

void ccnet_group_manager_start (CcnetGroupManager *manager)
{
}

static CcnetDB *
open_sqlite_db (CcnetGroupManager *manager)
{
    CcnetDB *db = NULL;
    char *db_dir;
    char *db_path;

    db_dir = g_build_filename (manager->session->config_dir, "GroupMgr", NULL);
    if (checkdir_with_mkdir(db_dir) < 0) {
        ccnet_error ("Cannot open db dir %s: %s\n", db_dir,
                     strerror(errno));
        g_free (db_dir);
        return NULL;
    }
    g_free (db_dir);

    db_path = g_build_filename (manager->session->config_dir, "GroupMgr",
                                "groupmgr.db", NULL);
    db = ccnet_db_new_sqlite (db_path);

    g_free (db_path);

    return db;
}

static int
open_db (CcnetGroupManager *manager)
{
    CcnetDB *db;

    switch (ccnet_db_type(manager->session->db)) {
    case CCNET_DB_TYPE_SQLITE:
        db = open_sqlite_db (manager);
        if (!db)
            return -1;
        break;
    case CCNET_DB_TYPE_PGSQL:
    case CCNET_DB_TYPE_MYSQL:
        db = manager->session->db;
        break;
    }
    
    manager->priv->db = db;
    return check_db_table (db);
}

/* -------- Group Database Management ---------------- */

static int check_db_table (CcnetDB *db)
{
    char *sql;

    int db_type = ccnet_db_type (db);
    if (db_type == CCNET_DB_TYPE_MYSQL) {
        sql = "CREATE TABLE IF NOT EXISTS `Group` (`group_id` INTEGER"
            " PRIMARY KEY AUTO_INCREMENT, `group_name` VARCHAR(255),"
            " `creator_name` VARCHAR(255), `timestamp` BIGINT)"
            "ENGINE=INNODB";
        if (ccnet_db_query (db, sql) < 0)
            return -1;

        sql = "CREATE TABLE IF NOT EXISTS `GroupUser` (`group_id` INTEGER,"
            " `user_name` VARCHAR(255), `is_staff` tinyint, UNIQUE INDEX"
            " (`group_id`, `user_name`), INDEX (`user_name`))"
            "ENGINE=INNODB";
        if (ccnet_db_query (db, sql) < 0)
            return -1;
    } else if (db_type == CCNET_DB_TYPE_SQLITE) {
        sql = "CREATE TABLE IF NOT EXISTS `Group` (`group_id` INTEGER"
            " PRIMARY KEY AUTOINCREMENT, `group_name` VARCHAR(255),"
            " `creator_name` VARCHAR(255), `timestamp` BIGINT)";
        if (ccnet_db_query (db, sql) < 0)
            return -1;

        sql = "CREATE TABLE IF NOT EXISTS `GroupUser` (`group_id` INTEGER, "
            "`user_name` VARCHAR(255), `is_staff` tinyint)";
        if (ccnet_db_query (db, sql) < 0)
            return -1;

        sql = "CREATE UNIQUE INDEX IF NOT EXISTS groupid_username_indx on "
            "`GroupUser` (`group_id`, `user_name`)";
        if (ccnet_db_query (db, sql) < 0)
            return -1;

        sql = "CREATE INDEX IF NOT EXISTS username_indx on "
            "`GroupUser` (`user_name`)";
        if (ccnet_db_query (db, sql) < 0)
            return -1;
    } else if (db_type == CCNET_DB_TYPE_PGSQL) {
        sql = "CREATE TABLE IF NOT EXISTS \"Group\" (group_id SERIAL"
            " PRIMARY KEY, group_name VARCHAR(255),"
            " creator_name VARCHAR(255), timestamp BIGINT)";
        if (ccnet_db_query (db, sql) < 0)
            return -1;

        sql = "CREATE TABLE IF NOT EXISTS GroupUser (group_id INTEGER,"
            " user_name VARCHAR(255), is_staff tinyint, UNIQUE "
            " (group_id, user_name))"
            "ENGINE=INNODB";
        if (ccnet_db_query (db, sql) < 0)
            return -1;
    }

    return 0;
}

static int
create_group_common (CcnetGroupManager *mgr,
                     const char *group_name,
                     const char *user_name,
                     GError **error)
{
    CcnetDB *db = mgr->priv->db;
    gint64 now = get_current_time();
    char sql[512];
    
    snprintf (sql, sizeof(sql), "INSERT INTO `Group`(`group_name`, "
              "`creator_name`, `timestamp`) VALUES('%s', '%s', "
              "%"G_GINT64_FORMAT")", group_name, user_name, now);

    if (ccnet_db_query (db, sql) < 0) {
        g_set_error (error, CCNET_DOMAIN, 0, "Failed to create group");
        return -1;
    }

    snprintf (sql, sizeof(sql),"SELECT `group_id` FROM `Group` WHERE "
              "`group_name` = '%s' AND `creator_name` = '%s' AND"
              " `timestamp` = %"G_GINT64_FORMAT"", group_name, user_name, now);
    int group_id = ccnet_db_get_int (db, sql);
    if (group_id < 0) {
        g_set_error (error, CCNET_DOMAIN, 0, "Failed to create group");
        return -1;
    }
    
    snprintf (sql, sizeof(sql), "INSERT INTO `GroupUser` VALUES (%d, '%s', %d)",
              group_id, user_name, 1);
    if (ccnet_db_query (db, sql) < 0) {
        snprintf (sql, sizeof(sql), "DELETE FROM `Group` WHERE `group_id`=%d",
                  group_id);
        ccnet_db_query (db, sql);
        g_set_error (error, CCNET_DOMAIN, 0, "Failed to create group");
        return -1;
    }
    
    return group_id;
}

/* static gboolean */
/* duplicate_group_name (CcnetGroupManager *mgr, */
/*                       const char *group_name, */
/*                       const char *user_name) */
/* { */
/*     GList *groups = NULL, *ptr; */
/*     CcnetOrgManager *org_mgr = NULL; */
/*     gboolean ret = FALSE; */

/*     groups = ccnet_group_manager_get_all_groups (mgr, -1, -1, NULL); */
/*     if (!groups) { */
/*         return FALSE; */
/*     } */
    
/*     for (ptr = groups; ptr; ptr = ptr->next) { */
/*         CcnetGroup *group = (CcnetGroup *)ptr->data; */
/*         org_mgr = ((CcnetServerSession *)(mgr->session))->org_mgr; */
/*         if (ccnet_org_manager_is_org_group(org_mgr, ccnet_group_get_id (group), */
/*                                            NULL)) { */
/*             /\* Skip org groups. *\/ */
/*             continue; */
/*         } */

/*         if (g_strcmp0 (group_name, ccnet_group_get_group_name (group)) == 0) { */
/*             ret = TRUE; */
/*             goto out; */
/*         } */
/*     } */
    
/* out: */
/*     for (ptr = groups; ptr; ptr = ptr->next) */
/*         g_object_unref ((GObject *)ptr->data); */
/*     g_list_free (groups); */
/*     return ret; */
/* } */

int ccnet_group_manager_create_group (CcnetGroupManager *mgr,
                                      const char *group_name,
                                      const char *user_name,
                                      GError **error)
{

    /* if (duplicate_group_name (mgr, group_name, user_name)) { */
    /*     g_set_error (error, CCNET_DOMAIN, 0, "The group has already created"); */
    /*     return -1; */
    /* } */

    return create_group_common (mgr, group_name, user_name, error);
}

static gboolean
duplicate_org_group_name (CcnetGroupManager *mgr,
                          int org_id,
                          const char *group_name)
{
    GList *org_groups = NULL, *ptr;
    CcnetOrgManager *org_mgr = ((CcnetServerSession *)(mgr->session))->org_mgr;
    
    org_groups = ccnet_org_manager_get_org_groups (org_mgr, org_id, -1, -1);
    if (!org_groups)
        return FALSE;

    for (ptr = org_groups; ptr; ptr = ptr->next) {
        int group_id = (int)(long)ptr->data;
        CcnetGroup *group = ccnet_group_manager_get_group (mgr, group_id,
                                                           NULL);
        if (!group)
            continue;

        if (g_strcmp0 (group_name, ccnet_group_get_group_name(group)) == 0) {
            g_list_free (org_groups);
            g_object_unref (group);
            return TRUE;
        } else {
            g_object_unref (group);
        }
    }

    g_list_free (org_groups);
    return FALSE;
}

int ccnet_group_manager_create_org_group (CcnetGroupManager *mgr,
                                          int org_id,
                                          const char *group_name,
                                          const char *user_name,
                                          GError **error)
{
    CcnetOrgManager *org_mgr = ((CcnetServerSession *)(mgr->session))->org_mgr;
    
    if (duplicate_org_group_name (mgr, org_id, group_name)) {
        g_set_error (error, CCNET_DOMAIN, 0,
                     "The group has already created in this org.");
        return -1;
    }

    int group_id = create_group_common (mgr, group_name, user_name, error);
    if (group_id < 0) {
        g_set_error (error, CCNET_DOMAIN, 0, "Failed to create org group.");
        return -1;
    }

    if (ccnet_org_manager_add_org_group (org_mgr, org_id, group_id,
                                         error) < 0) {
        g_set_error (error, CCNET_DOMAIN, 0, "Failed to create org group.");
        return -1;
    }

    return group_id;
}

static gboolean
check_group_staff (CcnetDB *db, int group_id, const char *user_name)
{
    char sql[512];

    snprintf (sql, sizeof(sql), "SELECT `group_id` FROM `GroupUser` WHERE "
              "`group_id` = %d AND `user_name` = '%s' AND `is_staff` = 1",
              group_id, user_name);
    
    return ccnet_db_check_for_existence (db, sql);
}

int ccnet_group_manager_remove_group (CcnetGroupManager *mgr,
                                      int group_id,
                                      const char *user_name,
                                      GError **error)
{
    CcnetDB *db = mgr->priv->db;
    char sql[512];

    /* No permission check here, since both group staff and seahub staff
     * can remove group.
     */
    
    snprintf (sql, sizeof(sql), "DELETE FROM `Group` WHERE `group_id`=%d",
              group_id);
    ccnet_db_query (db, sql);

    snprintf (sql, sizeof(sql), "DELETE FROM `GroupUser` WHERE `group_id`=%d",
              group_id);
    ccnet_db_query (db, sql);
    
    return 0;
}

static gboolean
check_group_exists (CcnetDB *db, int group_id)
{
    char sql[512];
    
    snprintf (sql, sizeof(sql), "SELECT `group_id` FROM `Group` WHERE "
              "`group_id`=%d", group_id);
    
    return ccnet_db_check_for_existence (db, sql);
}

int ccnet_group_manager_add_member (CcnetGroupManager *mgr,
                                    int group_id,
                                    const char *user_name,
                                    const char *member_name,
                                    GError **error)
{
    CcnetDB *db = mgr->priv->db;
    char sql[512];

    /* check whether user is the staff of the group */
    if (!check_group_staff (db, group_id, user_name)) {
        g_set_error (error, CCNET_DOMAIN, 0,
                     "Permission error: only group staff can add member");
        return -1; 
    }    

    /* check whether group exists */
    if (!check_group_exists (db, group_id)) {
        g_set_error (error, CCNET_DOMAIN, 0, "Group not exists");
        return -1;
    }

    /* check whether group is full */
    /* snprintf (sql, sizeof(sql), "SELECT count(group_id) FROM `GroupUser` " */
    /*           "WHERE `group_id` = %d", group_id); */
    /* int count = ccnet_db_get_int (db, sql); */
    /* if (count >= MAX_GROUP_MEMBERS) { */
    /*     g_set_error (error, CCNET_DOMAIN, 0, "Group is full"); */
    /*     return -1; */
    /* } */
    
    snprintf (sql, sizeof(sql), "INSERT INTO `GroupUser` VALUES (%d, '%s', %d)",
              group_id, member_name, 0);
    if (ccnet_db_query (db, sql) < 0) {
        g_set_error (error, CCNET_DOMAIN, 0, "Failed to add member to group");
        return -1;
    }

    return 0;
}

int ccnet_group_manager_remove_member (CcnetGroupManager *mgr,
                                       int group_id,
                                       const char *user_name,
                                       const char *member_name,
                                       GError **error)
{
    CcnetDB *db = mgr->priv->db;
    char sql[512];

    /* check whether user is the staff of the group */
    if (!check_group_staff (db, group_id, user_name)) {
        g_set_error (error, CCNET_DOMAIN, 0,
                     "Only group staff can remove member");
        return -1; 
    }    

    /* check whether group exists */
    if (!check_group_exists (db, group_id)) {
        g_set_error (error, CCNET_DOMAIN, 0, "Group not exists");
        return -1;
    }

    /* can not remove myself */
    if (g_strcmp0 (user_name, member_name) == 0) {
        g_set_error (error, CCNET_DOMAIN, 0, "Can not remove myself");
        return -1;
    }

    snprintf (sql, sizeof(sql), "DELETE FROM `GroupUser` WHERE `group_id`=%d AND "
              "`user_name`='%s'", group_id, member_name);
    ccnet_db_query (db, sql);

    return 0;
}

int ccnet_group_manager_set_admin (CcnetGroupManager *mgr,
                                   int group_id,
                                   const char *member_name,
                                   GError **error)
{
    CcnetDB *db = mgr->priv->db;
    char sql[512];

    snprintf (sql, sizeof(sql), "UPDATE `GroupUser` SET `is_staff` = 1 "
              "WHERE `group_id` = %d and `user_name` = '%s'",
              group_id, member_name);
    ccnet_db_query (db, sql);

    return 0;
}

int ccnet_group_manager_unset_admin (CcnetGroupManager *mgr,
                                     int group_id,
                                     const char *member_name,
                                     GError **error)
{
    CcnetDB *db = mgr->priv->db;
    char sql[512];

    snprintf (sql, sizeof(sql), "UPDATE `GroupUser` SET `is_staff` = 0 "
              "WHERE `group_id` = %d and `user_name` = '%s'",
              group_id, member_name);
    ccnet_db_query (db, sql);

    return 0;
}

int ccnet_group_manager_quit_group (CcnetGroupManager *mgr,
                                    int group_id,
                                    const char *user_name,
                                    GError **error)
{
    CcnetDB *db = mgr->priv->db;
    char sql[512];
    
    /* check where user is the staff of the group */
    if (check_group_staff (db, group_id, user_name)) {
        g_set_error (error, CCNET_DOMAIN, 0,
                     "Group staff can not quit group");
        return -1; 
    }    

    /* check whether group exists */
    if (!check_group_exists (db, group_id)) {
        g_set_error (error, CCNET_DOMAIN, 0, "Group not exists");
        return -1;
    }
    
    snprintf (sql, sizeof(sql), "DELETE FROM `GroupUser` WHERE `group_id`=%d "
              "AND `user_name`='%s'", group_id, user_name);
    ccnet_db_query (db, sql);

    return 0;
}

static gboolean
get_group_ids_cb (CcnetDBRow *row, void *data)
{
    GList **plist = data;

    int group_id = ccnet_db_row_get_column_int (row, 0);

    *plist = g_list_prepend (*plist, (gpointer)(long)group_id);

    return TRUE;
}

GList *
ccnet_group_manager_get_groupids_by_user (CcnetGroupManager *mgr,
                                          const char *user_name,
                                          GError **error)
{
    CcnetDB *db = mgr->priv->db;
    char sql[512];
    GList *group_ids = NULL;

    snprintf (sql, sizeof(sql), "SELECT `group_id` FROM `GroupUser` "
              "WHERE `user_name`='%s'", user_name);

    if (ccnet_db_foreach_selected_row (db, sql, get_group_ids_cb,
                                       &group_ids) < 0) {
        g_list_free (group_ids);
        return NULL;
    }

    return g_list_reverse (group_ids);
}

static gboolean
get_ccnetgroup_cb (CcnetDBRow *row, void *data)
{
    CcnetGroup **p_group = data;
    int group_id;
    const char *group_name;
    const char *creator;
    gint64 ts;
    
    group_id = ccnet_db_row_get_column_int (row, 0);
    group_name = (const char *)ccnet_db_row_get_column_text (row, 1);
    creator = (const char *)ccnet_db_row_get_column_text (row, 2);
    ts = ccnet_db_row_get_column_int64 (row, 3);

    *p_group = g_object_new (CCNET_TYPE_GROUP,
                             "id", group_id,
                             "group_name", group_name,
                             "creator_name", creator,
                             "timestamp", ts,
                             NULL);

    return FALSE;
}

CcnetGroup *
ccnet_group_manager_get_group (CcnetGroupManager *mgr, int group_id,
                               GError **error)
{
    CcnetDB *db = mgr->priv->db;
    char sql[512];
    CcnetGroup *ccnetgroup = NULL;

    snprintf (sql, sizeof(sql),
              "SELECT * FROM `Group` WHERE `group_id` = %d", group_id);
    if (ccnet_db_foreach_selected_row (db, sql, get_ccnetgroup_cb,
                                       &ccnetgroup) < 0)
        return NULL;

    return ccnetgroup;
}

static gboolean
get_ccnet_groupuser_cb (CcnetDBRow *row, void *data)
{
    GList **plist = data;
    CcnetGroupUser *group_user;
    
    int group_id = ccnet_db_row_get_column_int (row, 0);
    const char *user = (const char *)ccnet_db_row_get_column_text (row, 1);
    int is_staff = ccnet_db_row_get_column_int (row, 2);

    group_user = g_object_new (CCNET_TYPE_GROUP_USER,
                               "group_id", group_id,
                               "user_name", user,
                               "is_staff", is_staff,
                               NULL);
    if (group_user != NULL) {
        *plist = g_list_prepend (*plist, group_user);
    }
    
    return TRUE;
}

GList *
ccnet_group_manager_get_group_members (CcnetGroupManager *mgr, int group_id,
                                       GError **error)
{
    CcnetDB *db = mgr->priv->db;
    char sql[512];
    GList *group_users = NULL;
    
    snprintf (sql, sizeof(sql),
              "SELECT * FROM `GroupUser` WHERE `group_id` = %d", group_id);
    if (ccnet_db_foreach_selected_row (db, sql, get_ccnet_groupuser_cb,
                                       &group_users) < 0)
        return NULL;

    return g_list_reverse (group_users);
}

int
ccnet_group_manager_check_group_staff (CcnetGroupManager *mgr,
                                       int group_id,
                                       const char *user_name)
{
    return check_group_staff (mgr->priv->db, group_id, user_name);
}

int
ccnet_group_manager_remove_group_user (CcnetGroupManager *mgr,
                                       const char *user)
{
    CcnetDB *db = mgr->priv->db;
    char sql[256];

    snprintf (sql, sizeof(sql), "DELETE FROM `GroupUser` "
              "WHERE `user_name` = '%s'", user);
    return ccnet_db_query (db, sql);
}

int
ccnet_group_manager_is_group_user (CcnetGroupManager *mgr,
                                   int group_id,
                                   const char *user)
{
    CcnetDB *db = mgr->priv->db;
    char sql[512];

    snprintf (sql, sizeof(sql), "SELECT group_id FROM `GroupUser` "
              "WHERE `group_id`=%d AND `user_name`='%s'", group_id, user);
    return ccnet_db_check_for_existence (db, sql);
}

static gboolean
get_all_ccnetgroups_cb (CcnetDBRow *row, void *data)
{
    GList **plist = data;
    int group_id;
    const char *group_name;
    const char *creator;
    gint64 ts;

    group_id = ccnet_db_row_get_column_int (row, 0);
    group_name = (const char *)ccnet_db_row_get_column_text (row, 1);
    creator = (const char *)ccnet_db_row_get_column_text (row, 2);
    ts = ccnet_db_row_get_column_int64 (row, 3);

    CcnetGroup *group = g_object_new (CCNET_TYPE_GROUP,
                                      "id", group_id,
                                      "group_name", group_name,
                                      "creator_name", creator,
                                      "timestamp", ts,
                                      NULL);
    
    *plist = g_list_prepend (*plist, group);
    
    return TRUE;
}

GList*
ccnet_group_manager_get_all_groups (CcnetGroupManager *mgr,
                                    int start, int limit, GError **error)
{
    GList *ret = NULL;
    char sql[256];

    if (start == -1 && limit == -1) {
        snprintf (sql, sizeof(sql), "SELECT group_id, group_name, "
                  "creator_name, timestamp FROM `Group`");
    } else {
        snprintf (sql, sizeof(sql), "SELECT group_id, group_name, "
                  "creator_name, timestamp FROM `Group` LIMIT %d, %d",
                  start, limit);
    }

    if (ccnet_db_foreach_selected_row (mgr->priv->db, sql,
                                       get_all_ccnetgroups_cb, &ret) < 0) 
        return NULL;

    return g_list_reverse (ret);
}
