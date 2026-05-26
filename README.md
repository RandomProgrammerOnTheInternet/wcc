# `wcc` - the `Wonderful C compiler`
C Compiler, largely based on `chibicc` (used as a tutorial). The IR register allocation is also inspired off of `9cc`.

Currently it implements Calculator Number Lang. Single letter variables, simple expressions, semicolons, etc. It also now has `if`, `while`, and `for`.

I plan to also add a proper IR optimizer & codegen after I get far enough.

Generates code for aarch64 & x86_64. I have not tested if the x86_64 backend works because I don't have an x86 machine.

## Usage

```
./bin/wcc "program" [-o output] [-t target (either aarch64-apple or x64-sysv)]
````

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
