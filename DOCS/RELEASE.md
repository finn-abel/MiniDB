# Release

MiniDB publishes two local release artifacts:

- Source archive: `dist/MiniDB-0.1.2.tar.gz`
- Binary archive: `dist/MiniDB-0.1.2-<system>-<machine>.tar.gz`

The source archive is portable across systems with a C11 compiler and `make`.
The binary archive is built for the current machine and can be run without
building from source on a compatible system.

## Build A Release

```sh
make release
```

This runs `make check`, creates the source archive, and creates the binary
archive.

To build only the source archive:

```sh
make dist
```

To build only the binary archive:

```sh
make package
```

## Install From Source

```sh
tar -xzf MiniDB-0.1.2.tar.gz
cd MiniDB-0.1.2
make
make install
```

The default install prefix is `/usr/local`. Override it with:

```sh
make install PREFIX="$HOME/.local"
```

## Why `MiniDB --version` Works After Install

The install flow is old-school Unix. The important part is the `install`
target in the `Makefile`:

```make
TARGET = MiniDB
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: $(TARGET)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 "$(TARGET)" "$(DESTDIR)$(BINDIR)/$(TARGET)"
```

What happens:

1. `make` builds a binary called `MiniDB` in the project root.
2. `make install PREFIX=/usr/local` copies that binary to:

```sh
/usr/local/bin/MiniDB
```

3. `/usr/local/bin` is normally on your shell's `PATH`.
4. After that, your terminal can find `MiniDB` from anywhere:

```sh
MiniDB --version
MiniDB
```

Before installing, you run it by path:

```sh
./MiniDB --version
```

After installing, the command name alone works because the binary lives in a
directory your shell searches.

The binary archive uses the same idea. Its `install.sh` copies:

```sh
bin/MiniDB
```

to:

```sh
/usr/local/bin/MiniDB
```

If you do not want `sudo`, install into a user-local bin directory instead:

```sh
make install PREFIX="$HOME/.local"
```

or, from the binary archive:

```sh
PREFIX="$HOME/.local" ./install.sh
```

That puts the command at:

```sh
~/.local/bin/MiniDB
```

Then make sure this is in your shell config:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

For zsh on macOS, that usually goes in:

```sh
~/.zshrc
```

The reusable idea is: build an executable, copy it into a `bin` directory on
`PATH`, and give it executable permissions with `install -m 0755`. The terminal
command is just the filename installed there.

## Install From A Binary Archive

```sh
tar -xzf MiniDB-0.1.2-<system>-<machine>.tar.gz
cd MiniDB-0.1.2-<system>-<machine>
./install.sh
MiniDB --version
MiniDB
```

`MiniDB --version` works after installation because `install.sh` copies the
executable into a directory on `PATH`, `/usr/local/bin` by default. Before
installation, use the direct path:

```sh
./bin/MiniDB --version
```

Override the install prefix with:

```sh
PREFIX="$HOME/.local" ./install.sh
```

When using a custom prefix, make sure its `bin` directory is on `PATH`.

## Uninstall

If installed from source:

```sh
make uninstall
```

If installed from a binary archive:

```sh
rm -f /usr/local/bin/MiniDB
```
