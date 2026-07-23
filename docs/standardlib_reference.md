<!-- TOC --><a name="mylo-standard-library-documentation"></a>
# Mylo Standard Library Documentation

The Mylo Standard Library provides essential functions for file I/O, math, and data manipulation. These functions are built-in and available directly in the interpreter.

<!-- TOC start (generated with https://github.com/derlin/bitdowntoc) -->

- [Mylo Standard Library Documentation](#mylo-standard-library-documentation)
  * [Math Functions](#math-functions)
    + [`sqrt(value: num) -> num`](#sqrtvalue-num-num)
    + [`sin(value: num) -> num`](#sinangle-num---num)
    + [`cos(value: num) -> num`](#cosangle-num---num) 
    + [`tan(value: num) -> num`](#tanangle-num---num)
    + [`floor(value: num) -> num`](#floorvalue-num---num)
    + [`ceil(value: num) -> num`](#ceilvalue-num---num)
    + [`mix(start: num, stop: num, weight: num) -> num`](#mixstart-num-stop-num-weight-num---num)
    + [`min(val1: num, val2:num) -> num`](#minval1-num-val2-num---num)
    + [`max(val1: num, val2:num) -> num`](#maxval1-num-val2-num---num)
    + [`distance(x1: num, y1: num, x2: num, y2: num) -> num`](#distancex1-num-y1-num-x2-num-y2-num---num)
    + [`seed() -> None (Random Number Functions)`](#random_numbers)
    + [` noise(x: num, y: num, z: num) -> num`](#noisex-num-y-num-z-num---num)
  * [Utility Functions](#utility-functions)
    + [`type(value: any) -> str`](#type-any)
    + [`len(collection: any) -> num`](#lencollection-any-num)
    + [`contains(haystack: any, needle: any) -> num`](#containshaystack-any-needle-any-num)
    + [`list(size: num) -> arr`](#listsize-num---arr)
    + [`add(array: arr, index: num, value: any) -> arr`](#addarray-arr-index-num-value-any---arr)
    + [`remove(collection: any, key: any) -> obj`](#removecollection-any-key-any---obj)
    + [`where(collection: any, item: any) -> num`](#wherecollectionany-itemany-num)
    + [`filter(array: arr, func_name: str) -> arr`](#filter-arr)
    + [`param_filter(array: arr, func_name: str, param: any) -> arr`](#param-filter)
    + [`range(start: num, step: num, stop: num) -> arr`](#array-range)
    + [`split(source: str, delimiter: str) -> arr`](#splitsourcestr-delimiterstr-arr)
    + [`for_list(func_name: str, list: arr) -> arr`](#for-list-arr)
    + [`min_list(list: arr) -> num`](#min_listlist-arr---num)
    + [`max_list(list: arr) -> num`](#max_listlist-arr---num)
  * [Type Conversion](#type-conversion)
    + [`to_string(value: any) -> str`](#to_string)
    + [`to_num(value: any) -> num`](#to_num)
  * [File I/O (Text)](#file-io-text)
    + [`read_lines(path: str) -> arr`](#read_linespath-str-arr)
    + [`write_file(path: str, content: str, mode: str) -> num`](#write_filepath-str-content-str-mode-str-num)
  * [File I/O (Binary)](#file-io-binary)
    + [`read_bytes(path: str, stride: num) -> arr`](#read_bytespath-str-stride-num-arr)
    + [`write_bytes(path: str, data: arr) -> num`](#write_bytespath-str-data-arr-num)
  * [OS System & Process Management](#systemcommand-str---arr)
  * [Terminal Input](#terminal-input)
    + [`get_keys() -> arr`](#get_keys---arr)
    + [`kbhit() -> num`](#kbhit---num)
    + [`cget(blocking: num) -> str`](#cgetblocking-num---str)
  * [Multithreading](#multithreading)
    + [`create_worker(region: region, function_name: str) -> num`](#create_workerregion-region-function_name-str---num)
    + [`check_worker(worker_id: num) -> num`](#check_workerworker_id-num---num)
    + [`dock_worker(worker_id: num) -> void`](#dock_workerworker_id-num---void)
  * [Universal Bus](#universal-bus)
    + [`bus_get(key: str) -> any`](#bus_getkey-str---any)
    + [`bus_set(key: str, value: any) -> any`](#bus_setkey-str-value-any---num)
  * [Web Visual Library](#mylo-web-visual-library)
    + [`web_init() -> any`](#web_init---any)
    + [`web_status(wait: num) -> num`](#web_statuswait-num---num)
    + [`web_clr() -> any`](#web_clr---any)
  * [Web Primitives](#drawing-primitives)
    + [`web_rect(x: num, y: num, w: num, h: num, color: str) -> any`](#web_rectx-num-y-num-w-num-h-num-color-str---any)
    + [`web_circle(x: num, y: num, r: num, color: str) -> any`](#web_circlex-num-y-num-r-num-color-str---any)
    + [`web_line(x1: num, y1: num, x2: num, y2: num, thickness: num, color: str) -> any`](#web_linex1-num-y1-num-x2-num-y2-num-thickness-num-color-str---any)
    + [`web_text(text: str, x: num, y: num, size: num) -> any`](#web_texttext-str-x-num-y-num-size-num---any)
  * [Metaprogramming and Function Calls](#)


<!-- TOC end -->

<!-- TOC --><a name="math-functions"></a>
## Math Functions

<!-- TOC --><a name="sqrtvalue-num-num"></a>
### `sqrt(value: num) -> num`

Calculates the square root of a number.

**Arguments:**
* `value`: The number to calculate the square root of.

**Returns:**
* The square root as a number.

**Example:**
```javascript
var result = sqrt(16)
print(result) // 4
```

<a name="sinangle-num-num"></a>
### `sin(angle: num) -> num`

Calculates the sine of an angle (in radians).

**Arguments:**
* `angle`: The angle in radians.

**Returns:**
* The sine of the angle.

<a name="cosangle-num-num"></a>
### `cos(angle: num) -> num`

Calculates the cosine of an angle (in radians).

**Arguments:**
* `angle`: The angle in radians.

**Returns:**
* The cosine of the angle.

<a name="tanangle-num-num"></a>
### `tan(angle: num) -> num`

Calculates the tangent of an angle (in radians).

**Arguments:**
* `angle`: The angle in radians.

**Returns:**
* The tangent of the angle.

<a name="floorvalue-num-num"></a>
### `floor(value: num) -> num`

Rounds a number down to the nearest integer.

**Arguments:**
* `value`: The number to round.

**Returns:**
* The largest integer less than or equal to the given number.

<a name="ceilvalue-num-num"></a>
### `ceil(value: num) -> num`

Rounds a number up to the nearest integer.

**Arguments:**
* `value`: The number to round.

**Returns:**
* The smallest integer greater than or equal to the given number.

<a name="mixstart-stop-weight-num"></a>
### `mix(start: num, stop: num, weight: num) -> num`

Linearly interpolates between two values based on a weight.

**Arguments:**
* `start`: The starting value (returned when weight is 0).
* `stop`: The ending value (returned when weight is 1).
* `weight`: The interpolation factor (usually between 0.0 and 1.0).

**Returns:**
* The interpolated number.

<a name="minval1-val2-num"></a>
### `min(val1: num, val2: num) -> num`

Returns the smaller of two numbers.

**Arguments:**
* `val1`: The first number.
* `val2`: The second number.

**Returns:**
* The minimum value.

<a name="maxval1-val2-num"></a>
### `max(val1: num, val2: num) -> num`

Returns the larger of two numbers.

**Arguments:**
* `val1`: The first number.
* `val2`: The second number.

**Returns:**
* The maximum value.

<a name="distancex1-y1-x2-y2-num"></a>
### `distance(x1: num, y1: num, x2: num, y2: num) -> num`

Calculates the Euclidean distance between two 2D points.

**Arguments:**
* `x1`: The X coordinate of the first point.
* `y1`: The Y coordinate of the first point.
* `x2`: The X coordinate of the second point.
* `y2`: The Y coordinate of the second point.

**Returns:**
* The distance between the points.


<!-- TOC --><a name="random_numbers"></a>
### `seed(val: num) -> None`
Initializes the random number generator with a specific seed value.

### `rand() -> num`
Generates a uniform random number between `0.0` and `1.0` (inclusive).

**Example:**
```javascript
seed(1234)
var r = rand() // e.g. 0.832...
```

### `rand_normal() -> num`
Generates a random number following a Standard Normal Distribution (Gaussian) with a Mean of `0.0` and a Standard Deviation of `1.0`.

**Behavior:**
* **Distribution:** Uses the Box-Muller transform.
* **Range:** While theoretically unbounded, ~68% of values fall between `-1.0` and `1.0`, and ~99.7% fall between `-3.0` and `3.0`.

**Example:**
```javascript
// Generate a value, likely close to 0
var n = rand_normal() 
```

<a name="noisex-y-z-num"></a>
### `noise(x: num, y: num, z: num) -> num`

Calculates 3D Perlin Noise for the given coordinates.

**Arguments:**
* `x`: The X coordinate.
* `y`: The Y coordinate.
* `z`: The Z coordinate (use `0.0` for 2D noise).

**Returns:**
* A smooth random number roughly between `-1.0` and `1.0`.

**Example:**
```javascript
// 2D Noise sample
var val = noise(x * 0.1, y * 0.1, 0)

// Animated Noise
var t = 0
// Loop...
var val = noise(x * 0.1, y * 0.1, t)
t = t + 0.01
```

<!-- TOC --><a name="utility-functions"></a>
## Utility Functions

<!-- TOC --><a name="type-any"></a>
### `type(x: any) -> str`

Returns the string describing the type

**Arguments:**
* `x`: The variable to be checked

**Returns:**
* Type as string: num, str, name (if enum), list, map, bytes, i32[], f32[], i16[], i64[], bool[], struct, null

**Example:**
```javascript
var x = "Hi"
print(type(x)) // "str"

var s = 99
print(type(s)) // "num"
```

<!-- TOC --><a name="lencollection-any-num"></a>
### `len(collection: any) -> num`

Returns the number of elements in an array or the number of characters in a string.

**Arguments:**
* `collection`: The array or string to check.

**Returns:**
* The length as a number.

**Example:**
```javascript
var list = ["A", "B", "C"]
print(len(list)) // 3

var s = "Hello"
print(len(s)) // 5
```

<!-- TOC --><a name="containshaystack-any-needle-any-num"></a>
### `contains(haystack: any, needle: any) -> num`

Checks if a value exists within a collection.

**Arguments:**
* `haystack`: The collection (Array or String) to search in.
* `needle`: The value to search for.
  * If `haystack` is an **Array**, `needle` can be any type.
  * If `haystack` is a **String**, `needle` must be a substring.

**Returns:**
* `1` if found, `0` if not found.

**Example:**
```javascript
// Array Check
var list = ["apple", "banana", "cherry"]
if (contains(list, "banana")) {
    print("Found banana!")
}

// String Check
if (contains("Teamwork", "work")) {
    print("Found substring!")
}
```

<a name="splitsourcestr-delimiterstr-arr"></a>
### `split(source: str, delimiter: str) -> arr`

Splits a string into an array of substrings based on a delimiter.

**Arguments:**
* `source`: The string to split.
* `delimiter`: The string pattern to split by. If empty `""`, splits into individual characters.

**Returns:**
* An array of strings.

**Example:**
```javascript
var s = "a,b,c"
var parts = split(s, ",")
print(parts[0]) // "a"
print(parts[1]) // "b"

var word = "cat"
for (x in split(word, "")) {
	print(x)
}
// ["c", "a", "t"]
```


<a name="wherecollectionany-itemany-num"></a>
### `where(collection: any, item: any) -> num`

Finds the index of the first occurrence of an item within a collection.

**Arguments:**
* `collection`: The string or array to search.
* `item`: The value to search for.

**Returns:**
* The index (0-based) of the found item, or `-1` if not found.

**Example:**
```javascript
// Arrays
var list = ["cat", "dog"]
print(where(list, "dog")) // 1
print(where(list, "fish")) // -1

// Strings
var text = "ohblast"
print(where(text, "blast")) // 2
```
<a name="filter-arr"></a>
### `filter(array: arr, func_name: str) -> arr`

Applies a function with name func_name to the array, and returns a list
or elements where the func evaluated to true. See example

**Arguments:**
* `array`: The array to filter
* `func_name`: The function to apply

**Returns:**
* arr, the filtered array

**Example:**
```javascript
var L = [1, 2, 3]
fn filt(x) { ret x > 1 }
var o = filter(L, "filt")
print(o) // [2, 3]
```
<a name="param-filter"></a>
### `param_filter(array: arr, func_name: str, param : any) -> arr`

Applies a function with name func_name to the array, and returns a list
or elements where the func evaluated to true. Allows an additional parameter

**Arguments:**
* `array`: The array to filter
* `func_name`: The function to apply
* `param` : The additional filter parameter

**Returns:**
* arr, the filtered array

**Example:**
```javascript
var x = [{"ID"=5, "VAL"=9}, {"ID"=9, "VAL"=5}]
fn filt(x, y) {ret x["ID"] == y}
var o = param_filter(x, "filt", 5)
// {"ID"=5, "VAL"=9}
```


<a name="list_minlist-arr-num"></a>
### `min_list(list: arr) -> num`

Finds the smallest numeric value within a list.

**Arguments:**
* `list`: The array to search. Must not be empty.

**Returns:**
* The minimum number found in the list.

<a name="list_maxlist-arr-num"></a>
### `max_list(list: arr) -> num`

Finds the largest numeric value within a list.

**Arguments:**
* `list`: The array to search. Must not be empty.

**Returns:**
* The maximum number found in the list.

**Example:**
```javascript
var nums = [10, 5, 20, 2]
print(min_list(nums)) // 2
print(max_list(nums)) // 20
```

<a name="listsize-num-arr"></a>
### `list(size: num) -> arr`

Creates a new array of a specific size, initialized with zeros.

**Arguments:**
* `size`: The number of elements to allocate.

**Returns:**
* A new array containing `size` zeros.

**Example:**
```javascript
var buffer = list(10)
print(len(buffer)) // 10
print(buffer[0])   // 0
```
<a name="array-range"></a>
### `range(start: num, step: num, stop: num) -> arr`

Generates an array of numbers from `start` to `stop` (inclusive), incrementing by `step`.

**Arguments:**
* `start`: The starting number of the sequence.
* `step`: The amount to increment (or decrement) by in each step. Must not be 0.
* `stop`: The final number of the sequence.

**Behavior:**
* **Inclusive:** The returned array includes the `stop` value if the step lands exactly on it.
* **Auto-Direction:** The function automatically determines whether to increment or decrement based on `start` and `stop`. The sign of `step` is ignored (the absolute value is used).
    * If `start < stop`: Values increase.
    * If `start > stop`: Values decrease.
* **Floats:** Supports floating point numbers.

**Examples:**

```javascript
// Basic Integer Range
var r1 = range(0, 2, 10)
// Result: [0, 2, 4, 6, 8, 10]

// Floating Point Range
var r2 = range(0, 0.5, 1.5)
// Result: [0.0, 0.5, 1.0, 1.5]

// Reverse Range
var r3 = range(10, 2, 0)
// Result: [10, 8, 6, 4, 2, 0]

// Decimal Reverse
var r4 = range(1, 0.1, 0)
// Result: [1.0, 0.9, 0.8, ... 0.0]
```

<a name="addarray-arr-index-num-value-any-arr"></a>
### `add(array: arr, index: num, value: any) -> arr`

Inserts a value into an array at a specific index, shifting subsequent elements to the right.

**Important:** Arrays in Mylo are fixed-size in memory. This function creates and returns a **new** array. You must assign the result back to your variable to see the change.

**Arguments:**
* `array`: The source array.
* `index`: The position to insert the value (0 to len).
* `value`: The value to insert.

**Returns:**
* A **new** array with the element added.

**Example:**
```javascript
var weights = [10, 20]
// Insert 15 at index 1
weights = add(weights, 1, 15) 
print(weights) // [10, 15, 20]
```

<a name="removecollection-any-key-any-obj"></a>
### `remove(collection: any, key: any) -> obj`

Removes an element from a collection **in-place**.

**Arguments:**
* `collection`: The array or map to modify.
* `key`: The identifier for the item to remove.
  * **Array**: The numeric **index** of the item. Elements after this index are shifted left.
  * **Map**: The **key** (string) of the entry to remove.

**Returns:**
* Returns the modified collection to support chaining (e.g., `remove(arr, 0)`).

**Example:**
```javascript
// Array Removal
var items = ["A", "B", "C"]
remove(items, 1) // Remove index 1 ("B")
print(items) // ["A", "C"]

// Map Removal
var data = {"name"="Mylo", "ver"=1}
remove(data, "ver")
print(data) // {"name"="Mylo"}
```

<a name="for-list-arr"></a>
### `for_list(func_name: str, list: arr) -> arr`

Creates a new array by applying a specified function to every element in the provided list. This is similar to a `map` function in other languages.

**Arguments:**
* `func_name`: The name of the function to execute for each item. This can be the name of a standard library function (e.g., `"sqrt"`) or a user-defined function.
* `list`: The array containing the elements to process.

**Behavior:**
* The function specified by `func_name` must accept exactly one argument (the current element) and return a value.
* Returns a **new** array of the same length as the input `list`, containing the results.
* Supports both native functions and user-defined functions.

**Examples:**

```javascript
// Example 1: Using a user-defined math function
fn double(x) { 
    ret x * 2 
}

var numbers = range(0, 1, 3) // [0, 1, 2, 3]
var doubled = for_list("double", numbers)
print(doubled)
// Result: [0, 2, 4, 6]


// Example 2: Using a user-defined string function
fn add_fish(x) { 
    ret f"{x}fish" 
}

var animals = ["cat", "dog"]
var aquatic = for_list("add_fish", animals)
print(aquatic)
// Result: ["catfish", "dogfish"]
```

<!-- TOC --><a name="type-conversion"></a>
## Type Conversion
<!-- TOC --><a name="to_string"></a>
### `to_string(value: any) -> str`

Converts a value to its string representation.

**Arguments:**
* `value`: The value to convert.

**Returns:**
* The string representation of the value.

**Example:**
```javascript
var n = 123
var s = to_string(n) 
print(f"{s}4") // "1234"
```
<!-- TOC --><a name="to_num"></a>
### `to_num(value: any) -> num`

Converts a value (usually a string) to a number.

**Arguments:**
* `value`: The string to convert.

**Returns:**
* The numeric value. Returns `0` if the conversion fails.

**Example:**
```javascript
var s = "45.5"
var n = to_num(s)
print(n + 10) // 55.5
```

<!-- TOC --><a name="file-io-text"></a>
## File I/O (Text)

<!-- TOC --><a name="read_linespath-str-arr"></a>
### `read_lines(path: str) -> arr`

Reads a text file and returns its content as an array of strings, where each element represents one line of the file.

**Arguments:**
* `path`: The path to the file.

**Returns:**
* An array of strings. Returns an empty array if the file cannot be read.

**Example:**
```javascript
var lines = read_lines("data.txt")
for (line in lines) {
    print(line)
}
```

<!-- TOC --><a name="write_filepath-str-content-str-mode-str-num"></a>
### `write_file(path: str, content: str, mode: str) -> num`

Writes a string to a file. Supports overwriting or appending.

**Arguments:**
* `path`: The path to the file.
* `content`: The string data to write.
* `mode`: The file mode.
  * `"w"`: Write (Overwrite). Creates the file if it doesn't exist, or truncates it if it does.
  * `"a"`: Append. Writes data to the end of the file.

**Returns:**
* `1` on success, `0` on failure.

**Example:**
```javascript
// Create/Overwrite file
write_file("log.txt", "Log Started\n", "w")

// Append to file
write_file("log.txt", "New Entry\n", "a")
```

<!-- TOC --><a name="file-io-binary"></a>
## File I/O (Binary)

<!-- TOC --><a name="read_bytespath-str-stride-num-arr"></a>
### `read_bytes(path: str, stride: num) -> arr`

Reads a file in binary mode and returns the data as an array of numbers.

**Arguments:**
* `path`: The path to the file.
* `stride`: The size of each element to read.
  * `1`: Read as individual bytes (0-255).
  * `4`: Read as 32-bit Integers (Little Endian).

**Returns:**
* An array of numbers representing the file content.

**Example:**
```javascript
// Read as raw bytes
var bytes = read_bytes("image.png", 1)
print(bytes[0]) // First byte (e.g., 137 for PNG)

// Read as 32-bit integers
var ints = read_bytes("data.bin", 4)
print(ints[0])
```

<!-- TOC --><a name="write_bytespath-str-data-arr-num"></a>
### `write_bytes(path: str, data: arr) -> num`

Writes an array of numbers to a file as raw bytes.

**Arguments:**
* `path`: The path to the file.
* `data`: An array of numbers. Each number is cast to a byte (0-255) before writing.

**Returns:**
* `1` on success, `0` on failure.

**Example:**
```javascript
// Write ASCII bytes for "ABC\n"
var data = [65, 66, 67, 10]
write_bytes("output.bin", data)
```

## System & Process Management

<a name="systemcommand-str-arr"></a>
### `system(command: str) -> arr`

Executes a shell command and waits for it to complete, capturing both standard output and standard error.

**Arguments:**
* `command`: The full command string to execute (e.g., `"ls -la"` or `"dir"`).

**Returns:**
* An array containing two strings: `[stdout, stderr]`.

**Example:**
```javascript
var output = system("echo Hello Mylo")
print(output[0]) // "Hello Mylo"
```

<a name="system_threadcommand-str-name-str-num"></a>
### `system_thread(command: str, name: str) -> num`

Starts a shell command in a background thread. The result must be retrieved later using `get_job`.

**Arguments:**
* `command`: The command string to execute.
* `name`: A unique identifier string to associate with this background job.

**Returns:**
* `1` if the thread started successfully, `0` otherwise.

<a name="get_jobname-str-any"></a>
### `get_job(name: str) -> any`

Polls the status of a background job started by `system_thread`.

**Arguments:**
* `name`: The identifier used when the job was created.

**Returns:**
* `1`: The job is still running.
* `-1`: The job encountered an error or the name does not exist.
* `arr`: A two-element array `[stdout, stderr]` if the job has finished.

**Example:**
```javascript
system_thread("sleep 2 && echo Done", "my_job")

forever {
    var res = get_job("my_job")
    if (res == 1) {
        print("Working...")
    } else {
        print(f"Result: {res[0]}")
        break
    }
}
```

<a name="terminal-input"></a>
## Terminal Input

These functions allow for real-time interaction with the terminal, enabling TUI applications, games, and interactive tools.

<a name="cgetblocking-num-str"></a>
### `cget(blocking: num) -> str`

Reads a single character from the standard input.

**Arguments:**
* `blocking`:
  * `1`: Blocks execution until a key is pressed.
  * `0`: Non-blocking. Returns an empty string `""` immediately if no key is pressed.

**Returns:**
* A string containing the character code (e.g., "a", "1").
* Returns `""` if non-blocking and no input is available.

**Example:**
```javascript
// Wait for user to press a key
var key = cget(1)
print("You pressed: " + key)
```

<a name="kbhit-num"></a>
### `kbhit() -> num`

Checks if a key has been pressed and is waiting in the buffer.

**Returns:**
* `1`: A key is waiting to be read.
* `0`: No keys are pressed.

**Example:**
```javascript
if (kbhit()) {
    print("Key pressed!")
}
```

<a name="get_keys-arr"></a>
### `get_keys() -> arr`

Reads and clears the entire input buffer, returning a list of all key events that occurred since the last check.

**Returns:**
* An array of strings representing the keys pressed.
* Special keys are returned as tags: `[UP]`, `[DOWN]`, `[LEFT]`, `[RIGHT]`, `[ESC]`, `[ENTER]`, `[BACKSPACE]`, `[TAB]`, `[HOME]`, `[END]`, `[INS]`, `[DEL]`.
* Function keys are returned as `[F1]` to `[F10]`.
* Control combinations are returned as `[CTRL+A]` ... `[CTRL+Z]`.
* Alt combinations (Linux only) are returned as `[ALT+X]`.

**Example:**
```javascript
var inputs = get_keys()

// Process all inputs for this frame
for (k in inputs) {
    if (k == "[UP]") {
        player_y = player_y - 1
    }
    if (k == "q") {
        running = 0
    }
}
```

<a name="multithreading"></a>
## Threading (Workers)

> [!WARNING]
> **DEPRECATED**: The worker and threading model in Mylo was previously based on Region memory ownership transfer. With the introduction of the new Mark-and-Sweep Garbage Collector, the isolated region-based threading model has been deprecated. Workers and the Universal Bus are currently non-functional and will be rewritten in a future release to support concurrent GC.

The documentation below describes the **legacy** behavior of Mylo workers.

In Mylo, true multi-threading was achieved using **Workers**. A worker is a completely isolated VM instance running on a background OS thread. Because they share nothing by default, there are no race conditions or need for locks on standard variables.

To share data between the main thread and a worker, you transferred ownership of a memory region.

<a name="create_workerregion-num-function-str-num"></a>
### `create_worker(region: region, function_name: str) -> num`

Spawns a new thread to execute a specific function. Ownership of the provided memory region is transferred to the worker thread.

**Arguments:**
* `region`: The memory region identifier (e.g., created via `region my_mem`).
* `function_name`: The name of the function to execute in the new thread.

**Returns:**
* A numeric `worker_id` (0 or greater) if successful.
* `-1` if the worker could not be created (e.g., max threads reached).

**Runtime Safety:**
* Attempting to access variables within the passed `region` from the main thread *after* calling this function (but *before* docking) will result in a Runtime Error.

<a name="check_workerworker_id-num-num"></a>
### `check_worker(worker_id: num) -> num`

Non-blocking check of a worker's execution status.

**Arguments:**
* `worker_id`: The ID returned by `create_worker`.

**Returns:**
* `0`: The worker is still running.
* `1`: The worker has completed execution.
* `-1`: Invalid worker ID or the worker is not active.


<a name="dock_workerworker_id-num-void"></a>
### `dock_worker(worker_id: num) -> void`

Blocks the calling thread until the specified worker has finished execution. Once the worker joins, ownership of its memory region is returned to the main thread.

**Arguments:**
* `worker_id`: The ID returned by `create_worker`.

**Behavior:**
* If the worker is still running, this function waits (blocks).
* Once returned, variables in the region passed to `create_worker` become accessible again in the main thread.

#### Example
```javascript
// --- Threading Test Program ---

// Create a region for the job
region job_mem

// 1. Declare 'result' globally so Main can see it.
// 2. Use an Array '[0]' so the data lives in the Region (Heap), not just the stack.
var job_mem::result = [0]

// 1. Define the function that will run in the thread
fn worker_task() {
    print("  [Worker] Thread started. Processing...")
    
    // Simulate heavy work
    var total = 0
    for (var i in 0...5000) {
        total = total + 1
    }
    
    // [FIX] 3. Update the value inside the array
    job_mem::result[0] = total
    
    print("  [Worker] Done! Result stored.")
}

// 2. Main Program
print("[Main] Initializing...")

// Initialize some data in that region
// (Primitives like '100' are fine for read-only input because they are copied)
var job_mem::input = 100

print("[Main] Spawning worker...")
var worker_id = create_worker(job_mem, "worker_task")

if (worker_id < 0) {
    print("[Main] Failed to create worker!")
} else {
    print(f"[Main] Worker running (ID: {to_string(worker_id)})")
    
    var running = 1
    forever {
        if (running != 1) {
            break 
        }
        var status = check_worker(worker_id)
        
        // !!! WARNING !!!
        // If we try to check job_mem here, we will get an 
        // access violation!
        // print(job_mem::result[0]) 
        
        if (status == 1) {
            print("[Main] Worker reported complete.")
            running = 0
        }
    }
    
    print("[Main] Docking worker...")
    dock_worker(worker_id)
    
    print("[Main] Retrieving result from worker memory...")
    
    // 4. Read the value from the array
    var res = job_mem::result[0]
    print(f"[Main] Calculation Result: {to_string(res)}")
    
    if (res == 5000) {
        print("[Main] SUCCESS: Threading working correctly.")
    } else {
        print("[Main] FAILURE: Result incorrect.")
    }
    
    clear(job_mem)
```

<a name="universal-bus"></a>
## Universal Bus

The Universal Bus is a thread-safe, global key-value store that allows all VM instances (threads) to communicate and share simple data. It uses a global lock, making it safe for concurrent access but potentially slower than region-based memory sharing.

It is ideal for signaling state changes, broadcasting status updates, or sharing configuration strings between workers.

<a name="bus_setkey-str-value-any-num"></a>
### `bus_set(key: str, value: any) -> num`

Stores a value in the global bus under the specified key. If the key already exists, its value is overwritten.

**Arguments:**
* `key`: A unique string identifier for the data.
* `value`: The data to store.
  * Supported types: Numbers (`num`) and Strings (`str`).
  * **Note:** Passing Objects (Arrays/Maps) is not fully supported; they may be converted to a placeholder string.

**Returns:**
* `1` if the operation was successful.
* `0` if the bus is full or an error occurred.

**Example:**
```javascript
// Main thread sets a config
bus_set("difficulty", 2)
bus_set("server_msg", "Ready")
```

<a name="bus_getkey-str-any"></a>
### `bus_get(key: str) -> any`

Retrieves a value from the global bus.

**Arguments:**
* `key`: The identifier of the data to retrieve.

**Returns:**
* The stored value (Number or String).
* Returns `0` if the key is not found.

**Example:**
```javascript
// Worker thread reads the config
var diff = bus_get("difficulty")

if (diff > 1) {
    print("Hard Mode Active")
}
```

# Mylo Web Visual Library

The Web Library provides a high-fidelity alternative to standard terminal output. It enables hardware-accelerated 2D graphics, anti-aliased primitives, and transparent UI overlays using standard web technologies. 

### **Getting Started**
1.  Call `web_init()` at the start of your script. 
2.  Open your browser to `http://localhost:8080`. 
3.  The script will pause until the browser connects, then begin streaming. 

---

## **Core Lifecycle**

### `web_init() -> any`
Initializes the background TCP server and sets up the internal EventSource stream. 
* **Behavior:** Blocks execution until a browser successfully connects to the display URL. 
* **URL:** `http://localhost:8080` 

### `web_status(wait: num) -> num`
Checks the connection status of the browser display. 
* **Arguments:** * `wait`: If `0`, performs a non-blocking check. 
  * If `1`, blocks execution until a new browser connection is established (useful for handling window refreshes). 
* **Returns:** `1` if a browser is currently connected, `0` otherwise. 

### `web_clr() -> any`
Clears the entire canvas, resetting it to a transparent state.  Use this at the start of every frame in a `forever` loop to prevent "trails" from moving objects. 

---

## **Drawing Primitives**

All drawing functions accept a `color` string as their final argument.  The browser engine supports:
* **Named Colors:** `"orange"`, `"cyan"`, `"magenta"` 
* **Hex Codes:** `"#FF5500"`, `"#00CCFF"` 
* **RGBA:** `"rgba(255, 0, 0, 0.5)"` (Supports transparency) 

### `web_rect(x: num, y: num, w: num, h: num, color: str) -> any`
Draws a filled rectangle at the specified coordinates. 
* **x, y**: The top-left corner of the rectangle. 
* **w, h**: The width and height in pixels. 

### `web_circle(x: num, y: num, r: num, color: str) -> any`
Draws a filled, anti-aliased circle. 
* **x, y**: The center point of the circle. 
* **r**: The radius in pixels. 

### `web_line(x1: num, y1: num, x2: num, y2: num, thickness: num, color: str) -> any`
Draws a line between two points. 
* **x1, y1**: Starting coordinates. 
* **x2, y2**: Ending coordinates. 
* **thickness**: The width of the line in pixels. 

### `web_triangle(x1: num, y1: num, x2: num, y2: num, x3: num, y3: num, color: str) -> any`
Draws a filled triangle using three sets of coordinates.
* **x1, y1 / x2, y2 / x3, y3**: The three vertices of the triangle.
* **color**: Standard color string (name, hex, or rgba).


---

## **Text & UI**

### `web_text(text: str, x: num, y: num, size: num) -> any`
Renders high-fidelity text using the browser's default monospace font. 
* **text**: The string to display. 
* **x, y**: The baseline starting coordinates. 
* **size**: The font size in pixels (e.g., `24`). 

---

## **Example Usage**

```javascript
// Initialize the display
web_init()

forever {
    // Re-connect if the browser window was closed/refreshed
    if (web_status(0) == 0) {
        print("Waiting for browser...")
        web_status(1) 
    }

    // Start a new frame
    web_clr()
    
    // Draw a semi-transparent background
    web_rect(0, 0, 1920, 1080, "rgba(20, 20, 20, 0.8)")
    
    // Draw an orange circle
    web_circle(400, 300, 50, "orange")
    
    // Draw a grid line
    web_line(0, 300, 800, 300, 2, "#444")
    
    // Render status text
    web_text("System Online", 20, 40, 20)
}
```

# Metaprogramming and Function Calls

Mylo has specific functionality to allow dynamic programming by calling functions by their 
string name, and passing arguments.

### `call(name: str, args: list) -> any`
Call a function will a list of args, function must unpack the list
* **name**: The function name
* **args**: The list of args

#### Example Usage:

```javascript
fn my_func(x) {
    print(x + 1)
}

call("my_func", [[2 , 3]]) // <--- Note the double brackets here!

// prints [3, 4] 
```
