# `wcc` - the `Wonderful C compiler`
C Compiler, largely based on `chibicc` (used as a tutorial). The IR register allocation is also inspired off of `9cc`.

Currently it implements Calculator Number Lang. Single letter variables, simple expressions, semicolons, etc. It also now has `if`, `while`, and `for`.

I plan to also add a proper IR optimizer & codegen after I get far enough.

Generates code for aarch64. I plan to add x86 later.

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
