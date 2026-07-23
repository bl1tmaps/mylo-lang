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
    * [Variables](#variables)
        + [Numbers and Strings](#numbers-and-strings)
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
    * [Memory Management - Garbage Collection](#memory-management)
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
Mylo uses a high-level syntax like Python or Javascript, but is *Gradually Typed*. Code
is compiled to a virtual machine (VM), and it is executed on any platform with Mylo installed. Mylo also
supports compiling to a binary, with embedded VM code, as well as interfacing with native libraries.

<a name="variables"></a>
## Variables
<a name="numbers-and-strings"></a>
### Numbers and Strings
Mylo is gradually typed, but types are inferred for numbers, strings, bools, enums, maps and lists. Here is how to define variables.
```javascript
var a_number = 42
var a_string = "Hi"
```
Mylo handles variable overrides for you, so this is ok:
```javascript
var my_var = 40
// This is a comment by the way. Below we override my_var with a string.
my_var = "Oh no!"
```

You can define specific types like this:
```javascript
var my_int: i32 = 50
```

<a name="types"></a>
## Types

Mylo infers types at compile time, and can promote at runtime (math operations always use 64-bit floats).
It uses **Type Annotations** to check safety of functions. This allows for efficient memory usage, 
precise binary layouts, and seamless interoperability with C functions.
<a name="primitives"></a>
### Primitive Types

| Type   | Description              | Size (Bytes) | C Equivalent    |
|:-------|:-------------------------|:-------------|:----------------|
| `num`  | Standard Number (Double) | 8            | `double`        |
| `bool` | Boolean (0 or 1)         | 1            | `unsigned char` |
| `byte` | Unsigned Byte            | 1            | `unsigned char` |
| `i16`  | 16-bit Signed Integer    | 2            | `short`         |
| `i32`  | 32-bit Signed Integer    | 4            | `int`           |
| `i64`  | 64-bit Signed Integer    | 8            | `long long`     |
| `f32`  | 32-bit Floating Point    | 4            | `float`         |
| `str`  | String                   | -            | `char*`         |

#### Variable Annotations

You can annotate variables to enforce types or define specific storage formats.

**Standard Dynamic Variables:**
```javascript
var x = 10          // Defaults to 'num' (f64)
var list = [1, 2]   // Defaults to generic list of 'num'
```

**Typed Arrays:**
To create a compact, typed array (packed binary data), use the `[]` syntax in the annotation.

```javascript
// 32-bit Integer Array (Compact Storage)
var counts: i32[] = [100, 200, 300]

// Byte Array (Buffer)
var pixels: byte[] = [255, 0, 128, 255]

// Float Array (Good for Geometry/GPU data)
var vertices: f32[] = [1.0, 0.5, -1.0]
```

#### Type Behavior

1.  **Storage:** Typed arrays (`i32[]`, `byte[]`, etc.) store elements tightly packed in memory, not as generic Mylo values. This significantly reduces memory overhead.
2.  **Runtime:** When you access a value from a typed array (e.g. `val = counts[0]`), it is converted to a standard `num` (double) on the stack so you can perform standard math on it.
3.  **C-Bindings:** When passing a typed array to a C binding (via `C(...)`), Mylo passes the raw pointer (e.g. `int*`, `unsigned char*`), allowing for zero-copy overhead.

<a name="structs"></a>
### Structs (Classes & Object Orientation)
You can make your own types using the `struct` keyword. Structs in Mylo support fields and methods, giving them object-oriented capabilities similar to classes.

The type is specified after `var` using the `:` operator. Struct fields can be optionally separated by commas.

```javascript
struct Color {
    var r,
    var g,
    var b,
    
    // You can define methods directly inside structs!
    // The first parameter is usually `self` to access the struct instance.
    fn to_grayscale(self) {
        ret (self.r + self.g + self.b) / 3
    }
}

// Very red
var my_colour: Color = { r=255, g=0, b=0 }

// Call a method
print(my_colour.to_grayscale())
```

#### Structs can also be annotated.

```javascript
struct Vec3 { 
    var x: f32, 
    var y: f32, 
    var z: f32 
}

var pos: Vec3 = {x: 10.0, y: 20.0, z: 0.0}
var mesh: Vec3[] = [{x: 1.0}, {x: 2.0}]
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
var myvar = Transport::car
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
var a = "foo"
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
var my_list = ["a", "b", "c"]
var my_list = [1, 2, 3]
```

<a name="arrays-of-structs"></a>
### Arrays of Structs
Arrays of a given struct are defined as usual with type information, and specified with `[]` operators. If not
type is given for the list initialization, the type will either be promoted to Any[] or an error will be given if this
is not possible.

```javascript
struct Color {
    var rgba
}
// Notice that arrays of structs need type annotation otherwise they will be Any[].
var my_list: Color[] = [{rgba=1000}, {rgba=2000}]
```

<a name="adding-to-or-concatenating-arrays"></a>
### Adding to or Concatenating Arrays
Elements can be added to an array with the `+` operator, with the
elements to be added in braces `[]`.

```javascript
struct Color {
    var rgba
}
// Empty list of Colors
var my_list: Color[] = []
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
    var rgba
}

// Start empty
var my_list: Color[] = []
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
var numbers = [10, 20, 30]

// Addition
for (x in numbers+5) {
    print(x)
}   // [15, 25, 35]

// Subtraction (Scalar - Array supported)
var numbers = [10, 20, 30]

for (x in 100-numbers) {
    print(x)
}  // [90, 80, 70]

// Multiplication
numbers * 2    // [20, 40, 60]

// Modulo
var vals = [10, 11, 12]
vals % 2       // [0, 1, 0]
```

#### String Broadcasting
You can add strings to an array to **prepend** or **append** text to every element.

```javascript
var files = ["image1", "image2"]

// Append: Array + String
var pngs = files + ".png" 
// ["image1.png", "image2.png"]

// Prepend: String + Array
var paths = "/home/user/" + pngs
// ["/home/user/image1.png", "/home/user/image2.png"]
```

#### Byte Array Math
Byte arrays (`b"..."`) support vector math. The result is automatically promoted to a standard List (`TYPE_ARRAY`) to safely handle values that exceed 255 (0xFF).

```javascript
// Raw bytes: [254, 255]
var data = b"\xFE\xFF" 

// Add 2 to every byte
// Result is a number list: [256, 257]
var result = data + 2 

print(result[0]) // 256
```

<a name="bytes"></a>
## Bytes

Bytes can be defined using the `b""` string literal. You can use standard ASCII characters or Hexadecimal escape codes for non-printable bytes.

```javascript
var byte_e = b"0"
print(byte_e)

// Standard Bytes
var byte_array = b"PNG"
for (B in byte_array) {
    print(B) // Prints 80, 78, 71
}

// Hexadecimal Escape Codes
// Represents [255, 0, 10, 65]
var binary_data = b"\xFF\x00\x0A\x41"
print(binary_data[0]) // 255
```

<a name="maps"></a>
## Maps (Dictionaries)

Maps are key-value collections where keys are always strings and values can be any type. They are dynamic and will grow as new keys are added.
You can create a map using the map literal syntax `{}`. Keys must be strings, and assignments use the `=` operator.

```javascript
// Empty Map
var empty = {}

// Map with initial values
var user = {
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
var name = user["name"]
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
var age = 5
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
for (var x in 0...5) {
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
var index = 0
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
var my_pets = ["dog", "cat", "fish"]
// notice we didn't need 'var x'
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
var i = 0
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
for (var x in 0...5) {
    if (x == 1) {
        break
    }
    print(x)
}

for (var x in 0...100) {
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
// This function takes an input 'n' and returns 
// n + 1
fn add_one_to_my_number(n)
{
    ret n + 1
}

var my_num = 99

print(add_one_to_my_number(my_num))
// ^^ prints 100 
print(my_num)
// ^^ prints 99? Why?
// because my_num was copied to add_one_to_my_number
// and the output was passed to print

// Now what happens?
my_num = add_one_to_my_number(my_num)
print(my_num)
```

Functions without type information are considered to have the annotation 'Any'. Here is an example of how to control 
function prototypes.

```javascript
// Accepts and prints 'any'
fn foo(x) {
    print(x)
}

// Will only accept i32
fn foo_i32(x: i32) {
    print(x)
}
```

<a name="passing-own-types"></a>
#### Passing own types

Functions can reference any type, but use *Strict Types*. This example show how to use a custom type.
The last line shows how Runtime Type Checking is used to make sure functions are compatible.
```javascript
struct foo {
    var name
}

// Will will pass a list of foo, this must be specified
fn list_names(n : foo[])
{
    // x needs to be defined as a foo.
    for (x: foo in n) {
        print(x.name)
    }
    // done.
    ret
}

// define some foos.
var myvar0: foo = [{name="Harry"}, {name="Barry"}, {name="John"}]

// This prints 'Harry', 'Barry', 'John'
list_names(myvar0)
// This would fail, as 99 is not an obj foo[] (list of foo).
// Runtime Error: Type Mismatch. Expected struct type 0, got 1
// list_names([99])
```

<a name="scope-modules"></a>
## Scope & Modules

Variables inside functions have their own scope unless they are passed in.

```javascript
var x = 5

fn foo() {
    var x = 10
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
## Memory Management

#### Garbage Collection in Mylo

Mylo uses a tracing **Mark-and-Sweep Garbage Collector** to manage memory automatically. This means you do not need to manually allocate or free memory; the GC runs periodically when the heap reaches a threshold capacity and cleans up any objects (Lists, Maps, Strings, Structs) that are no longer reachable from the root variables.

In **Mylo**, variables allocated within functions and loops are automatically cleaned up when they go out of scope. If they are stored in longer-lived data structures, they persist until they are no longer accessible.

#### What does this mean for developers?

1.  **Automatic Memory Management:** You do not have to worry about manual memory arenas, tracking allocations, or freeing memory.
2.  **No Region Syntax:** The deprecated `region` and `region_name::variable` syntax is no longer required or supported. Just declare variables using `var` and they will be managed automatically.
3.  **Safety:** The GC guarantees that there will be no dangling pointers or "use-after-free" errors. Accessing memory is fundamentally safe.

#### Example: Temporary Variables (The "Loop" Pattern)

In a game loop or server request handler, you generate temporary data that is only needed for that specific iteration.

```javascript
var i = 0

// Infinite loop simulating a game engine
forever {
    if i > 2 { break }

    print("--- Frame " + to_string(i) + " ---")

    // Create temporary strings and arrays
    // These will be automatically collected by the GC when they go out of scope
    var temp_calc = [i, i * 2, i * 3]
    var log_msg = "Processing entity " + to_string(i)

    print(temp_calc)
    print(log_msg)
    
    i = i + 1
}
```

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
    var x
}


// Uses "c_foo" automatically, Mylo will make
// this struct available at compile time.
var oh_my: foo = C() -> foo {
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
Mylo's numbers are doubles as default, so we can cast chars or other types and pass them back from C:
```javascript
struct Color {
    var r
    var g
    var b
}

var pixel : Color = C() -> Color {
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
    var x = C() -> byte[] {
        const char* my_data = "\x01\x02\x03\x00\x04";
        int len = 5;

        // 1. Calculate size (Same as before)
        int doubles_needed = (len + 7) / 8;

        // 2. Allocate (NOW RETURNS DOUBLE)
        // heap_alloc now returns a packed handle, not a raw index.
        double ptr = heap_alloc(2 + doubles_needed);

        // 3. Resolve Pointer (CRITICAL NEW STEP)
        // You must convert the handle into a raw C pointer to the memory.
        double* base = vm_resolve_ptr(ptr);

        // 4. Write Header to 'base'
        // TYPE_BYTES is -2
        base[0] = -2;
        base[1] = (double)len;

        // 5. Copy Data
        // We take the address of the 3rd double (&base[2]) and write bytes there.
        memcpy(&base[2], my_data, len);

        // 6. Return the handle (ptr), NOT the raw pointer (base)
        // T_OBJ is 2
        return (MyloReturn){ .value = ptr, .type = 2 };
    }
    ret x
}
```

<a name="sending-values-to-c"></a>
#### Sending values to C
Here we send an int, and a string to c, and print them using
printf.
````javascript
var a: i32 = 100
var b = "200"

struct MyStruct {
    var x
}

var X : MyStruct = {x=9}

C(val: i32 = a, val2 : str = b, val3 : MyStruct = X) {
    printf("Inside C: %d, %s, %d\n", (int)val, val2, (int)val3->x);
}
````

<a name="namespace-complexity"></a>
#### Namespace Complexity
Mylo uses `mod` as its module/namespace seperator. This
must be reflected in C bindings using underscores to type names, like so:

````javascript

mod FOO {
    struct BAR {
        var x
    }
    
    // Notice the type in this function is just BAR
    fn MyFunc(var0: BAR) {
        // Notice tht type passed to C must be FOO_BAR
        C(var_to_c : FOO_BAR = var0) {
            
            //... things
            
            // Access to the var
            var_to_c->x = 99999;

            //... things
        }
    }
}


````


<a name="getting-type-handles"></a>
#### Getting Type Handles !

Libraries have custom types in C. Mylo has memory handlers which
allow a 'Handle' to those objects to be passed around in Mylo. In this example
Raylib's Image object is utilised.

```javascript
import C "raylib.h"

// 1. Return an ID (num)
var img_id: num = C() -> num {
    Image raw = LoadImage("cat.png");
    // "Image" is hashed automatically
    // and is used to remember the type.
    return MYLO_STORE(raw, "Image");
}

// Back in Mylo ...
// img_id is just a handle (num), and is only meaningful to C.
print(img_id)

// img_id is now passed back to C
C(id: num = img_id) {
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
var x = 10
C(v: num = x) {
    printf("Value is: %f\n", v);
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
directly by declaring them as a `cfn` (c-function). The input, and return 
types must be provided.
```
// 'cfn' defines a c function, in this case
// the standard library atoi. It *must* also
// have the correct return type, otherwise it
// will return null
cfn atoi(x : str) -> f32
// Prints 7
print(f"{atoi("7")}")
```

#### Above C code generation
The above, when compiled generates this c code:
```
float __mylo_user_0(VM* vm, char* x) {
    return atoi(x);
}
```

<a name="dynamically-running-native-code---c-bindings"></a>
## Dynamically Running Native Code - C Bindings

### Building a Dynamic C Library, to use in the Mylo Interpreter

Building the mylo application hinders the ability the test and extend an
intpreted Mylo application. We can build a binding to allow Mylo to load
compiled C code and use them. Here is a simple example included below.

You will need two files: the Library (test_lib.mylo) containing the C code, and the App (test_app.mylo) that imports and runs it.

### 1. The Library (test_lib.mylo)
This file defines the C functionality we want to share. It is a regular Mylo file,
but had inline C.

```javascript
fn add(a: num, b: num) {
    var r = C(a: num = a, b: num = b) -> num {
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
