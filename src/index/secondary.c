#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "index/secondary.h"
#include "record.h"
#include "rid.h"
#include "row.h"
#include "schema.h"
#include "value.h"

#define SECONDARY_INDEX_MAGIC 0x58444953u
#define SECONDARY_INDEX_VERSION 1

/*
 * Secondary index files are intentionally simple append-only snapshots:
 *
 *   header: magic, version, indexed-column count
 *   entry*: RID, key value 0, key value 1, ...
 *
 * They are rebuilt from table rows after mutations and db_open, so there is no
 * in-place page management here. That tradeoff keeps duplicate keys, TEXT
 * keys, and composite keys correct without changing the primary-key B+ tree.
 */
typedef struct {
    FILE *file;
    const Schema *schema;
    const uint16_t *column_indexes;
    uint16_t column_count;
} SecondaryBuildContext;

static DBStatus secondary_write_exact(FILE *file, const void *data, size_t size) {
    if (file == NULL || data == NULL) {
        return DB_ERROR;
    }

    return fwrite(data, 1, size, file) == size ? DB_OK : DB_IO_ERROR;
}

static DBStatus secondary_read_exact(FILE *file, void *data, size_t size) {
    if (file == NULL || data == NULL) {
        return DB_ERROR;
    }

    return fread(data, 1, size, file) == size ? DB_OK : DB_IO_ERROR;
}

static DBStatus secondary_write_header(FILE *file, uint16_t column_count) {
    uint32_t magic = SECONDARY_INDEX_MAGIC;
    uint16_t version = SECONDARY_INDEX_VERSION;

    DBStatus status = secondary_write_exact(file, &magic, sizeof(magic));

    if (status != DB_OK) {
        return status;
    }

    status = secondary_write_exact(file, &version, sizeof(version));

    if (status != DB_OK) {
        return status;
    }

    return secondary_write_exact(file, &column_count, sizeof(column_count));
}

static DBStatus secondary_read_header(FILE *file, uint16_t *out_column_count) {
    uint32_t magic = 0;
    uint16_t version = 0;

    DBStatus status = secondary_read_exact(file, &magic, sizeof(magic));

    if (status != DB_OK) {
        return status;
    }

    status = secondary_read_exact(file, &version, sizeof(version));

    if (status != DB_OK) {
        return status;
    }

    status = secondary_read_exact(file, out_column_count, sizeof(*out_column_count));

    if (status != DB_OK) {
        return status;
    }

    /*
     * The header is the only structural guard before scanning variable-length
     * entries. Reject unknown versions and impossible column counts early.
     */
    if (
        magic != SECONDARY_INDEX_MAGIC ||
        version != SECONDARY_INDEX_VERSION ||
        *out_column_count == 0 ||
        *out_column_count > MAX_COLUMNS
    ) {
        return DB_ERROR;
    }

    return DB_OK;
}

static DBStatus secondary_write_value(FILE *file, const Value *value) {
    if (file == NULL || value == NULL) {
        return DB_ERROR;
    }

    uint8_t type = (uint8_t)value->type;
    DBStatus status = secondary_write_exact(file, &type, sizeof(type));

    if (status != DB_OK) {
        return status;
    }

    if (value->type == VALUE_INT) {
        return secondary_write_exact(file, &value->int_value, sizeof(value->int_value));
    }

    if (value->type == VALUE_TEXT) {
        if (value->text_value == NULL) {
            return DB_ERROR;
        }

        /*
         * Store TEXT as length + raw bytes. The null terminator is process
         * memory detail, not part of the index format.
         */
        uint32_t len = (uint32_t)strlen(value->text_value);

        status = secondary_write_exact(file, &len, sizeof(len));

        if (status != DB_OK) {
            return status;
        }

        return secondary_write_exact(file, value->text_value, len);
    }

    return DB_TYPE_ERROR;
}

static DBStatus secondary_read_value(FILE *file, Value *out_value) {
    uint8_t type = 0;

    if (file == NULL || out_value == NULL) {
        return DB_ERROR;
    }

    DBStatus status = secondary_read_exact(file, &type, sizeof(type));

    if (status != DB_OK) {
        return status;
    }

    if (type == VALUE_INT) {
        int32_t value = 0;

        status = secondary_read_exact(file, &value, sizeof(value));

        if (status != DB_OK) {
            return status;
        }

        *out_value = value_int(value);
        return DB_OK;
    }

    if (type == VALUE_TEXT) {
        uint32_t len = 0;

        status = secondary_read_exact(file, &len, sizeof(len));

        if (status != DB_OK) {
            return status;
        }

        if (len > PAGE_SIZE) {
            return DB_ERROR;
        }

        char *text = malloc((size_t)len + 1);

        if (text == NULL) {
            return DB_ERROR;
        }

        if (fread(text, 1, len, file) != len) {
            free(text);
            return DB_IO_ERROR;
        }

        text[len] = '\0';
        out_value->type = VALUE_TEXT;
        out_value->text_value = text;
        return DB_OK;
    }

    return DB_TYPE_ERROR;
}

static DBStatus secondary_write_entry(
    FILE *file,
    const Row *row,
    RID rid,
    const uint16_t *column_indexes,
    uint16_t column_count
) {
    DBStatus status = secondary_write_exact(file, &rid.page_id, sizeof(rid.page_id));

    if (status != DB_OK) {
        return status;
    }

    status = secondary_write_exact(file, &rid.slot_id, sizeof(rid.slot_id));

    if (status != DB_OK) {
        return status;
    }

    /*
     * Composite indexes store values in catalog column order. Scan callers
     * pass the index-column position they want to compare.
     */
    for (uint16_t i = 0; i < column_count; i++) {
        const Value *value = row_get_value_const(row, column_indexes[i]);

        if (value == NULL) {
            return DB_ERROR;
        }

        status = secondary_write_value(file, value);

        if (status != DB_OK) {
            return status;
        }
    }

    return DB_OK;
}

static DBStatus secondary_build_callback(const Row *row, RID rid, void *context) {
    SecondaryBuildContext *build = context;

    if (row == NULL || build == NULL) {
        return DB_ERROR;
    }

    return secondary_write_entry(
        build->file,
        row,
        rid,
        build->column_indexes,
        build->column_count
    );
}

DBStatus secondary_index_build(
    const char *index_file,
    const char *table_file,
    const Schema *schema,
    const uint16_t *column_indexes,
    uint16_t column_count
) {
    if (
        index_file == NULL ||
        table_file == NULL ||
        schema == NULL ||
        column_indexes == NULL ||
        column_count == 0 ||
        column_count > MAX_COLUMNS
    ) {
        return DB_ERROR;
    }

    for (uint16_t i = 0; i < column_count; i++) {
        if (column_indexes[i] >= schema->column_count) {
            return DB_ERROR;
        }
    }

    FILE *file = fopen(index_file, "wb");

    if (file == NULL) {
        return DB_IO_ERROR;
    }

    DBStatus status = secondary_write_header(file, column_count);

    if (status == DB_OK) {
        SecondaryBuildContext context;

        context.file = file;
        context.schema = schema;
        context.column_indexes = column_indexes;
        context.column_count = column_count;

        status = record_scan(table_file, secondary_build_callback, &context);
    }

    if (fclose(file) != 0 && status == DB_OK) {
        status = DB_IO_ERROR;
    }

    return status;
}

DBStatus secondary_index_scan_condition(
    const char *index_file,
    uint16_t condition_index_column,
    const WhereCondition *condition,
    SecondaryIndexRIDCallback callback,
    void *context
) {
    if (index_file == NULL || condition == NULL || callback == NULL) {
        return DB_ERROR;
    }

    FILE *file = fopen(index_file, "rb");

    if (file == NULL) {
        return DB_IO_ERROR;
    }

    uint16_t column_count = 0;
    DBStatus status = secondary_read_header(file, &column_count);

    if (status != DB_OK) {
        fclose(file);
        return status;
    }

    if (condition_index_column >= column_count) {
        fclose(file);
        return DB_ERROR;
    }

    /*
     * This is a linear scan over the secondary index snapshot, not a sorted
     * seek. It still avoids deserializing full table rows for non-matching
     * entries and supports duplicates/ranges uniformly.
     */
    while (status == DB_OK) {
        RID rid;
        Value values[MAX_COLUMNS];
        bool matches = false;

        memset(values, 0, sizeof(values));

        size_t read_count = fread(&rid.page_id, 1, sizeof(rid.page_id), file);

        if (read_count == 0 && feof(file)) {
            break;
        }

        if (read_count != sizeof(rid.page_id)) {
            status = DB_IO_ERROR;
            break;
        }

        status = secondary_read_exact(file, &rid.slot_id, sizeof(rid.slot_id));

        if (status != DB_OK) {
            break;
        }

        for (uint16_t i = 0; i < column_count; i++) {
            status = secondary_read_value(file, &values[i]);

            if (status != DB_OK) {
                break;
            }
        }

        if (status == DB_OK) {
            status = value_compare(
                &values[condition_index_column],
                condition->operator_type,
                &condition->value,
                &matches
            );
        }

        /*
         * Free every value read from this entry before invoking the callback
         * result path. RIDs are copied out, so key values are not needed after
         * comparison.
         */
        for (uint16_t i = 0; i < column_count; i++) {
            value_free(&values[i]);
        }

        if (status != DB_OK) {
            break;
        }

        if (matches) {
            status = callback(rid, context);
        }
    }

    if (fclose(file) != 0 && status == DB_OK) {
        status = DB_IO_ERROR;
    }

    return status;
}
