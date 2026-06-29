# MiniDB

MiniDB is a small self-contained relational database written in C. It has a
simple SQL shell, on-disk table/catalog storage, row serialization, buffer pool
support, primary-key and secondary indexes, and a focused test suite for the
core database layers.

## Features

- Interactive shell backed by a persistent `mydb/` database directory
- SQL lexer, parser, binder, planner, and executor pipeline
- `INT` and `TEXT` column types
- `CREATE TABLE`, `INSERT`, `SELECT`, `DELETE`, and `UPDATE`
- Basic column constraints: `PRIMARY KEY` and `NOT NULL`
- Equality and comparison predicates in `WHERE` clauses
- Primary-key B+ tree indexes for integer primary keys
- Explicit secondary indexes with `CREATE INDEX` and `DROP INDEX`
- Catalog persistence for tables and indexes
- Write-ahead log support for autocommitted row changes
- Unit tests for storage, schema, SQL, execution, indexes, and transactions

## Requirements

- A C11 compiler such as `gcc` or `clang`
- `make`

## Build

```sh
make
```

This builds the `MiniDB` executable.

## Run

```sh
make run
```

or:

```sh
./MiniDB
```

The shell opens or creates a database at `mydb/`.

Example session:

```sql
CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT);
INSERT INTO users VALUES (1, "Finn", 20);
INSERT INTO users VALUES (2, "Alex", 21);
SELECT * FROM users;
SELECT name, age FROM users WHERE id = 1;
UPDATE users SET age = 22 WHERE name = "Alex";
DELETE FROM users WHERE id = 1;
.tables
.schema users
.exit
```

## Supported SQL

MiniDB currently supports one statement per input line. SQL statements should
end with a semicolon.

```sql
CREATE TABLE table_name (
    column_name INT [PRIMARY KEY] [NOT NULL],
    column_name TEXT [NOT NULL]
);

CREATE INDEX index_name ON table_name (column_name [, column_name ...]);
DROP INDEX index_name;

INSERT INTO table_name VALUES (1, "text");

SELECT * FROM table_name;
SELECT column_name [, column_name ...] FROM table_name
    [WHERE column_name = 1];

DELETE FROM table_name [WHERE column_name != "text"];

UPDATE table_name SET column_name = value
    [WHERE column_name >= value];
```

Supported `WHERE` operators:

```text
=  !=  >  <  >=  <=
```

Supported shell commands:

```text
.help
.exit
.tables
.schema <table>
```

## Test

```sh
make test
```

The test target builds each test executable, runs the full suite, and then
cleans generated binaries and objects.

For the full local verification pass, run:

```sh
make check
```

This runs formatting checks, static analysis, the unit suite, and sanitizer
tests.

To remove build output manually:

```sh
make clean
```

## Documentation

Additional docs live under `DOCS/`:

- [USAGE](DOCS/USAGE.md)
- [DEVELOPMENT](DOCS/DEVELOPMENT.md)
- [ARCHITECTURE](DOCS/ARCHITECTURE.md)

## Project Layout

```text
include/                 Public headers
src/                     Database implementation
src/sql/                 Lexer, parser, binder, and AST
src/execution/           Planner and executor
src/buffer/              Buffer pool and replacement policy
src/index/               B+ tree and secondary index support
src/storage/             Free-space management
src/transaction/         Transaction state and WAL
tests/                   Unit tests
Makefile                 Build, run, test, and clean targets
```

## Notes

- The default CLI database path is `mydb/`.
- Table files are stored under `mydb/tables/`.
- Index files are stored under `mydb/indexes/`.
- The catalog is persisted as `mydb/catalog.db`.
- The WAL is persisted as `mydb/minidb.wal`.

## License

MiniDB is licensed under the [MIT License](LICENSE).
