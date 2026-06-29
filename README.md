# MiniDB

[![CI](https://github.com/finn-abel/MiniDB-C/actions/workflows/ci.yml/badge.svg)](https://github.com/finn-abel/MiniDB-C/actions/workflows/ci.yml)

MiniDB is a compact relational database engine written in C. It ships as a
single CLI binary with a SQL shell, persistent on-disk storage, catalog
metadata, primary and secondary indexes, transactions, and write-ahead logging.

Current version: `0.1.2`

## Capabilities

- Persistent database directory under `mydb/`
- SQL shell with parser, binder, planner, and executor layers
- `INT` and `TEXT` column types
- `CREATE TABLE`, `INSERT`, `SELECT`, `UPDATE`, and `DELETE`
- `CREATE INDEX` and `DROP INDEX`
- `PRIMARY KEY` and `NOT NULL` constraints
- Comparison predicates with `=`, `!=`, `>`, `<`, `>=`, and `<=`
- Primary-key B+ tree indexes
- Secondary indexes
- Catalog persistence
- Write-ahead log recovery path
- CI-backed checks with formatting, static analysis, unit tests, and sanitizers

## Install

Download a binary release for your platform, extract it, and run the installer:

```sh
tar -xzf MiniDB-0.1.2-<system>-<machine>.tar.gz
cd MiniDB-0.1.2-<system>-<machine>
./install.sh
MiniDB --version
```

By default, the installer copies `MiniDB` to `/usr/local/bin`. To install
without elevated permissions, use a user-local prefix:

```sh
PREFIX="$HOME/.local" ./install.sh
```

Make sure `$HOME/.local/bin` is on your `PATH` when using a custom prefix.

## Build From Source

Requirements:

- C11 compiler such as `gcc` or `clang`
- `make`

Build and run from a source checkout:

```sh
make
./MiniDB
```

Install from source:

```sh
make install
MiniDB --version
```

Use a custom install prefix when needed:

```sh
make install PREFIX="$HOME/.local"
```

Uninstall from the same prefix:

```sh
make uninstall PREFIX="$HOME/.local"
```

## Usage

Start the shell:

```sh
MiniDB
```

or, from an uninstalled source checkout:

```sh
make run
```

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

MiniDB currently accepts one SQL statement per input line. SQL statements end
with a semicolon. Shell commands start with a dot and do not need a semicolon.

## Supported SQL

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

Supported shell commands:

```text
.help
.exit
.tables
.schema <table>
```

## Verification

Run the full local verification suite:

```sh
make check
```

This runs:

- `make fmt-check`
- `make analyze`
- `make test`
- `make test-asan`

Run only the unit suite:

```sh
make test
```

## Releases

Create source and binary release archives:

```sh
make release
```

This runs `make check`, then writes:

```text
dist/MiniDB-0.1.2.tar.gz
dist/MiniDB-0.1.2-<system>-<machine>.tar.gz
```

Create only the source archive:

```sh
make dist
```

Create only the binary archive:

```sh
make package
```

Remove generated release artifacts:

```sh
make clean-dist
```

## Runtime Files

The default database path is `mydb/`.

```text
mydb/
  catalog.db
  minidb.wal
  tables/
    table_name.tbl
  indexes/
    table_name_pk.btree
    index_name.sidx
```

Runtime database files are local state and should not be committed.

## Documentation

- [USAGE](DOCS/USAGE.md)
- [DEVELOPMENT](DOCS/DEVELOPMENT.md)
- [ARCHITECTURE](DOCS/ARCHITECTURE.md)
- [RELEASE](DOCS/RELEASE.md)

## License

MiniDB is licensed under the [MIT License](LICENSE).
