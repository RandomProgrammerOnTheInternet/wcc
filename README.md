# `wcc` - the `Wonderful C compiler`
C Compiler, largely based on `chibicc` (used as a tutorial). The IR register allocation is also inspired off of `9cc`.

Work in Progress.....

Generates code for aarch64 & x86_64. I have not tested if the x86_64 backend works on the latest commit because I don't have an x86 machine.

## Usage

```
wcc version 0.0.1 build May 26 2026
Usage: ./bin/wcc <input file> [-o <output asm file>] [-t <arch>-<abi>] [-d] [-?/--help]
  -o <output>:          file to output assembly to (stdout is default)
  -t <arch>-<abi>:      target architecture, abi
                        only aarch64-apple, x64-sysv
                        are supported.
  -d:                   enable debug IR printing
  -?, --help:           this page 
```

## Build

```sh
$ make
```

## Test

```sh
$ make test
```

## Help

```sh
$ make help  
```

## Calculate Swag Points

```sh
$ make count
```
