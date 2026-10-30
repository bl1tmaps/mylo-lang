<!-- TOC --><a name="mylo-standard-library-documentation"></a>
# Mylo Standard Library Documentation

The Mylo Standard Library provides essential built-in functions for file I/O, math, bit manipulation, string operations, data collections, multithreading, and visual web output. All types are strictly annotated and variable declarations require explicit typing (`name: type = value`).

## Table of Contents

- [Mylo Standard Library Documentation](#mylo-standard-library-documentation)
  * [Math Functions](#math-functions)
    + [`sqrt(value: f64) -> f64`](#sqrtvalue-f64---f64)
    + [`sin(angle: f64) -> f64`](#sinangle-f64---f64)
    + [`cos(angle: f64) -> f64`](#cosangle-f64---f64)
    + [`tan(angle: f64) -> f64`](#tanangle-f64---f64)
    + [`floor(value: f64) -> f64`](#floorvalue-f64---f64)
    + [`ceil(value: f64) -> f64`](#ceilvalue-f64---f64)
    + [`abs(value: f64) -> f64`](#absvalue-f64---f64)
    + [`clamp(value: f64, min: f64, max: f64) -> f64`](#clampvalue-f64-min-f64-max-f64---f64)
    + [`mix(start: f64, stop: f64, weight: f64) -> f64`](#mixstart-f64-stop-f64-weight-f64---f64)
    + [`min(val1: f64, val2: f64) -> f64`](#minval1-f64-val2-f64---f64)
    + [`max(val1: f64, val2: f64) -> f64`](#maxval1-f64-val2-f64---f64)
    + [`distance(x1: f64, y1: f64, x2: f64, y2: f64) -> f64`](#distancex1-f64-y1-f64-x2-f64-y2-f64---f64)
    + [`seed(val: f64) -> void`](#seedval-f64---void)
    + [`rand() -> f64`](#rand---f64)
    + [`rand_normal() -> f64`](#rand_normal---f64)
    + [`noise(x: f64, y: f64, z: f64) -> f64`](#noisex-f64-y-f64-z-f64---f64)
  * [Bitwise Operations](#bitwise-operations)
    + [`bit_and(a: i64, b: i64) -> i64`](#bit_anda-i64-b-i64---i64)
    + [`bit_or(a: i64, b: i64) -> i64`](#bit_ora-i64-b-i64---i64)
    + [`bit_xor(a: i64, b: i64) -> i64`](#bit_xora-i64-b-i64---i64)
    + [`bit_not(a: i64) -> i64`](#bit_nota-i64---i64)
    + [`bit_shl(a: i64, b: i64) -> i64`](#bit_shla-i64-b-i64---i64)
    + [`bit_shr(a: i64, b: i64) -> i64`](#bit_shra-i64-b-i64---i64)
    + [`popcount(a: i64) -> i32`](#popcounta-i64---i32)
  * [String Operations & Inspection](#string-operations--inspection)
    + [String Indexing (`str[i]`) and Slicing (`str[start:end]`)](#string-indexing-and-slicing)
    + [`len(collection: any) -> i32`](#lencollection-any---i32)
    + [`ord(s: string) -> i32`](#ords-string---i32)
    + [`chr(code: i32) -> string`](#chrcode-i32---string)
    + [`split(source: string, delimiter: string) -> string[]`](#splitsource-string-delimiter-string---string)
    + [`contains(haystack: any, needle: any) -> i32`](#containshaystack-any-needle-any---i32)
    + [`where(collection: any, item: any) -> i32`](#wherecollection-any-item-any---i32)
  * [Collections & Array Manipulation](#collections--array-manipulation)
    + [`list(size: i32) -> any[]`](#listsize-i32---any)
    + [`range(start: f64, step: f64, stop: f64) -> f64[]`](#rangestart-f64-step-f64-stop-f64---f64)
    + [`add(array: any[], index: i32, value: any) -> any[]`](#addarray-any-index-i32-value-any---any)
    + [`remove(collection: any, key: any) -> void`](#removecollection-any-key-any---void)
    + [`min_list(list: f64[]) -> f64`](#min_listlist-f64---f64)
    + [`max_list(list: f64[]) -> f64`](#max_listlist-f64---f64)
    + [`for_list(func_name: string, list: any[]) -> any[]`](#for_listfunc_name-string-list-any---any)
    + [`filter(array: any[], func_name: string) -> any[]`](#filterarray-any-func_name-string---any)
    + [`param_filter(array: any[], func_name: string, param: any) -> any[]`](#param_filterarray-any-func_name-string-param-any---any)
    + [`copy(value: any) -> any`](#copyvalue-any---any)
  * [Type Inspection & Conversion](#type-inspection--conversion)
    + [`type(value: any) -> string`](#typevalue-any---string)
    + [`to_string(value: any) -> string`](#to_stringvalue-any---string)
    + [`to_num(value: any) -> f64`](#to_numvalue-any---f64)
  * [Time & Timing](#time--timing)
    + [`clock() -> f64`](#clock---f64)
    + [`sleep(ms: i32) -> void`](#sleepms-i32---void)
  * [File I/O (Text & Binary)](#file-io-text--binary)
    + [`read_lines(path: string) -> string[]`](#read_linespath-string---string)
    + [`write_file(path: string, content: string, mode: string) -> i32`](#write_filepath-string-content-string-mode-string---i32)
    + [`read_bytes(path: string, stride: i32) -> i32[]`](#read_bytespath-string-stride-i32---i32)
    + [`write_bytes(path: string, data: i32[]) -> i32`](#write_bytespath-string-data-i32---i32)
    + [`list_dir(path: string, filter: string) -> string[]`](#list_dirpath-string-filter-string---string)
  * [OS System & Process Management](#os-system--process-management)
    + [`system(command: string) -> string[]`](#systemcommand-string---string)
    + [`system_thread(command: string, name: string) -> i32`](#system_threadcommand-string-name-string---i32)
    + [`get_job(name: string) -> any`](#get_jobname-string---any)
  * [Terminal Input](#terminal-input)
    + [`cget(blocking: i32) -> string`](#cgetblocking-i32---string)
    + [`kbhit() -> i32`](#kbhit---i32)
    + [`get_keys() -> string[]`](#get_keys---string)
  * [Multithreading & Universal Bus](#multithreading--universal-bus)
    + [`create_worker(function_name: string) -> i32`](#create_workerfunction_name-string---i32)
    + [`check_worker(worker_id: i32) -> i32`](#check_workerworker_id-i32---i32)
    + [`dock_worker(worker_id: i32) -> void`](#dock_workerworker_id-i32---void)
    + [`bus_set(key: string, value: any) -> i32`](#bus_setkey-string-value-any---i32)
    + [`bus_get(key: string) -> any`](#bus_getkey-string---any)
  * [Web Visual Library](#web-visual-library)
    + [`web_init() -> void`](#web_init---void)
    + [`web_status(wait: i32) -> i32`](#web_statuswait-i32---i32)
    + [`web_clr() -> void`](#web_clr---void)
    + [`web_rect(x: f64, y: f64, w: f64, h: f64, color: string) -> void`](#web_rectx-f64-y-f64-w-f64-h-f64-color-string---void)
    + [`web_circle(x: f64, y: f64, r: f64, color: string) -> void`](#web_circlex-f64-y-f64-r-f64-color-string---void)
    + [`web_line(x1: f64, y1: f64, x2: f64, y2: f64, thickness: f64, color: string) -> void`](#web_linex1-f64-y1-f64-x2-f64-y2-f64-thickness-f64-color-string---void)
    + [`web_triangle(x1: f64, y1: f64, x2: f64, y2: f64, x3: f64, y3: f64, color: string) -> void`](#web_trianglex1-f64-y1-f64-x2-f64-y2-f64-x3-f64-y3-f64-color-string---void)
    + [`web_text(text: string, x: f64, y: f64, size: f64) -> void`](#web_texttext-string-x-f64-y-f64-size-f64---void)
  * [Metaprogramming](#metaprogramming)
    + [`call(name: string, args: any[]) -> any`](#callname-string-args-any---any)

---

## Math Functions

<a name="sqrtvalue-f64---f64"></a>
### `sqrt(value: f64) -> f64`

Calculates the square root of a floating-point number.

**Arguments:**
* `value`: The number to calculate the square root of.

**Returns:**
* The square root as an `f64`.

**Example:**
```javascript
result: f64 = sqrt(16.0)
print(result) // 4
```

---

<a name="sinangle-f64---f64"></a>
### `sin(angle: f64) -> f64`

Calculates the sine of an angle (in radians).

**Arguments:**
* `angle`: The angle in radians.

**Returns:**
* The sine of the angle as an `f64`.

---

<a name="cosangle-f64---f64"></a>
### `cos(angle: f64) -> f64`

Calculates the cosine of an angle (in radians).

**Arguments:**
* `angle`: The angle in radians.

**Returns:**
* The cosine of the angle as an `f64`.

---

<a name="tanangle-f64---f64"></a>
### `tan(angle: f64) -> f64`

Calculates the tangent of an angle (in radians).

**Arguments:**
* `angle`: The angle in radians.

**Returns:**
* The tangent of the angle as an `f64`.

---

<a name="floorvalue-f64---f64"></a>
### `floor(value: f64) -> f64`

Rounds a floating-point number down to the nearest integer value.

**Arguments:**
* `value`: The number to round.

**Returns:**
* The largest integer less than or equal to `value` as an `f64`.

---

<a name="ceilvalue-f64---f64"></a>
### `ceil(value: f64) -> f64`

Rounds a floating-point number up to the nearest integer value.

**Arguments:**
* `value`: The number to round.

**Returns:**
* The smallest integer greater than or equal to `value` as an `f64`.

---

<a name="absvalue-f64---f64"></a>
### `abs(value: f64) -> f64`

Returns the absolute value of a number.

**Arguments:**
* `value`: The numeric value.

**Returns:**
* Absolute value as an `f64`.

**Example:**
```javascript
diff: f64 = abs(-42.5)
print(diff) // 42.5
```

---

<a name="clampvalue-f64-min-f64-max-f64---f64"></a>
### `clamp(value: f64, min: f64, max: f64) -> f64`

Clamps a value to lie within the specified closed range `[min, max]`.

**Arguments:**
* `value`: The input value.
* `min`: The lower bound.
* `max`: The upper bound.

**Returns:**
* Clamped value as an `f64`.

**Example:**
```javascript
v: f64 = clamp(15.0, 0.0, 10.0)
print(v) // 10.0
```

---

<a name="mixstart-f64-stop-f64-weight-f64---f64"></a>
### `mix(start: f64, stop: f64, weight: f64) -> f64`

Linearly interpolates between two values based on a weighting factor: `start + (stop - start) * weight`.

**Arguments:**
* `start`: The starting value.
* `stop`: The ending value.
* `weight`: The interpolation factor (typically between 0.0 and 1.0).

**Returns:**
* The interpolated `f64`.

---

<a name="minval1-f64-val2-f64---f64"></a>
### `min(val1: f64, val2: f64) -> f64`

Returns the smaller of two numbers.

---

<a name="maxval1-f64-val2-f64---f64"></a>
### `max(val1: f64, val2: f64) -> f64`

Returns the larger of two numbers.

---

<a name="distancex1-f64-y1-f64-x2-f64-y2-f64---f64"></a>
### `distance(x1: f64, y1: f64, x2: f64, y2: f64) -> f64`

Calculates the Euclidean distance between two 2D points: `sqrt((x2 - x1)^2 + (y2 - y1)^2)`.

---

<a name="seedval-f64---void"></a>
### `seed(val: f64) -> void`

Initializes the pseudo-random number generator with a specific seed value.

---

<a name="rand---f64"></a>
### `rand() -> f64`

Generates a uniform pseudo-random number in the range `[0.0, 1.0]`.

**Example:**
```javascript
seed(1234.0)
r: f64 = rand()
```

---

<a name="rand_normal---f64"></a>
### `rand_normal() -> f64`

Generates a random number following a Standard Normal Distribution (Gaussian) with mean `0.0` and standard deviation `1.0` using the Box-Muller transform.

---

<a name="noisex-f64-y-f64-z-f64---f64"></a>
### `noise(x: f64, y: f64, z: f64) -> f64`

Calculates 3D Perlin Noise for the given coordinates.

**Arguments:**
* `x`: The X coordinate.
* `y`: The Y coordinate.
* `z`: The Z coordinate (pass `0.0` for 2D noise).

**Returns:**
* A smooth noise value roughly between `-1.0` and `1.0`.

---

## Bitwise Operations

All bitwise operations operate on 64-bit signed/unsigned integer values (`i64` / `u64`).

<a name="bit_anda-i64-b-i64---i64"></a>
### `bit_and(a: i64, b: i64) -> i64`

Performs bitwise AND (`a & b`).

**Example:**
```javascript
mask: i64 = bit_and(0x0F, 0xFF)
print(mask) // 15
```

---

<a name="bit_ora-i64-b-i64---i64"></a>
### `bit_or(a: i64, b: i64) -> i64`

Performs bitwise OR (`a | b`).

---

<a name="bit_xora-i64-b-i64---i64"></a>
### `bit_xor(a: i64, b: i64) -> i64`

Performs bitwise XOR (`a ^ b`).

---

<a name="bit_nota-i64---i64"></a>
### `bit_not(a: i64) -> i64`

Performs bitwise NOT (`~a`).

---

<a name="bit_shla-i64-b-i64---i64"></a>
### `bit_shl(a: i64, b: i64) -> i64`

Performs bitwise left shift (`a << b`).

---

<a name="bit_shra-i64-b-i64---i64"></a>
### `bit_shr(a: i64, b: i64) -> i64`

Performs bitwise right shift (`a >> b`).

---

<a name="popcounta-i64---i32"></a>
### `popcount(a: i64) -> i32`

Counts the number of set (1) bits in an integer (population count / Hamming weight).

**Example:**
```javascript
bits: i32 = popcount(7)
print(bits) // 3
```

---

## String Operations & Inspection

<a name="string-indexing-and-slicing"></a>
### String Indexing and Slicing

Strings in Mylo support 0-based indexing and slice ranges:

* `s[i]`: Retrieves a 1-character substring containing the byte at index `i`.
* `s[start:end]`: Retrieves the substring from `start` up to (exclusive) `end`.
* `len(s)`: Returns the total number of bytes in string `s`.

**Example:**
```javascript
text: string = "Hello, Mylo!"
first_char: string = text[0]        // "H"
sub: string = text[7:11]             // "Mylo"
length: i32 = len(text)             // 12
```

---

<a name="lencollection-any---i32"></a>
### `len(collection: any) -> i32`

Returns the number of elements in an array or the number of characters in a string.

**Arguments:**
* `collection`: The array or string.

**Returns:**
* Length as an integer.

---

<a name="ords-string---i32"></a>
### `ord(s: string) -> i32`

Returns the ASCII / byte code value of the first character of the string `s`.

**Arguments:**
* `s`: The input string.

**Returns:**
* ASCII numeric code (0 to 255), or 0 if string is empty.

**Example:**
```javascript
code: i32 = ord("A")
print(code) // 65
```

---

<a name="chrcode-i32---string"></a>
### `chr(code: i32) -> string`

Converts an ASCII / byte value (0 to 255) into a 1-character string.

**Example:**
```javascript
letter: string = chr(65)
print(letter) // "A"
```

---

<a name="splitsource-string-delimiter-string---string"></a>
### `split(source: string, delimiter: string) -> string[]`

Splits a string into an array of substrings based on a delimiter. If `delimiter` is empty `""`, splits into individual 1-character strings.

**Example:**
```javascript
s: string = "red,green,blue"
parts: string[] = split(s, ",")
print(parts[0]) // "red"
```

---

<a name="containshaystack-any-needle-any---i32"></a>
### `contains(haystack: any, needle: any) -> i32`

Checks if a value exists within an array or if a substring exists within a string.

**Returns:**
* `1` if found, `0` otherwise.

**Example:**
```javascript
fruits: string[] = ["apple", "banana", "cherry"]
if (contains(fruits, "banana")) {
    print("Found banana")
}
```

---

<a name="wherecollection-any-item-any---i32"></a>
### `where(collection: any, item: any) -> i32`

Finds the 0-based index of the first occurrence of an item within an array or string.

**Returns:**
* 0-based index if found, or `-1` if not found.

---

## Collections & Array Manipulation

<a name="listsize-i32---any"></a>
### `list(size: i32) -> any[]`

Creates a new array of a specific size, initialized with zero elements.

**Example:**
```javascript
buffer: i32[] = list(10)
print(len(buffer)) // 10
```

---

<a name="rangestart-f64-step-f64-stop-f64---f64"></a>
### `range(start: f64, step: f64, stop: f64) -> f64[]`

Generates an array of numbers from `start` to `stop` (inclusive), incrementing or decrementing by `step`.

**Example:**
```javascript
r: f64[] = range(0.0, 2.0, 10.0) // [0, 2, 4, 6, 8, 10]
```

---

<a name="addarray-any-index-i32-value-any---any"></a>
### `add(array: any[], index: i32, value: any) -> any[]`

Inserts a value into an array at a specific index, shifting subsequent elements to the right. Returns a **new** array.

**Example:**
```javascript
weights: i32[] = [10, 20]
weights = add(weights, 1, 15) // [10, 15, 20]
```

---

<a name="removecollection-any-key-any---void"></a>
### `remove(collection: any, key: any) -> void`

Removes an element in-place:
* **Array**: Removes element at the numeric index `key`.
* **Map**: Removes entry with string key `key`.

---

<a name="min_listlist-f64---f64"></a>
### `min_list(list: f64[]) -> f64`

Finds the smallest numeric value in a list.

---

<a name="max_listlist-f64---f64"></a>
### `max_list(list: f64[]) -> f64`

Finds the largest numeric value in a list.

---

<a name="for_listfunc_name-string-list-any---any"></a>
### `for_list(func_name: string, list: any[]) -> any[]`

Maps each element of `list` through the named function, returning a new array with the mapped results.

---

<a name="filterarray-any-func_name-string---any"></a>
### `filter(array: any[], func_name: string) -> any[]`

Filters elements in `array` by calling `func_name(item)` and retaining items where the predicate returns true.

---

<a name="param_filterarray-any-func_name-string-param-any---any"></a>
### `param_filter(array: any[], func_name: string, param: any) -> any[]`

Filters elements in `array` by calling `func_name(item, param)` with an extra parameter.

---

<a name="copyvalue-any---any"></a>
### `copy(value: any) -> any`

Creates a deep copy of an object, array, or map.

---

## Type Inspection & Conversion

<a name="typevalue-any---string"></a>
### `type(value: any) -> string`

Returns the runtime type name of a variable as a string (`"str"`, `"num"`, `"list"`, `"map"`, `"bytes"`, `"struct"`, etc.).

---

<a name="to_stringvalue-any---string"></a>
### `to_string(value: any) -> string`

Converts any value into its string representation.

**Example:**
```javascript
n: i32 = 123
s: string = to_string(n)
```

---

<a name="to_numvalue-any---f64"></a>
### `to_num(value: any) -> f64`

Converts a string representation of a number into an `f64`. Returns `0.0` on failure.

---

## Time & Timing

<a name="clock---f64"></a>
### `clock() -> f64`

Returns the current monotonic wall clock time in seconds with sub-millisecond precision (`clock_gettime(CLOCK_MONOTONIC)` on POSIX, `QueryPerformanceCounter` on Windows).

**Example:**
```javascript
t0: f64 = clock()
// perform computation
t1: f64 = clock()
elapsed: f64 = t1 - t0
print(f"Elapsed: {elapsed} seconds")
```

---

<a name="sleepms-i32---void"></a>
### `sleep(ms: i32) -> void`

Suspends execution of the current thread for the specified number of milliseconds.

**Example:**
```javascript
sleep(50) // Sleep 50ms
```

---

## File I/O (Text & Binary)

<a name="read_linespath-string---string"></a>
### `read_lines(path: string) -> string[]`

Reads a text file line-by-line and returns an array of strings.

**Example:**
```javascript
lines: string[] = read_lines("data.txt")
for (line in lines) {
    print(line)
}
```

---

<a name="write_filepath-string-content-string-mode-string---i32"></a>
### `write_file(path: string, content: string, mode: string) -> i32`

Writes text content to a file.
* `mode`: `"w"` to truncate/overwrite, or `"a"` to append.

**Returns:**
* `1` on success, `0` on failure.

**Example:**
```javascript
ok: i32 = write_file("out.txt", "Hello World\n", "w")
```

---

<a name="read_bytespath-string-stride-i32---i32"></a>
### `read_bytes(path: string, stride: i32) -> i32[]`

Reads a binary file into an array of integers.
* `stride = 1`: Reads individual 8-bit unsigned bytes (0–255).
* `stride = 4`: Reads 32-bit integers (Little-Endian).

---

<a name="write_bytespath-string-data-i32---i32"></a>
### `write_bytes(path: string, data: i32[]) -> i32`

Writes an array of integer values as raw bytes (each value clamped to 0–255) to the specified file.

---

<a name="list_dirpath-string-filter-string---string"></a>
### `list_dir(path: string, filter: string) -> string[]`

Lists filenames in the specified directory matching a file extension or suffix filter (e.g. `".mylo"` or `""` for all files).

---

## OS System & Process Management

<a name="systemcommand-string---string"></a>
### `system(command: string) -> string[]`

Executes a shell command synchronously and waits for completion.

**Returns:**
* A 2-element array `[stdout, stderr]`.

**Example:**
```javascript
res: string[] = system("echo hello")
print(res[0]) // "hello"
```

---

<a name="system_threadcommand-string-name-string---i32"></a>
### `system_thread(command: string, name: string) -> i32`

Starts a background OS process under a unique job name string.

---

<a name="get_jobname-string---any"></a>
### `get_job(name: string) -> any`

Polls the status of a background process started with `system_thread`:
* `1`: Still running.
* `-1`: Error or unknown job name.
* `string[]`: Two-element array `[stdout, stderr]` once finished.

---

## Terminal Input

<a name="cgetblocking-i32---string"></a>
### `cget(blocking: i32) -> string`

Reads a single character from terminal standard input.
* `blocking = 1`: Blocks until a key is pressed.
* `blocking = 0`: Non-blocking; returns `""` if no key is ready.

---

<a name="kbhit---i32"></a>
### `kbhit() -> i32`

Returns `1` if a key has been pressed and is waiting in the input buffer, or `0` if empty.

---

<a name="get_keys---string"></a>
### `get_keys() -> string[]`

Reads and flushes all pending terminal input events, returning an array of string key representations (including control keys like `"[UP]"`, `"[DOWN]"`, `"[ENTER]"`).

---

## Multithreading & Universal Bus

Mylo provides true multithreading with isolated background worker virtual machines and a thread-safe Universal Bus for message passing.

<a name="create_workerfunction_name-string---i32"></a>
### `create_worker(function_name: string) -> i32`

Spawns a new background thread executing `function_name`. The function must be declared globally.

**Returns:**
* Worker ID (`0` to `MAX_WORKERS - 1`) on success, or `-1` if no slots are available.

---

<a name="check_workerworker_id-i32---i32"></a>
### `check_worker(worker_id: i32) -> i32`

Non-blocking check of a worker's completion status.
* `0`: Worker is actively running.
* `1`: Worker has completed.
* `-1`: Invalid worker ID.

---

<a name="dock_workerworker_id-i32---void"></a>
### `dock_worker(worker_id: i32) -> void`

Blocks the calling thread until the specified worker has finished execution and joins its thread.

---

<a name="bus_setkey-string-value-any---i32"></a>
### `bus_set(key: string, value: any) -> i32`

Sets a key-value pair in the global thread-safe Universal Bus. Overwrites existing keys.

**Example:**
```javascript
bus_set("task_status", "in_progress")
bus_set("row_0", 12345)
```

---

<a name="bus_getkey-string---any"></a>
### `bus_get(key: string) -> any`

Retrieves a value from the Universal Bus by key. Returns `0` if the key is not found.

**Example:**
```javascript
val: string = bus_get("task_status")
```

#### Complete Multithreading Example

```javascript
fn compute_worker() {
    start_row: i32 = bus_get("start")
    end_row: i32 = bus_get("end")
    sum: i64 = 0
    for (i in start_row ... end_row) {
        sum = sum + i
    }
    bus_set("result", sum)
}

bus_set("start", 1)
bus_set("end", 1000)
worker: i32 = create_worker("compute_worker")

dock_worker(worker)
total: i64 = bus_get("result")
print(f"Total: {total}")
```

---

## Web Visual Library

Mylo includes a built-in HTTP server and canvas visualizer (`http://localhost:8080`) for 2D graphics and dashboards.

<a name="web_init---void"></a>
### `web_init() -> void`

Initializes the background TCP web server on port 8080 and pauses execution until a web browser connects to `http://localhost:8080`.

---

<a name="web_statuswait-i32---i32"></a>
### `web_status(wait: i32) -> i32`

Checks browser connection status.
* `wait = 0`: Non-blocking check. Returns `1` if connected, `0` otherwise.
* `wait = 1`: Blocks until a browser connects.

---

<a name="web_clr---void"></a>
### `web_clr() -> void`

Clears the web canvas.

---

<a name="web_rectx-f64-y-f64-w-f64-h-f64-color-string---void"></a>
### `web_rect(x: f64, y: f64, w: f64, h: f64, color: string) -> void`

Draws a filled rectangle on the web canvas. Supports named colors (`"red"`), hex codes (`"#112233"`), and RGBA strings (`"rgba(255, 255, 255, 0.5)"`).

---

<a name="web_circlex-f64-y-f64-r-f64-color-string---void"></a>
### `web_circle(x: f64, y: f64, r: f64, color: string) -> void`

Draws a filled circle with center `(x, y)` and radius `r`.

---

<a name="web_linex1-f64-y1-f64-x2-f64-y2-f64-thickness-f64-color-string---void"></a>
### `web_line(x1: f64, y1: f64, x2: f64, y2: f64, thickness: f64, color: string) -> void`

Draws a line between `(x1, y1)` and `(x2, y2)`.

---

<a name="web_trianglex1-f64-y1-f64-x2-f64-y2-f64-x3-f64-y3-f64-color-string---void"></a>
### `web_triangle(x1: f64, y1: f64, x2: f64, y2: f64, x3: f64, y3: f64, color: string) -> void`

Draws a filled triangle with vertices `(x1, y1)`, `(x2, y2)`, and `(x3, y3)`.

---

<a name="web_texttext-string-x-f64-y-f64-size-f64---void"></a>
### `web_text(text: string, x: f64, y: f64, size: f64) -> void`

Renders text at `(x, y)` with font size `size` in pixels.

---

## Metaprogramming

<a name="callname-string-args-any---any"></a>
### `call(name: string, args: any[]) -> any`

Invokes a function dynamically by its string identifier with an array of arguments.

**Example:**
```javascript
fn greet(msg: string) {
    print(f"Message: {msg}")
}

call("greet", ["Hello from dynamic call!"])
```
