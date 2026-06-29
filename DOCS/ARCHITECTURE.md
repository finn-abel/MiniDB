# Architecture

MiniDB is organized as a small relational database with a SQL front end and a
persistent storage path. The code favors clear module boundaries over broad
abstractions.

## High-Level Flow

```text
SQL text
  -> lexer
  -> parser
  -> binder
  -> planner
  -> executor
  -> table / record / index / transaction layers
  -> pager and on-disk files
```

## Core Modules

`value`
: Owns scalar values such as `INT` and `TEXT`.

`row`
: Owns arrays of values and row serialization/deserialization.

`schema`
: Defines table schemas and validates row shape, types, and constraints.

`page`
: Manages raw bytes inside fixed-size slotted pages.

`pager`
: Reads and writes fixed-size pages to table and index files.

`record`
: Combines rows, pages, pager operations, and record identifiers.

`table`
: Wraps a schema, table file path, pager, and table-level operations.

`catalog`
: Persists table and index metadata.

`db`
: Owns database-level startup, shutdown, catalog state, WAL, and transaction
state.

`sql`
: Contains the lexer, parser, AST, and binder.

`execution`
: Converts bound statements into plans and executes them.

`index`
: Contains B+ tree primary-key support and secondary index support.

`transaction`
: Tracks transaction state and writes/replays WAL records.

## Storage Files

MiniDB stores database state in the configured database directory. The default
shell path is `mydb/`.

```text
mydb/
  catalog.db
  minidb.wal
  tables/
  indexes/
```

Table rows live in slotted pages under `tables/`. Index files live under
`indexes/`. The catalog records the metadata needed to reopen tables and
indexes on restart.

## Design Notes

- Low-level modules return `DBStatus` values instead of printing user-facing
  messages.
- SQL-facing code is responsible for statement shape, names, planning, and
  execution flow.
- Storage modules should remain usable without depending on the SQL shell.
- Tests are organized by module and run as standalone executables.
