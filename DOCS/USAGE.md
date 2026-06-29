# Usage

## Requirements

- A C11 compiler such as `gcc` or `clang`
- `make`

## Build

```sh
make
```

This creates the `MiniDB` executable in the repository root.

## Start The Shell

```sh
make run
```

or:

```sh
./MiniDB
```

By default, the shell opens or creates a database under `mydb/`.

To print the version:

```sh
./MiniDB --version
```

## Install

From a source checkout:

```sh
make
make install
```

The default install prefix is `/usr/local`. Override it with:

```sh
make install PREFIX="$HOME/.local"
```

To uninstall from the same prefix:

```sh
make uninstall PREFIX="$HOME/.local"
```

After install, `MiniDB --version` works because `make install` copies the
binary into a `bin` directory on your shell's `PATH`. See
[RELEASE](RELEASE.md) for the full explanation.

## Example Session

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

MiniDB currently expects one SQL statement per input line. SQL statements
should end with a semicolon. Shell commands begin with a dot and do not need a
semicolon.

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
