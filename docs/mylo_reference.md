<a name="mylo-reference"></a>
# Mylo Reference

<a name="language-basics"></a>
## Language Basics
Mylo uses familiar programming concepts: variables, loops, conditional statements, functions, modules etc.
It is fast to write, fast to run, and can run anywhere.

### Contents
- [Mylo Reference](#mylo-reference)
    * [Language Basics](#language-basics)
    * [Technical Intro (for nerds)](#technical-intro-for-nerds)
    * [Variables & Scoping](#variables)
        + [Declaration Syntax](#declaration-syntax)
        + [Strict Typing Rules](#strict-typing-rules)
        + [Monitor](#monitor)
    * [Types](#types)
        + [Primitives](#primitive-types)
        + [Structs](#structs)
        + [Enums](#enums)
        + [Bools](#bools)
    * [Lists/Arrays](#listsarrays)
        + [Arrays of Structs](#arrays-of-structs)
        + [Adding to or Concatenating Arrays](#adding-to-or-concatenating-arrays)
        + [Sub-Arrays or Array-Slicing](#sub-arrays-or-array-slicing)
        + [Vector Math & Broadcasting](#vector-math-broadcasting)
    * [Bytes](#bytes)
    * [Maps](#maps)
        + [Accessing Values](#accessing-values)
        + [Modifying and Inserting](#modifying-and-inserting)
    * [Control flow](#control-flow)
        + [Conditionals (If Statements)](#conditionals-if-statements)
        + [For loops](#for-loops)
            - [Range Based](#range-based)
            - [Checked](#checked)
            - [Iterated](#iterated)
            - [Forever](#forever)
            - [Break & Continue](#break-and-continue)
    * [Functions](#functions)
        - [Passing own types](#passing-own-types)
    * [Scope & Modules](#scope-modules)
    * [Memory Management - Arenas & Regions](#memory-management)
        + [Lexical Scoped Region Blocks](#lexical-scoped-region-blocks)
        + [Persistent Named Regions & Explicit Lifecycles](#persistent-named-regions--explicit-lifecycles)
        + [Memory Reclamation & Compile-Time Safety](#memory-reclamation--compile-time-safety)
    * [Import & Module Path](#import)
    * [C Interoperability Foreign Function Interface (FFI)](#c-interoperability-foreign-function-interface-ffi)
        + [Source example.mylo](#source-examplemylo)
        + [Passing variables to C](#passing-variables-to-c)
            - [Getting values back](#getting-values-back)
            - [Getting Arrays from C](#getting-arrays-from-c)
            - [Sending values to C](#sending-values-to-c)
            - [Namespace Complexity](#namespace-complexity)
        + [Getting Type Handles](#getting-type-handles)
        + [Void blocks](#void-blocks)
        + [Calling C Functions (cfn)](#c-function-calling-native-c-functions-by-prototype)
    * [Dynamically Running Native Code - C Bindings](#dynamically-running-native-code---c-bindings)

<a name="technical-intro-for-nerds"></a>
## Technical Intro (for nerds)
Mylo uses a high-level syntax like Python or Javascript. Code
is compiled to a virtual machine (VM), and it is executed on any platform with Mylo installed. Mylo also
supports compiling to a binary, with embedded VM code, as well as interfacing with native libraries.

<a name="variables"></a>
## Variables & Scoping

Mylo is strictly and statically typed. All variable declarations require an explicit type annotation and there is no type guessing.

### Declaration Syntax
Variables are declared by specifying the name, a colon `:`, the type, and an initial value:

```javascript
a_number: i32 = 42
a_string: string = "Hi"
```

### Strict Typing Rules
1. **Mandatory Type Annotations (No Guessing):**
   Attempting to assign to an undeclared variable without a type annotation is a compile-time error:
   ```javascript
   x = 8 // Compiler Error: what is x?
   ```
2. **No Redefinition in the Same Scope:**
   Declaring a variable name that already exists in the current scope is prohibited:
   ```javascript
   x: string = "foo"
   x: u32 = 4 // Compiler Error: redefinition of 'x' in the same scope
   ```
3. **No Type Mutation:**
   Variables cannot change their type. Assigning a value of a different type to an existing variable is a compile-time error:
   ```javascript
   x: string = "foo"
   x = 4 // Compiler Error: cannot assign i32 to string
   ```
4. **Lexical Block Scoping:**
   Variables declared inside a block `{ ... }` exist only within that scope. Referencing an out-of-scope variable causes a compile-time error:
   ```javascript
   {
       x: u8 = 5
   }
   print(x) // Compiler Error: 'x' does not exist anymore
   ```


<a name="monitor"></a>
## Monitor

You can inspect the runtime memory state, active arenas, and currently live symbols at any point in your program using `monitor()`:

```javascript
region foo
foo::x: i32 = 6555
monitor()

clear foo
monitor()
```

Output:
```bash
  Memory Monitor
  Arenas
  Current Arena: 0, Head: 0 / 1048576
  Arena 1: Head: 0 / 1048576

   Globals
  foo: 1
  foo_x: 6555

   Locals (Frame #0)
  (None)
  Memory Monitor
  Arenas
  Current Arena: 0, Head: 0 / 1048576

   Globals

   Locals (Frame #0)
  (None)
```

Notice how `monitor()` displays the active Arena 1 and `foo_x` while the region is active, and once `clear foo` runs, Arena 1 is reclaimed and the variable disappears from the active symbol table.

<a name="types"></a>
## Types

Mylo is strictly and statically typed. It uses **Type Annotations** to guarantee type safety at compile time and runtime. This enables efficient memory packing, predictable binary layouts, and seamless interoperability with C functions.
<a name="primitives"></a>
### Primitive Types

| Type     | Description                | Size (Bytes) | C Equivalent    |
|:---------|:---------------------------|:-------------|:----------------|
| `u8`     | 8-bit Unsigned Integer     | 1            | `unsigned char` |
| `u16`    | 16-bit Unsigned Integer    | 2            | `uint16_t`      |
| `u32`    | 32-bit Unsigned Integer    | 4            | `uint32_t`      |
| `u64`    | 64-bit Unsigned Integer    | 8            | `uint64_t`      |
| `i8`     | 8-bit Signed Integer       | 1            | `int8_t`        |
| `i16`    | 16-bit Signed Integer      | 2            | `int16_t`       |
| `i32`    | 32-bit Signed Integer      | 4            | `int32_t`       |
| `i64`    | 64-bit Signed Integer      | 8            | `int64_t`       |
| `f16`    | 16-bit Half-Precision Float| 2            | `uint16_t`      |
| `f32`    | 32-bit Floating Point      | 4            | `float`         |
| `f64`    | 64-bit Floating Point      | 8            | `double`        |
| `string` | UTF-8 String (or `str`)    | -            | `char*`         |
| `struct` | User-defined Struct type   | -            | `void*`         |
| `enum`   | User-defined Enum type     | -            | `int`           |
| `array`  | Array (or typed `T[]`)     | -            | `void*`         |
| `map`    | Hash Map / Dictionary      | -            | `void*`         |
| `bool`   | Boolean (0 or 1)           | 1            | `unsigned char` |

> **Note:** The `num` type has been completely removed in favor of explicit signed, unsigned, and floating point numeric types.

#### Variable Annotations

Every variable in Mylo must be annotated with a specific type:

```javascript
x: i32 = 10                // 32-bit signed integer
pi: f64 = 3.14159          // 64-bit float
name: string = "Mylo"      // UTF-8 string
active: bool = true        // Boolean flag
```

**Typed Arrays:**
To create a compact, typed array (packed binary data), use the `[]` syntax in the annotation.

```javascript
// 32-bit Integer Array (Compact Storage)
counts: i32[] = [100, 200, 300]

// Byte Array (Buffer)
pixels: u8[] = [255, 0, 128, 255]

// Float Array (Good for Geometry/GPU data)
vertices: f32[] = [1.0, 0.5, -1.0]
```

#### Type Behavior

1.  **Storage:** Typed arrays (`i32[]`, `byte[]`, etc.) store elements tightly packed in memory, not as generic Mylo values. This significantly reduces memory overhead.
2.  **Runtime:** When you access a value from a typed array (e.g. `val = counts[0]`), it is loaded as a 64-bit float (`double`) on the VM stack so you can perform standard math on it.
3.  **C-Bindings:** When passing a typed array to a C binding (via `C(...)`), Mylo passes the raw pointer (e.g. `int*`, `unsigned char*`), allowing for zero-copy overhead.

<a name="structs"></a>
### Structs (Classes & Object Orientation)
You can make your own types using the `struct` keyword. Structs in Mylo support fields and methods, giving them object-oriented capabilities similar to classes.

The type is specified after `var` using the `:` operator. Struct fields can be optionally separated by commas.

```javascript
struct Color {
    r: i32,
    g: i32,
    b: i32,
    
    // You can define methods directly inside structs!
    // The first parameter is usually `self` to access the struct instance.
    fn to_grayscale(self) {
        ret (self.r + self.g + self.b) / 3
    }
}

// Very red
my_colour: Color = { r=255, g=0, b=0 }

// Call a method
print(my_colour.to_grayscale())
```

#### Structs can also be annotated.

```javascript
struct Vec3 { 
    x: f32, 
    y: f32, 
    z: f32 
}

pos: Vec3 = {x: 10.0, y: 20.0, z: 0.0}
mesh: Vec3[] = [{x: 1.0}, {x: 2.0}]
```

<a name="enums"></a>
### Enums
You can also define your own enumerated variations on types like this:

```javascript
enum Transport {
    car,
    bus,
    bike
}
myvar: Transport = Transport::car
if (myvar == Transport::car) {
    print("He's in a car!")
}
```

Enums also have string reflection:
```javascript
print(Transport::car)
// 'car'
print(f"I'm on the {Transpot::bus}")
// I'm on the bus.
```

They can also be iterated
```javascript
for (x in Transport) {
    print(f"{x}") // car, then bus, then bike
}
```

<a name="bools"></a>
### Bools
Truth statements evaluate to 0 for false and 1 for true. These are also codified as `true` and `false`

```javascript
a: string = "foo"
if (true == false) { // Never true
    // never executes
    a = "bar"
}
// "foo"
print(a)
```

<a name="listsarrays"></a>
## Lists/Arrays

Arrays for string and numbers can be defined just like regular variables,
using the `[]` syntax.
```javascript
my_list: string[] = ["a", "b", "c"]
my_list: i32[] = [1, 2, 3]
```

<a name="arrays-of-structs"></a>
### Arrays of Structs
Arrays of a given struct are defined as usual with type information, and specified with `[]` operators. If not
type is given for the list initialization, the type will either be promoted to Any[] or an error will be given if this
is not possible.

```javascript
struct Color {
    rgba: i32
}
// Notice that arrays of structs need type annotation otherwise they will be Any[].
my_list: Color[] = [{rgba=1000}, {rgba=2000}]
```

<a name="adding-to-or-concatenating-arrays"></a>
### Adding to or Concatenating Arrays
Elements can be added to an array with the `+` operator, with the
elements to be added in braces `[]`.

```javascript
struct Color {
    rgba: i32
}
// Empty list of Colors
my_list: Color[] = []
// Add an element
my_list = mylist + [{rgba: 500}]

// Print the first
print(my_list[0])
```

<a name="sub-arrays-or-array-slicing"></a>
### Sub-Arrays or Array-Slicing
Elements can be sliced using the `:` operator in the index. Inclusive Slicing (like Ruby ranges or standard math [start, end]) is used so:

- `[0:1]` returning indices 0 and 1 (first and next).
- `[1:3]` returning indices 1, 2, and 3 (second, third and fourth element).
- `[-1]`  returning the last element.
```javascript
struct Color {
    rgba: i32
}

// Start empty
my_list: Color[] = []
// Put some values in
my_list = my_list + [{rgba=500}, {rgba=400}, {rgba=300}, {rgba=700}, {rgba=900}]

// Slice to get [{400}, {300}, {700}]
my_list = my_list[1:3]

// Print them out
for (x: Color in my_list) {
    print(x.rgba)
}
```

<a name="vector-math-broadcasting"></a>
### Vector Math & Broadcasting

Mylo supports vector arithmetic, allowing you to apply operations to every element of an array or byte string simultaneously.

#### Number Arrays
You can use standard math operators (`+`, `-`, `*`, `/`, `%`) between an array and a single number (scalar).

```javascript
numbers: i32[] = [10, 20, 30]

// Addition
for (x in numbers+5) {
    print(x)
}   // [15, 25, 35]

// Subtraction (Scalar - Array supported)
numbers: i32[] = [10, 20, 30]

for (x in 100-numbers) {
    print(x)
}  // [90, 80, 70]

// Multiplication
numbers * 2    // [20, 40, 60]

// Modulo
vals: i32[] = [10, 11, 12]
vals % 2       // [0, 1, 0]
```

#### String Broadcasting
You can add strings to an array to **prepend** or **append** text to every element.

```javascript
files: string[] = ["image1", "image2"]

// Append: Array + String
pngs: string[] = files + ".png" 
// ["image1.png", "image2.png"]

// Prepend: String + Array
paths: string[] = "/home/user/" + pngs
// ["/home/user/image1.png", "/home/user/image2.png"]
```

#### Byte Array Math
Byte arrays (`b"..."`) support vector math. The result is automatically promoted to a standard List (`TYPE_ARRAY`) to safely handle values that exceed 255 (0xFF).

```javascript
// Raw bytes: [254, 255]
data: bytes = b"\xFE\xFF" 

// Add 2 to every byte
// Result is a number list: [256, 257]
result: i32[] = data + 2 

print(result[0]) // 256
```

<a name="bytes"></a>
## Bytes

Bytes can be defined using the `b""` string literal. You can use standard ASCII characters or Hexadecimal escape codes for non-printable bytes.

```javascript
byte_e: bytes = b"0"
print(byte_e)

// Standard Bytes
byte_array: bytes = b"PNG"
for (B in byte_array) {
    print(B) // Prints 80, 78, 71
}

// Hexadecimal Escape Codes
// Represents [255, 0, 10, 65]
binary_data: bytes = b"\xFF\x00\x0A\x41"
print(binary_data[0]) // 255
```

<a name="maps"></a>
## Maps (Dictionaries)

Maps are key-value collections where keys are always strings and values can be any type. They are dynamic and will grow as new keys are added.
You can create a map using the map literal syntax `{}`. Keys must be strings, and assignments use the `=` operator.

```javascript
// Empty Map
empty: map = {}

// Map with initial values
user: map = {
    "name" = "Alice",
    "age" = 30,
    "role" = "Admin"
}
```

<a name="accessing-values"></a>
### Accessing Values

Values are retrieved using the bracket syntax `["key"]`.

**Note:** If a key does not exist, accessing it returns an empty string `""`.

```javascript
name: string = user["name"]
print(name) // "Alice"
```

<a name="modifying-and-inserting"></a>
### Modifying and Inserting

Maps are mutable. You can update existing keys or add new ones using the bracket assignment syntax.

```javascript
// Update existing
user["age"] = 31

// Insert new key
user["status"] = "Active"
```

<a name="control-flow"></a>
## Control flow

<a name="conditionals-if-statements"></a>
### Conditionals (If Statements)
We can control the flow of our program like this.
```javascript
age: i32 = 5
// This will print "Hello Little One"
if (age < 10) {
    print("Hello Little One")
} else {
    print("Hello ... not Little One??")
}
```

Mylo supports binary comparisons the same as C:
less than`<`, less than or equal to`<=`, greater than`>`, greater than or equal to`>=`, not equal `!=` and  equal to`==`.

<a name="for-loops"></a>
### For loops
For loops can be range based, checked or iterated. See here:
<a name="range-based"></a>
#### Range Based
```javascript
// This is a loop to count from 0 to 5, and is the same as range(0,1,5) => [0,1,2,3,4,5]
for (x in 0...5) {
    print(x)
}
// You can also go the other way 5...0
```
output:
```shell
0
1
2
3
4
5
```

<a name="checked"></a>
#### Checked
Checked loops check a value each iteration, they could loop
forever if the checked value is never updated.
```javascript
index: i32 = 0
for (index <= 100) {
    print(index)
    // If you comment this out, it will loop forever
    index = index + 1
}
```

<a name="iterated"></a>
#### Iterated
Lists can be iterated over, using the `in` keyword.
```javascript
my_pets: string[] = ["dog", "cat", "fish"]
for (x in my_pets) {
    // This will print dog, then cat, then fish
    // each on a new line.
    print(x)
}
```
<a name="forever"></a>
#### Forever
You can just keep executing a block forever, or until you break out.
```javascript
i: i32 = 0
forever {
    print(i)
    i = i + 1
    
    if (i > 10) {
        break
    }
}
```
<a name="break-and-continue"></a>
#### Break and Continue
You can exit a loop early or jump back and begin the next iteration with `break` and `continue`

```javascript
for (x in 0...5) {
    if (x == 1) {
        break
    }
    print(x)
}

for (x in 0...100) {
    if (x < 50) {
        continue
    }
    print(x)
}
```

<a name="functions"></a>
## Functions

Subroutines/Functions can be defined to allow variables to be passed
and *values* returned.

```javascript
// This function takes an input 'n' and returns n + 1
fn add_one_to_my_number(n: i32) -> i32 {
    ret n + 1
}

my_num: i32 = 99

print(add_one_to_my_number(my_num))
// ^^ prints 100 
print(my_num)
// ^^ prints 99: my_num was passed by value

// Now update my_num with the returned result
my_num = add_one_to_my_number(my_num)
print(my_num)
```

Functions without explicit type annotations default to `any`. You can annotate parameter types and return types using `-> Type`:

```javascript
// Accepts and prints 'any'
fn foo(x) {
    print(x)
}

// Accepts only i32 and returns an i32
fn foo_i32(x: i32) -> i32 {
    ret x * 2
}
```

<a name="passing-own-types"></a>
#### Passing own types

Functions can reference any type, including custom structs.

```javascript
struct Person {
    name: string
}

// Pass a list of Person structs
fn list_names(people: Person[]) {
    for (p: Person in people) {
        print(p.name)
    }
    ret
}

// Define some Person structs
my_people: Person[] = [{name="Harry"}, {name="Barry"}, {name="John"}]

// This prints 'Harry', 'Barry', 'John'
list_names(my_people)
```
```

<a name="scope-modules"></a>
## Scope & Modules

Variables inside functions have their own scope unless they are passed in.

```javascript
x: i32 = 5

fn foo() {
    x: i32 = 10
    ret x
}

// Prints 5
print(x)
// Prints 10
print(foo())
```

Functions and variables can be gathered into Modules, and accessed with the `::` operator.

```javascript

fn a_big_number() {
    ret 65535
}

mod MyFirstMod {
    // Same name as above, but in a module.
    fn a_big_number() {
        ret 99999
    }
}

// Prints 99999
print(MyFirstMod::a_big_number())

// Prints 65556
print(a_big_number())
```
<a name="memory-management"></a>
## Memory Management - Arenas & Regions

Mylo relies entirely on **Generational Memory Arenas**. The garbage collector has been removed in favor of bump-allocation speed (as fast as stack allocations) and deterministic, zero-overhead memory reclamation.

Arenas eliminate GC pauses, heap fragmentation, and unpredictable latency, making Mylo ideal for systems programming, real-time games, audio, and embedded targets (like the Commodore Amiga).

### How Arenas Work
Instead of tracking individual object allocations with reference counters or tracing garbage collection passes:
1. Allocations linearly bump a pointer within a dedicated chunk of memory (`MemoryArena`).
2. Objects allocated within an arena live together.
3. When the arena is cleared or its enclosing scope exits, the entire block of memory is reclaimed instantaneously in $O(1)$ time by resetting the head pointer and incrementing the generation counter.

### The 'main' Arena
By default, all global allocations outside of any explicit region live in Arena 0 (the `main` arena).

---

<a name="lexical-scoped-region-blocks"></a>
### Lexical Scoped Region Blocks

For scoped, temporary allocations, Mylo provides `region` blocks. All dynamic values (such as arrays, maps, strings, and structs) created within the block are placed into a freshly allocated arena. When execution exits the closing brace `}`, the arena is automatically freed and all memory is instantly reclaimed.

#### Anonymous Regions
```javascript
region {
    temp_arr: i32[] = [1, 2, 3, 4, 5]
    print(temp_arr[0]) // 1
    print(temp_arr[4]) // 5
}
// Arena is destroyed here; temp_arr is out of scope and memory is reclaimed.
```

#### Named Region Blocks
You can give lexical regions an explicit name for documentation and debugging clarity:
```javascript
x: i64 = 999
region myregion {
    v: i32 = 777
    arr: i32[] = [10, 20, 30]
    print(v)       // 777
    print(arr[1])  // 20
}
print(x) // 999
```

#### Nested Regions
Regions can be nested arbitrarily. Mylo manages an internal arena stack (`vm.arena_stack`) that pushes each newly created arena and pops back to the parent arena when the inner region completes:
```javascript
region outer {
    a: i32 = 100
    region inner {
        b: i32 = 200
        print(a + b) // 300
    }
    print(a) // 100
}
```

#### Regions Inside Functions
Regions are particularly effective inside performance-critical functions or game loops, allowing heavy temporary scratchpad work without leaking memory:
```javascript
fn compute_sum() -> i32 {
    region temp {
        buffer: i32[] = [10, 20, 30, 40]
        total: i32 = 0
        for (item in buffer) {
            total = total + item
        }
        ret total
    }
}
print(compute_sum()) // 100
```

---

<a name="persistent-named-regions--explicit-lifecycles"></a>
### Persistent Named Regions & Explicit Lifecycles

While lexical `region { ... }` blocks mirror the call stack, many architectures (such as state machines, asset loaders, level systems, and long-lived game loops) require allocations that span multiple functions or frames before being bulk-cleared on demand.

Mylo supports **Persistent Named Regions** using the syntax `region <name>`, qualified member assignment `<name>::<var>`, and explicit teardown with `clear <name>` (or `clear(<name>)`).

#### Declaring and Populating a Named Region
```javascript
region foo

// Allocate variables directly into region 'foo'
foo::x : i32 = 6555
foo::arr : i32[] = [10, 20, 30]

print(foo::x)      // 6555
print(foo::arr[1]) // 20

// Reclaim all memory allocated in 'foo'
clear foo
```

#### Using Named Regions Across Function Boundaries
Because persistent regions remain open until explicitly cleared, functions can populate them collaboratively:
```javascript
region cache

fn populate_cache() {
    cache::data : i32[] = [42, 84, 126]
    print("Cache populated!")
}

fn process_cache() {
    print(f"First element: {cache::data[0]}")
    print(f"Second element: {cache::data[1]}")
}

populate_cache()
process_cache()

// Tear down the cache arena and free all items at once
clear cache
```

---

<a name="memory-reclamation--compile-time-safety"></a>
### Memory Reclamation & Compile-Time Safety

Mylo guarantees safety and prevents use-after-free bugs at compile-time:

1. **Lexical Scope Errors:**
   Referencing a variable declared inside a lexical region block from outside causes a compile-time error:
   ```javascript
   region myreg {
       v: i32 = 42
   }
   print(v) // Compiler Error: Undefined var 'v'
   ```

2. **Access After `clear` Errors:**
   Once a persistent region has been closed with `clear foo`, any subsequent access to symbols belonging to that region is caught and rejected by the compiler:
   ```javascript
   region foo
   foo::x : i32 = 6555
   clear foo
   print(foo::x) // Compiler Error: Undefined var 'foo_x'
   ```

3. **Symbol Lifetime Tracking (`start_ip` / `end_ip`):**
   The compiler records the exact instruction pointer range for which every symbol is valid. At runtime, tools like `monitor()`, the CLI debugger (`--db`), and the VSCode DAP debugger (`--dap`) query this table to ensure that only currently live variables are presented to the developer.

### Benefits of Arenas
1. **Ultra Fast Allocation:** Allocation is a single pointer bump (no free-list traversal or GC tracking).
2. **Instant Deallocation:** When the scope ends or `clear` runs, memory is instantly reclaimed without GC pauses.
3. **Deterministic & Safe:** Scoping ensures that out-of-scope references are caught at compile-time.

<a name="import"></a>
## Import - Using other Mylo files
Other files, containing modules can be imported into the program using `import`, and search path can be added with
`module_path()`.

Here is an example, see `test_import.mylo`

```javascript
fn my_test() {
    print("3")
}
```

Now in our main.mylo:
```javascript
    // The mylo file is in tests, and we usually would run from either the 
    // top level or build directory, so I've added some relative module paths
    // to search
    module_path("../tests/")
    module_path("tests/")
    module_path("../../tests/")
    // my_test is imported here
    import "test_import.mylo"
    
    // This prints 3
    my_test()
```

<a name="c-interoperability-foreign-function-interface-ffi"></a>
## C Interoperability Foreign Function Interface (FFI)

Most software, especially that running at high speed, interfacing with hardware, is written either in C or with a C API.

To make Mylo useful, C code can be compiled within a Mylo source file.
This is achieved by compiling the VM assembly and embedding the VM into a C-application, and generating interfaces
with C variables and functions.

<a name="source-examplemylo"></a>
### Source example.mylo
```javascript
struct foo {
    x: i32
}


// Uses "c_foo" automatically, Mylo will make
// this struct available at compile time.
oh_my: foo = C() -> foo {
    c_foo val;
    val.x = 6;
    return val;
}

print(f"foo.x : {oh_my.x}")
```

We build it by producing a C source file with embedded VM code and wrapper code pre-generated.

```shell
# This produces an out.c file
> mylo --build example.mylo
```

We then just compile it, like any other C file, but we need to include
the Mylo VM source and headers.
```shell
# This produces mylo_app
> gcc out.c ../src/vm.c -o mylo_app -I../src -lm
```

Our application can then be executed (without a runtime):
```shell
# Run
> ./mylo_app
  foo.x : 6
```

<a name="passing-variables-to-c"></a>
### Passing variables to C

It's important to be able to pass information to C, and crucially, to get it back again!
This includes arbitrary structures from C!

<a name="getting-values-back"></a>
#### Getting values back
Mylo numbers can cast bytes or other types and pass them back from C:
```javascript
struct Color {
    r: f64
    g: f64
    b: f64
}

pixel: Color = C() -> Color {
    struct color_rgb { char r; char g; char b; };
    // Simulated native data: 255 (0xFF), 0 (0x00), 128 (0x80)
    struct color_rgb raw = { 255, 0, 128 };

    c_Color out;
    // FIX: Cast to (unsigned char) first to prevent negative numbers
    out.r = (double)(unsigned char)raw.r;
    out.g = (double)(unsigned char)raw.g;
    out.b = (double)(unsigned char)raw.b;

    return out;
}

print(pixel.r) // Now prints 255
print(pixel.b) // Now prints 128
```

<a name="getting-arrays-from-c"></a>
#### Getting Arrays from C
```javascript
// Unknown return type falls back to 'MyloReturn'
fn get_bytes() {
    x: bytes = C() -> byte[] {
        const char* my_data = "\x01\x02\x03\x00\x04";
        int len = 5;

        // 1. Calculate size
        int doubles_needed = (len + 7) / 8;

        // 2. Allocate handle
        double ptr = heap_alloc(2 + doubles_needed);

        // 3. Resolve Pointer
        // Convert the handle into a raw C pointer to the memory.
        double* base = vm_resolve_ptr(ptr);

        // 4. Write Header to 'base' (TYPE_BYTES is -2)
        base[0] = -2;
        base[1] = (double)len;

        // 5. Copy Data
        memcpy(&base[2], my_data, len);

        // 6. Return the handle (ptr), NOT the raw pointer (base)
        return (MyloReturn){ .value = ptr, .type = 2 };
    }
    ret x
}
```

<a name="sending-values-to-c"></a>
#### Sending values to C
Here we send an int, and a string to C, and print them using `printf`:
```javascript
a: i32 = 100
b: str = "200"

struct MyStruct {
    x: i32
}

X: MyStruct = {x=9}

C(val: i32 = a, val2: str = b, val3: MyStruct = X) {
    printf("Inside C: %d, %s, %d\n", (int)val, val2, (int)val3->x);
}
```

<a name="namespace-complexity"></a>
#### Namespace Complexity
Mylo uses `mod` as its module/namespace separator. This
must be reflected in C bindings using underscores to type names, like so:

```javascript
mod FOO {
    struct BAR {
        x: i32
    }
    
    // Notice the type in this function is just BAR
    fn MyFunc(var0: BAR) {
        // Notice the type passed to C must be FOO_BAR
        C(var_to_c: FOO_BAR = var0) {
            // Access to the var
            var_to_c->x = 99999;
        }
    }
}
```


````


<a name="getting-type-handles"></a>
#### Getting Type Handles !

Libraries have custom types in C. Mylo has memory handlers which
allow a 'Handle' to those objects to be passed around in Mylo. In this example
Raylib's Image object is utilised.

```javascript
import C "raylib.h"

// 1. Return an ID (handle)
img_id: i32 = C() -> i32 {
    Image raw = LoadImage("cat.png");
    // "Image" is hashed automatically
    // and is used to remember the type.
    return MYLO_STORE(raw, "Image");
}

// Back in Mylo ...
// img_id is just a handle (i32), and is only meaningful to C.
print(img_id)

// img_id is now passed back to C
C(id: i32 = img_id) {
    // 2. Retrieve it safely
    // Checks if ID exists AND if the stored hash matches hash("Image")
    Image* img = MYLO_RETRIEVE(id, Image, "Image");
    
    // Null if not found
    if (img) {
        // Here we can use the object again
        printf("Image size: %d x %d\n", img->width, img->height);
    }
}
```

<a name="void-blocks"></a>
#### Void blocks

Running C code can return no values
```javascript
// This works as a statement
C() {
    printf("I am a void C block!\n");
}

// Arguments still work
x: i32 = 10
C(v: i32 = x) {
    printf("Value is: %d\n", v);
}

// This will now throw an error, you can't have a function 
// called C.
fn C() {
    print("Cannot do this")
}
```

### Including C Headers

To include C header code (to define functions to call in native blocks) you use
the 'C' identifier in import statements
```javascript
import C "cool_func.h"

C() {
    cool_func();
}
```

### C-Function: Calling native C functions by prototype

Mylo allows functions from C, such as `atoi` below, to be called
directly by declaring them as a `cfn` (c-function). The input and return 
types must be provided.
```javascript
// 'cfn' defines a C function, in this case
// the standard library atoi. It *must* also
// have the correct return type.
cfn atoi(x: str) -> f32
// Prints 7
print(f"{atoi("7")}")
```

#### Automatic Runtime Dynamic Resolution
When running in interpreted mode (`./mylo script.mylo`), Mylo automatically attempts to resolve `cfn` symbols from the running host process and loaded libraries at compile/load time.
- If the symbol is found (for example standard C library functions or functions linked into Mylo), Mylo dynamically binds and executes it directly in interpreter mode without requiring a compilation step.
- If the symbol is not found (for example, functions from an external library that is not yet linked or provided via shared library), Mylo reports:
  ```
  error: native function <name> was not found, either build mylo with these libraries, or provide shared libraries
  Error: This program contains Native C blocks and no shared objects are found, so it cannot be interpreted.
  Please compile it to bytecode using: mylo --build <file>.mylo
  ```
  with terminal syntax highlighting.

#### C Code Generation for Native Builds
When compiled using `mylo --build script.mylo`, Mylo generates standard C wrappers:
```c
float __mylo_user_0(VM* vm, char* x) {
    return atoi(x);
}
```
and registers the wrapper into the native dispatch table in `out.c`.

<a name="dynamically-running-native-code---c-bindings"></a>
## Dynamically Running Native Code - C Bindings

### Building a Dynamic C Library, to use in the Mylo Interpreter

Building the mylo application hinders the ability the test and extend an
interpreted Mylo application. We can build a binding to allow Mylo to load
compiled C code and use them. Here is a simple example included below.

You will need two files: the Library (test_lib.mylo) containing the C code, and the App (test_app.mylo) that imports and runs it.

### 1. The Library (test_lib.mylo)
This file defines the C functionality we want to share. It is a regular Mylo file,
but has inline C.

```javascript
fn add(a: i32, b: i32) {
    r: i32 = C(a: i32 = a, b: i32 = b) -> i32 {
        return a + b;
    }
    ret r
}
```

### 1.2 Create a Shared Library

#### Generate the C-code for compilation
```shell
# This outputs test_lib.mylo_bind.c
mylo --bind test_lib.mylo  
```

#### Generate the shared library
The shared libary must have the same name as our mylo file (test_lib). This
will be dynamically loaded by the interpreter. We *must* include the header files for Mylo
```shell
cc -shared -fPIC test_lib.mylo_bind.c -Isrc -o test_lib.so  
```

### 2.0 Importing our binding in Mylo
The `native` keyword tells Mylo to load a shared object at runtime.
```javascript
// Native code is imported
import native "test_lib.mylo"

// The 'add' function is our native function
print(add(10, 20))
```

#### Output
```shell
> mylo test_app.mylo

Mylo: Loading Native Module './test_lib.so'...
Mylo: Bound 1 native functions starting at ID 14
30 # <-- add(10, 20)
```

### Standard Library
The standard functions are still rather limited, but can do file manipulation, array insights and some maths.
This is actively being developed to add more power to the VM.

Check out the [Standard Library Reference](standardlib_reference.md).
