#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "buffer/buffer_pool.h"
#include "catalog.h"
#include "common.h"
#include "db.h"
#include "index/btree.h"
#include "schema.h"
#include "value.h"

static DBStatus catalog_build_path(
    const DB *db,
    char *out_path,
    uint32_t out_size
) {
    if (db == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    /*
     * The catalog always lives directly inside the database folder.
     * Example: mydb/catalog.db
     */
    int written = snprintf(
        out_path,
        out_size,
        "%s/catalog.db",
        db->path
    );

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

static DBStatus catalog_build_table_file_path(
    const DB *db,
    const char *table_name,
    char *out_path,
    uint32_t out_size
) {
    if (db == NULL || table_name == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    /*
     * Each table has one data file.
     * Example: mydb/tables/users.tbl
     */
    int written = snprintf(
        out_path,
        out_size,
        "%s/tables/%s.tbl",
        db->path,
        table_name
    );

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

static DBStatus catalog_build_primary_key_index_path(
    const DB *db,
    const char *table_name,
    char *out_path,
    uint32_t out_size
) {
    if (db == NULL || table_name == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    /*
     * The SQL layer only supports one primary key per table, so the filename
     * can be derived entirely from the table name.
     */
    int written = snprintf(
        out_path,
        out_size,
        "%s/indexes/%s_pk.btree",
        db->path,
        table_name
    );

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

static DBStatus catalog_build_secondary_index_path(
    const DB *db,
    const char *index_name,
    char *out_path,
    uint32_t out_size
) {
    if (db == NULL || index_name == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    int written = snprintf(
        out_path,
        out_size,
        "%s/indexes/%s.sidx",
        db->path,
        index_name
    );

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

static const char *catalog_type_to_string(ValueType type) {
    /*
     * Convert internal value types into catalog text.
     */
    if (type == VALUE_INT) {
        return "INT";
    }

    if (type == VALUE_TEXT) {
        return "TEXT";
    }

    return "UNKNOWN";
}

static DBStatus catalog_string_to_type(const char *text, ValueType *out_type) {
    if (text == NULL || out_type == NULL) {
        return DB_ERROR;
    }

    /*
     * Convert catalog text back into internal value types.
     */
    if (strcmp(text, "INT") == 0) {
        *out_type = VALUE_INT;
        return DB_OK;
    }

    if (strcmp(text, "TEXT") == 0) {
        *out_type = VALUE_TEXT;
        return DB_OK;
    }

    return DB_TYPE_ERROR;
}

static DBStatus catalog_parse_constraint_flags(
    const char *first_flag,
    const char *second_flag,
    bool *out_not_null,
    bool *out_primary_key
) {
    const char *flags[2];

    if (out_not_null == NULL || out_primary_key == NULL) {
        return DB_ERROR;
    }

    *out_not_null = false;
    *out_primary_key = false;
    flags[0] = first_flag;
    flags[1] = second_flag;

    /*
     * New catalog files may append PRIMARY_KEY and/or NOT_NULL after the type.
     * Old catalog files stop after the type, leaving both flag strings empty.
     */
    for (uint16_t i = 0; i < 2; i++) {
        if (flags[i] == NULL || flags[i][0] == '\0') {
            continue;
        }

        if (strcmp(flags[i], "NOT_NULL") == 0) {
            *out_not_null = true;
        } else if (strcmp(flags[i], "PRIMARY_KEY") == 0) {
            *out_primary_key = true;
        } else {
            return DB_PARSE_ERROR;
        }
    }

    if (*out_primary_key) {
        *out_not_null = true;
    }

    return DB_OK;
}

static DBStatus catalog_create_primary_key_index_file(
    const DB *db,
    const Schema *schema
) {
    uint16_t primary_key_index = 0;
    DBStatus status = schema_get_primary_key_index(schema, &primary_key_index);

    if (status == DB_NOT_FOUND) {
        return DB_OK;
    }

    if (status != DB_OK) {
        return status;
    }

    char index_path[MAX_DB_PATH];

    status = catalog_build_primary_key_index_path(
        db,
        schema->table_name,
        index_path,
        sizeof(index_path)
    );

    if (status != DB_OK) {
        return status;
    }

    /*
     * CREATE TABLE is the first moment the primary-key access path should
     * exist. It starts empty; INSERT will add key -> RID entries later.
     */
    status = buffer_pool_discard_file(index_path);

    if (status != DB_OK) {
        return status;
    }

    FILE *file = fopen(index_path, "wb");

    if (file == NULL) {
        return DB_IO_ERROR;
    }

    if (fclose(file) != 0) {
        return DB_IO_ERROR;
    }

    BTree tree;

    status = btree_open(&tree, index_path);

    if (status != DB_OK) {
        return status;
    }

    return btree_close(&tree);
}

static void catalog_clear(Catalog *catalog) {
    if (catalog == NULL) {
        return;
    }

    /*
     * Clear all loaded table metadata.
     */
    memset(catalog, 0, sizeof(Catalog));
}

DBStatus catalog_load(DB *db) {
    if (db == NULL) {
        return DB_ERROR;
    }

    catalog_clear(&db->catalog);

    char catalog_path[MAX_DB_PATH];

    DBStatus status = catalog_build_path(
        db,
        catalog_path,
        sizeof(catalog_path)
    );

    if (status != DB_OK) {
        return status;
    }

    /*
     * If the catalog file does not exist yet, start with an empty catalog.
     */
    FILE *file = fopen(catalog_path, "r");

    if (file == NULL) {
        return DB_OK;
    }

    char line[256];

    while (fgets(line, sizeof(line), file) != NULL) {
        char table_name[MAX_TABLE_NAME];
        char index_name[MAX_INDEX_NAME];
        char index_table_name[MAX_TABLE_NAME];
        char third_field[MAX_COLUMN_NAME];

        /*
         * Secondary indexes are saved as standalone lines after table blocks.
         * New format: INDEX idx_users_age users 1 age
         * Old format: INDEX idx_users_age users age UNIQUE
         */
        if (
            sscanf(
                line,
                "INDEX %63s %63s %63s",
                index_name,
                index_table_name,
                third_field
            ) == 3
        ) {
            if (db->catalog.index_count >= MAX_CATALOG_INDEXES) {
                fclose(file);
                return DB_FULL;
            }

            CatalogIndex *index = &db->catalog.indexes[db->catalog.index_count];

            strncpy(index->index_name, index_name, sizeof(index->index_name) - 1);
            index->index_name[sizeof(index->index_name) - 1] = '\0';
            strncpy(index->table_name, index_table_name, sizeof(index->table_name) - 1);
            index->table_name[sizeof(index->table_name) - 1] = '\0';

            uint16_t parsed_column_count = 0;

            if (sscanf(third_field, "%hu", &parsed_column_count) == 1) {
                char *cursor = line;

                /*
                 * New index lines store the column count before the column
                 * names. Walk past: INDEX, index name, table name, count.
                 */
                for (uint16_t skip = 0; skip < 4; skip++) {
                    cursor = strchr(cursor, ' ');

                    if (cursor == NULL) {
                        fclose(file);
                        return DB_PARSE_ERROR;
                    }

                    while (*cursor == ' ') {
                        cursor++;
                    }
                }

                if (parsed_column_count == 0 || parsed_column_count > MAX_COLUMNS) {
                    fclose(file);
                    return DB_PARSE_ERROR;
                }

                index->column_count = parsed_column_count;

                for (uint16_t i = 0; i < parsed_column_count; i++) {
                    if (
                        sscanf(cursor, "%63s", index->column_names[i]) != 1 ||
                        strlen(index->column_names[i]) >= MAX_COLUMN_NAME
                    ) {
                        fclose(file);
                        return DB_PARSE_ERROR;
                    }

                    cursor = strchr(cursor, ' ');

                    if (cursor == NULL && i + 1 < parsed_column_count) {
                        fclose(file);
                        return DB_PARSE_ERROR;
                    }

                    if (cursor != NULL) {
                        while (*cursor == ' ') {
                            cursor++;
                        }
                    }
                }
            } else {
                /*
                 * Older catalogs saved only one column name. Loading them as a
                 * one-column non-unique index keeps existing databases usable.
                 */
                index->column_count = 1;
                strncpy(
                    index->column_names[0],
                    third_field,
                    sizeof(index->column_names[0]) - 1
                );
                index->column_names[0][sizeof(index->column_names[0]) - 1] = '\0';
            }

            index->unique = false;
            db->catalog.index_count++;

            continue;
        }

        /*
         * A table definition starts with:
         * TABLE users
         */
        if (sscanf(line, "TABLE %63s", table_name) != 1) {
            continue;
        }

        if (db->catalog.table_count >= MAX_CATALOG_TABLES) {
            fclose(file);
            return DB_FULL;
        }

        Schema schema;

        status = schema_init(&schema, table_name);

        if (status != DB_OK) {
            fclose(file);
            return status;
        }

        /*
         * The next line should be:
         * COLUMNS N
         */
        if (fgets(line, sizeof(line), file) == NULL) {
            fclose(file);
            return DB_PARSE_ERROR;
        }

        uint16_t column_count = 0;

        if (sscanf(line, "COLUMNS %hu", &column_count) != 1) {
            fclose(file);
            return DB_PARSE_ERROR;
        }

        for (uint16_t i = 0; i < column_count; i++) {
            char column_name[MAX_COLUMN_NAME];
            char type_name[32];
            char first_flag[32];
            char second_flag[32];

            if (fgets(line, sizeof(line), file) == NULL) {
                fclose(file);
                return DB_PARSE_ERROR;
            }

            /*
             * Column lines look like:
             * id INT
             * name TEXT
             */
            first_flag[0] = '\0';
            second_flag[0] = '\0';

            if (
                sscanf(
                    line,
                    "%63s %31s %31s %31s",
                    column_name,
                    type_name,
                    first_flag,
                    second_flag
                ) < 2
            ) {
                fclose(file);
                return DB_PARSE_ERROR;
            }

            ValueType type;

            status = catalog_string_to_type(type_name, &type);

            if (status != DB_OK) {
                fclose(file);
                return status;
            }

            bool not_null = false;
            bool primary_key = false;

            status = catalog_parse_constraint_flags(
                first_flag,
                second_flag,
                &not_null,
                &primary_key
            );

            if (status != DB_OK) {
                fclose(file);
                return status;
            }

            status = schema_add_column(
                &schema,
                column_name,
                type,
                not_null,
                primary_key
            );

            if (status != DB_OK) {
                fclose(file);
                return status;
            }
        }

        /*
         * A table definition must end with:
         * END
         */
        if (fgets(line, sizeof(line), file) == NULL) {
            fclose(file);
            return DB_PARSE_ERROR;
        }

        if (strncmp(line, "END", 3) != 0) {
            fclose(file);
            return DB_PARSE_ERROR;
        }

        db->catalog.tables[db->catalog.table_count] = schema;
        db->catalog.table_count++;
    }

    fclose(file);
    return DB_OK;
}

DBStatus catalog_save(const DB *db) {
    if (db == NULL) {
        return DB_ERROR;
    }

    char catalog_path[MAX_DB_PATH];

    DBStatus status = catalog_build_path(
        db,
        catalog_path,
        sizeof(catalog_path)
    );

    if (status != DB_OK) {
        return status;
    }

    /*
     * Rewrite the full catalog each time.
     * This is simple and fine for this stage.
     */
    FILE *file = fopen(catalog_path, "w");

    if (file == NULL) {
        return DB_IO_ERROR;
    }

    for (uint16_t i = 0; i < db->catalog.table_count; i++) {
        const Schema *schema = &db->catalog.tables[i];

        fprintf(file, "TABLE %s\n", schema->table_name);
        fprintf(file, "COLUMNS %u\n", schema->column_count);

        for (uint16_t j = 0; j < schema->column_count; j++) {
            const Column *column = &schema->columns[j];

            fprintf(
                file,
                "%s %s",
                column->name,
                catalog_type_to_string(column->type)
            );

            if (column->primary_key) {
                fprintf(file, " PRIMARY_KEY");
            }

            if (column->not_null) {
                fprintf(file, " NOT_NULL");
            }

            fprintf(file, "\n");
        }

        fprintf(file, "END\n");
    }

    for (uint16_t i = 0; i < db->catalog.index_count; i++) {
        const CatalogIndex *index = &db->catalog.indexes[i];

        /*
         * Persist explicit indexes with a column count so composite index
         * definitions round-trip without needing escaping or nested blocks.
         */
        fprintf(file, "INDEX %s %s %u", index->index_name, index->table_name, index->column_count);

        for (uint16_t j = 0; j < index->column_count; j++) {
            fprintf(file, " %s", index->column_names[j]);
        }

        fprintf(file, "%s\n", index->unique ? " UNIQUE" : "");
    }

    if (fclose(file) != 0) {
        return DB_IO_ERROR;
    }

    return DB_OK;
}

DBStatus catalog_create_index(DB *db, const CatalogIndex *index) {
    if (db == NULL || index == NULL) {
        return DB_ERROR;
    }

    if (
        catalog_index_exists(db, index->index_name) ||
        !catalog_table_exists(db, index->table_name)
    ) {
        return DB_ERROR;
    }

    if (db->catalog.index_count >= MAX_CATALOG_INDEXES) {
        return DB_FULL;
    }

    char index_path[MAX_DB_PATH];

    DBStatus status = catalog_build_secondary_index_path(
        db,
        index->index_name,
        index_path,
        sizeof(index_path)
    );

    if (status != DB_OK) {
        return status;
    }

    FILE *file = fopen(index_path, "rb");

    if (file == NULL) {
        return DB_IO_ERROR;
    }

    if (fclose(file) != 0) {
        return DB_IO_ERROR;
    }

    db->catalog.indexes[db->catalog.index_count] = *index;
    db->catalog.index_count++;

    status = catalog_save(db);

    if (status != DB_OK) {
        db->catalog.index_count--;
        return status;
    }

    return DB_OK;
}

DBStatus catalog_drop_index(DB *db, const char *index_name) {
    if (db == NULL || index_name == NULL) {
        return DB_ERROR;
    }

    for (uint16_t i = 0; i < db->catalog.index_count; i++) {
        if (strcmp(db->catalog.indexes[i].index_name, index_name) != 0) {
            continue;
        }

        CatalogIndex removed = db->catalog.indexes[i];

        for (uint16_t j = i + 1; j < db->catalog.index_count; j++) {
            db->catalog.indexes[j - 1] = db->catalog.indexes[j];
        }

        db->catalog.index_count--;
        memset(&db->catalog.indexes[db->catalog.index_count], 0, sizeof(CatalogIndex));

        DBStatus status = catalog_save(db);

        if (status != DB_OK) {
            /*
             * Put the in-memory catalog back if persistence failed. The index
             * file itself is removed by the executor only after this succeeds.
             */
            for (uint16_t j = db->catalog.index_count; j > i; j--) {
                db->catalog.indexes[j] = db->catalog.indexes[j - 1];
            }

            db->catalog.indexes[i] = removed;
            db->catalog.index_count++;
            return status;
        }

        return DB_OK;
    }

    return DB_NOT_FOUND;
}

DBStatus catalog_create_table(DB *db, const Schema *schema) {
    if (db == NULL || schema == NULL) {
        return DB_ERROR;
    }

    if (catalog_table_exists(db, schema->table_name)) {
        return DB_ERROR;
    }

    if (db->catalog.table_count >= MAX_CATALOG_TABLES) {
        return DB_FULL;
    }

    /*
     * Add the schema to the in-memory catalog first.
     */
    db->catalog.tables[db->catalog.table_count] = *schema;
    db->catalog.table_count++;

    char table_file_path[MAX_DB_PATH];

    DBStatus status = catalog_build_table_file_path(
        db,
        schema->table_name,
        table_file_path,
        sizeof(table_file_path)
    );

    if (status != DB_OK) {
        db->catalog.table_count--;
        return status;
    }

    /*
     * Create the table data file if it does not exist.
     * The file can start empty; pages are allocated later.
     */
    FILE *table_file = fopen(table_file_path, "ab");

    if (table_file == NULL) {
        db->catalog.table_count--;
        return DB_IO_ERROR;
    }

    if (fclose(table_file) != 0) {
        db->catalog.table_count--;
        return DB_IO_ERROR;
    }

    status = catalog_create_primary_key_index_file(db, schema);

    if (status != DB_OK) {
        db->catalog.table_count--;
        return status;
    }

    /*
     * Persist the updated catalog immediately.
     */
    status = catalog_save(db);

    if (status != DB_OK) {
        db->catalog.table_count--;
        return status;
    }

    return DB_OK;
}

DBStatus catalog_get_schema(
    const DB *db,
    const char *table_name,
    Schema *out_schema
) {
    if (db == NULL || table_name == NULL || out_schema == NULL) {
        return DB_ERROR;
    }

    for (uint16_t i = 0; i < db->catalog.table_count; i++) {
        const Schema *schema = &db->catalog.tables[i];

        if (strcmp(schema->table_name, table_name) == 0) {
            *out_schema = *schema;
            return DB_OK;
        }
    }

    return DB_NOT_FOUND;
}

bool catalog_table_exists(const DB *db, const char *table_name) {
    if (db == NULL || table_name == NULL) {
        return false;
    }

    for (uint16_t i = 0; i < db->catalog.table_count; i++) {
        if (strcmp(db->catalog.tables[i].table_name, table_name) == 0) {
            return true;
        }
    }

    return false;
}

bool catalog_index_exists(const DB *db, const char *index_name) {
    if (db == NULL || index_name == NULL) {
        return false;
    }

    for (uint16_t i = 0; i < db->catalog.index_count; i++) {
        if (strcmp(db->catalog.indexes[i].index_name, index_name) == 0) {
            return true;
        }
    }

    return false;
}

DBStatus catalog_find_index_for_column(
    const DB *db,
    const char *table_name,
    const char *column_name,
    CatalogIndex *out_index
) {
    if (db == NULL || table_name == NULL || column_name == NULL || out_index == NULL) {
        return DB_ERROR;
    }

    for (uint16_t i = 0; i < db->catalog.index_count; i++) {
        const CatalogIndex *index = &db->catalog.indexes[i];

        if (
            strcmp(index->table_name, table_name) == 0
        ) {
            for (uint16_t j = 0; j < index->column_count; j++) {
                if (strcmp(index->column_names[j], column_name) == 0) {
                    *out_index = *index;
                    return DB_OK;
                }
            }
        }
    }

    return DB_NOT_FOUND;
}

void catalog_list_tables(const DB *db, FILE *out) {
    if (db == NULL || out == NULL) {
        return;
    }

    for (uint16_t i = 0; i < db->catalog.table_count; i++) {
        fprintf(out, "%s\n", db->catalog.tables[i].table_name);
    }
}
