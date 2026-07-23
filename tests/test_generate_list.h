//
// Created by Bradley Pearce on 19/01/2026.
//

#ifndef MYLO_TEST_GENERATE_LIST_H
#define MYLO_TEST_GENERATE_LIST_H


#include <cstring>
#include "test_include.h"

// Toggle this define to print the source code for each test
#define PRINT_TEST_CODE false
#define PRINT_MACHINE_CODE false


// Mylo includes
extern "C" {
    #include "../src/vm.h"
    void compiler_reset();
    // declarations from compiler.c
    void parse(VM* vm, char* src);
    void mylo_reset(VM* vm); // If needed, or just rely on vm_init
}

// Tests the test harness
inline TestOutput test_test() {
    return {true};
}

inline TestOutput test_hello_world() {
    VM vm = {0};
    vm_init(&vm);
    MyloConfig.print_to_memory = true;

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = "print(\"hello\")";
    parse(&vm, const_cast<char *>(src.c_str()));
    run_vm(&vm, false);

    // Reset config so other tests don't break
    MyloConfig.print_to_memory = false;

    // Check if the output buffer actually contains the output
    // Note: OP_PRN adds a newline "\n"
    TestOutput output;
    // Check string pool
    output.result = (strcmp(vm.string_pool[(int)vm.globals[0]], "hello") == 0);
    output.result_string = output.result ? "" : "Expected 'hello' and got '" + std::string(vm.string_pool[(int)vm.globals[0]]) + "'";
    vm_cleanup(&vm);
    return output;
}

// The test VM, exposed here for post test hooks
VM test_vm;

inline TestOutput run_source_test(const std::string& src, const std::string& expected, bool clean_up = true) {
    vm_init(&test_vm);
    compiler_reset();
    MyloConfig.print_to_memory = true;
    
    if (PRINT_TEST_CODE) {
        std::cout << std::endl;
        std::cout << src << std::endl;
    }

    parse(&test_vm, const_cast<char *>(src.c_str()));
    run_vm(&test_vm, PRINT_MACHINE_CODE);

    // Reset config so other tests don't break
    MyloConfig.print_to_memory = false;

    // Check if the output buffer actually contains the output
    // Note: OP_PRN adds a newline "\n"
    TestOutput output;
    output.result = (strcmp(test_vm.output_char_buffer, expected.c_str()) == 0);
    output.result_string = output.result ? "" : "Expected '" + expected + "'" + "and got '" + std::string(test_vm.output_char_buffer) + "'";
    if (clean_up)
        vm_cleanup(&test_vm);
    return output;
}

inline TestOutput test_math_simple() {
    std::string src = """"
    "print((((5 + 2)*7)+1)/2)\n";
    std::string expected = """"
        "25\n"; // As it is an array, it is promoted to num array :')
    return run_source_test(src, expected);
}

inline TestOutput test_logic() {
    std::string src = """"
    "if (0 < 1) {print(1)} else {print(0)}\n"
    "if (1 > 1) {print(0)} else {print(1)}\n"
    "if (1 >= 1) {print(1)} else {print(0)}\n"
    "if (1 == 1) {print(1)} else {print(0)}\n"
    "if (1 || 0) {print(1)} else {print(0)}\n"
    "if (1 != 0) {print(1)} else {print(0)}\n"
    "if (0 <= 1) {print(1)} else {print(0)}\n"
    "if (1 && 1) {print(1)} else {print(0)}\n";

    std::string expected = """"
        "1\n1\n1\n1\n1\n1\n1\n1\n"; // As it is an array, it is promoted to num array :')
    return run_source_test(src, expected);
}


inline TestOutput test_change_types() {

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = """"
    "var x = \"hi\"\n"
    "print(x)\n"
    "x = 1\n"
    "print(x)\n"

    "x = f\"hi {x}\"\n"
    "print(f\"hi {x}\")\n";

    std::string expected = """"
    "hi\n"
    "1\n"
    "hi hi 1\n";

    return run_source_test(src, expected);
}

inline TestOutput test_ifs() {

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = """"
    "var x = 5\n"
    "if x <= 5 {\n"
        "print(\"it is less than or equals 5\")\n"
    "} else {\n"
        "print(\"its greater than 5\")"
    "}";

    std::string expected = """"
    "it is less than or equals 5\n";

    return run_source_test(src, expected);
}

inline TestOutput test_elif() {

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = """"
    "var x = 6\n"
    "if x <= 5 {\n"
        "print(\"it is less than or equals 5\")\n"
    "} elif x == 6 {\n"
        "print(\"Ok\")"
    "} else { print(2) }";

    std::string expected = """"
    "Ok\n";

    return run_source_test(src, expected);
}

inline TestOutput test_fib() {

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = """"
    "fn fib(n) {\n"
        "if (n < 2) {\n"
            "ret n\n"
        "}\n"
        // Recursively call fib
        "ret fib(n - 1) + fib(n - 2)\n"
    "}\n"
    "print(\"Calculating Fib(10)...\")\n"
    "var result = fib(10)\n"
    "print(result)";

    std::string expected = """"
    "Calculating Fib(10)...\n"
    "55\n";

    return run_source_test(src, expected);
}

inline TestOutput test_ternary() {

    // Note: escape the quote properly for C string "print(\"hello\")"
    std::string src = """"
    "var x = 3\n"
    "print(f\"hello { x > 3 ? \"little one\" else \"not little one}\")";


    std::string expected = """"
    "hello not little one\n";

    return run_source_test(src, expected);
}

inline TestOutput test_literal_loop() {
    std::string src = """"
    "for (var i in 0...5) {\n"
    "print(i)"
    "}";

    std::string expected = """"
    "0\n1\n2\n3\n4\n5\n";

    return run_source_test(src, expected);
}

inline TestOutput test_literal_reverse() {
    std::string src = """"
    "for (var i in 5...0) {\n"
    "print(i)"
    "}";

    std::string expected = """"
    "5\n4\n3\n2\n1\n0\n";

    return run_source_test(src, expected);
}

inline TestOutput test_variable_loop() {
    std::string src = """"
    "var x = 3\n"
    "for (var i in x...5) {\n"
    "print(i)"
    "}"
    "for (var i in 5...x) {\n"
    "print(i)"
    "}";

    std::string expected = """"
    "3\n4\n5\n5\n4\n3\n";

    return run_source_test(src, expected);
}

inline TestOutput test_struct() {

    std::string src = """"
    "struct my_struct {\n"
        "var age\n"
        "var name\n"
    "}\n"

    "var person : my_struct = { age=30, name=\"Andy\" }\n"
    "var age = person.age\n"

    "print(person)\n"
    "print(age)\n"
    "print(person.name)\n";

    std::string expected = """"
    "[Struct Ref:0]\n"
    "30\n"
    "Andy\n";

    return run_source_test(src, expected);
}

inline TestOutput test_maps() {

    std::string src = """"
    // 1. Creation: Map literal syntax
    "var my_map = { \"name\"=\"foo\", \"age\"=32 }\n"

    // Print the map object (should be Ref: 0 if it's the first allocation)
    "print(my_map)\n"

    // 2. Read: Access by key
    "print(my_map[\"name\"])\n"
    "print(my_map[\"age\"])\n"

    // 3. Update: Change existing value
    "my_map[\"name\"] = \"bar\"\n"
    "print(my_map[\"name\"])\n"

    // 4. Insert: Add new key-value pair
    "my_map[\"city\"] = \"London\"\n"
    "print(my_map[\"city\"])\n"

    // 5. Contains check
    "if (contains(my_map, \"city\")) {\n"
        "print(\"Found\")\n"
    "}\n";

    std::string expected = """"
    "{name: \"foo\", age: 32}\n"
    "foo\n"
    "32\n"
    "bar\n"
    "London\n"
    "Found\n";

    return run_source_test(src, expected);
}

inline TestOutput test_to_num() {

    std::string src = """"
    // 1. Creation: Map literal syntax
    "var x = \"1\"\n"
    "if (to_num(x) == 1) {\n"
    "print(x)\n"
    "}"
    "var y = \"2.8\"\n"
    "if (to_num(y) == 2.8) {\n"
    "print(y)\n"
    "}";

    std::string expected = """"
    "1\n"
    "2.8\n";

    return run_source_test(src, expected);
}

inline TestOutput test_to_str() {

    std::string src = """"
    // 1. Creation: Map literal syntax
    "var x = 1\n"
    "if (to_string(x) == \"1\") {\n"
        "print(x)\n"
    "}"
    "var y = 2.8\n"
    "if (to_string(y) == \"2.8\") {\n"
    "print(y)\n"
    "}";

    std::string expected = """"
    "1\n"
    "2.8\n";

    return run_source_test(src, expected);
}

inline TestOutput test_string_interp() {

    std::string src = """"
    "var name = \"Ben\"\n"
    "var age = 37.5\n"
    "var string = f\"I am {name}, and I am {age + 2.5}\"\n"
    "print(string)";

    std::string expected = """"
    "I am Ben, and I am 40\n";

    return run_source_test(src, expected);
}

inline TestOutput test_scope() {

    std::string src = """"
    "var x = 5\n"

    "fn foo() {\n"
        "var x = 10\n"
        "ret x\n"
    "}\n"

    "var y = foo()\n"
    "print(x)"
    "print(y)";

    std::string expected = """"
    "5\n10\n";

    return run_source_test(src, expected);
}

inline TestOutput test_namespace() {

    std::string src = """"
    "var foo = 5\n"
    "mod MySpace {\n"
        "fn foo() {\n"
            "ret 10\n"
        "}\n"
    "}\n"
    "print(foo)\n"
    "print(MySpace::foo())\n";

    std::string expected = """"
    "5\n10\n";

    return run_source_test(src, expected);
}

inline TestOutput test_while_for() {

    std::string src = """"
    "var i = 0\n"
    "for (i < 2) {\n"
        "print(i)\n"
        "i = i + 1\n"
    "}\n";

    std::string expected = """"
    "0\n1\n";

    return run_source_test(src, expected);
}

inline TestOutput test_var_access() {

    std::string src = """"
    "struct my_struct {\n"
        "var name\n"
    "}\n"
    "var person : my_struct = {name=\"Andy\"}\n"

    "fn hi(n : my_struct) {\n"
        "print(f\"Hi {n.name}\")\n"
        "ret\n"
    "}\n"
    "fn change_name(x : my_struct) {\n"
        "x.name = \"Darren\""
        "ret x\n"
    "}\n"
    "hi(change_name(person))\n";

    std::string expected = """"
    "Hi Darren\n";

    return run_source_test(src, expected);
}

inline TestOutput test_arrays() {

    std::string src = """"
    "var my_list = [\"m\", \"o\", \"o\", \"n\"]\n"

    // prints 'm'
    "print(my_list[0])\n"

    "for (x in my_list) {\n"
        "print(x)\n"
    "}\n"

    "var next_list = [1, 2, 33]\n"
    "for (x in next_list) {\n"
        "print(x)\n"
    "}\n";

    std::string expected = """"
    "m\nm\no\no\nn\n1\n2\n33\n";

    return run_source_test(src, expected);
}

inline TestOutput test_list_array() {

    std::string src = """"
    "struct Color {\n"
        "var rgba\n"
    "}\n"
    "var my_list: Color[] = [{rgba= 255}, {rgba= 444}]\n"

    "for (x: Color in my_list) {\n"
        "print(x.rgba)\n"
    "}\n";

    std::string expected = """"
    "255\n444\n";

    return run_source_test(src, expected);
}

inline TestOutput test_list_concat() {

    std::string src = """"
    "struct Color {\n"
        "var rgba\n"
    "}\n"
    "var my_list: Color[] = []\n"
    "my_list = my_list + [{rgba=500}, {rgba=800}, {rgba=22}]\n"
    "print(my_list[1].rgba)\n";

    std::string expected = """"
    "800\n";

    return run_source_test(src, expected);
}

inline TestOutput test_string_concat() {

    std::string src = """"
    "print(\"He\" + \"llo\")\n"
    "print(\"He\" + 77)\n";

    std::string expected = """"
    "Hello\nHe77\n";

    return run_source_test(src, expected);
}


inline TestOutput test_list_slices() {

    std::string src = """"
    "struct Color {\n"
    "var rgba\n"
    "}\n"
    "var my_list: Color[] = []\n"
    "my_list = my_list + [{rgba=500}, {rgba=400}, {rgba=300}, {rgba=700}, {rgba=900}]\n"
    "my_list = my_list[1:3]\n"
    // prints 'm'
    "for (x: Color in my_list) {\n"
        "print(x.rgba)\n"
    "}\n";

    std::string expected = """"
    "400\n300\n700\n";

    return run_source_test(src, expected);
}

inline TestOutput test_func_iterator_passing() {

    std::string src = """"
    "struct foo {\n"
        "var name\n"
    "}\n"

    "fn list_names(n : foo[])\n"
    "{\n"
        "for (x : foo in n) {\n"
            "print(x.name)\n"
        "}\n"
        "ret\n"
    "}\n"
    "var myvar0: foo[] = [{name=\"Harry\"}, {name=\"Barry\"}, {name=\"John\"}]\n"

    // This prints 'Harry', 'Barry', 'John'
    "list_names(myvar0)\n";
    std::string expected = """"
    "Harry\nBarry\nJohn\n";

    return run_source_test(src, expected);
}

inline TestOutput test_for_break() {

    std::string src = """"
    "for (var x in 0...5) {\n"
        "print(x)\n"
        "if (x > 2) {\n"
            "break\n"
        "}\n"
    "}\n";

    std::string expected = """"
    "0\n1\n2\n3\n";

    return run_source_test(src, expected);
}

inline TestOutput test_for_var_break() {

    std::string src = """"
    "var x = 0\n"
    "for (x < 5) {\n"
        "print(x)\n"
        "if (x > 2) {\n"
            "break\n"
        "}\n"
        "x = x + 1"
    "}\n";

    std::string expected = """"
    "0\n1\n2\n3\n";

    return run_source_test(src, expected);
}

inline TestOutput test_forever() {

    std::string src = """"
    "var x = 0\n"
    "forever {\n"
        "print(x)\n"
        "x = x + 1\n"
        "if (x > 2) {\n"
            "break\n"
        "}\n"
    "}\n";

    std::string expected = """"
    "0\n1\n2\n";

    return run_source_test(src, expected);
}

inline TestOutput test_for_continue() {

    std::string src = """"
    "for (var x in 0...5) {\n"
        "if (x < 2) {\n"
            "continue\n"
        "}\n"
        "print(x)\n"
    "}\n";

    std::string expected = """"
    "2\n3\n4\n5\n";

    return run_source_test(src, expected);
}

inline TestOutput test_file_str_io() {

    std::string src = """"
    "write_file(\"test.txt\", \"Line1\n\", \"w\")\n"
    // "a" = append mode (replaces append_file)
    "write_file(\"test.txt\", \"Line2\n\", \"a\")\n"
    "var lines = read_lines(\"test.txt\")\n"
    "for (line in lines) {\n"
        "print(line)\n"
    "}\n";

    std::string expected = """"
    "Line1\nLine2\n";

    return run_source_test(src, expected);
}

inline TestOutput test_len() {

    std::string src = """"
    "var x = [1, 2, 3, 4]\n"
    // "a" = append mode (replaces append_file)
    "print(len(x))\n";
    std::string expected = """"
    "4\n";

    return run_source_test(src, expected);
}

inline TestOutput test_file_bin_io() {

    std::string src = """"
    "var bytes = [65, 66, 67, 10]\n"
    "write_bytes(\"data.bin\", bytes)\n"

    "var read_data = read_bytes(\"data.bin\", 1)\n"
    "for (b in read_data) {\n"
        "print(b)\n"
    "}\n";

    std::string expected = """"
    "65\n66\n67\n10\n";

    return run_source_test(src, expected);
}

inline TestOutput test_contains() {

    std::string src = """"
    "var list = [\"apple\", \"banana\", \"cherry\"]\n"
    "if (contains(list, \"banana\")) {\n"
        "print(\"A\")\n"
    "}\n"

    "if (contains(\"Teamwork\", \"work\")) {\n"
        "print(\"B\")\n"
    "}\n"
    "if (contains(\"Teamwork\", \"fnarf\")) {\n"
    "}\n"
    "else {"
        "print(\"C\")\n"
    "}";

    std::string expected = """"
    "A\nB\nC\n";

    return run_source_test(src, expected);
}

inline TestOutput test_enums() {

    std::string src = """"
    "enum Transport {\n"
        "car,\n"
        "bike,\n"
        "truck,\n"
    "}\n"
    "var myvar = Transport::car\n"
    "if (myvar == Transport::bike) {\n"
        "print(\"A\")\n"
    "}\n"

    "if (myvar == Transport::car) {\n"
        "print(\"B\")\n"
    "}\n";
    std::string expected = """"
    "B\n";

    return run_source_test(src, expected);
}

inline TestOutput test_module_path_and_import() {

    std::string src = """"
    // Assumes run from build
    "module_path(\"../tests/\")\n"
    "module_path(\"tests/\")\n"
    "module_path(\"../../tests/\")\n"
    "import \"test_import.mylo\"\n"
    "my_test()\n";
    std::string expected = """"
    "3\n";

    return run_source_test(src, expected);
}

inline TestOutput test_bool() {

    std::string src = """"
    "if (true == false) {\n"
    "print(true)\n"
    "}\n"
    "else {\n"
    "print(false)\n"
    "}\n"
    "if (true == true) {\n"
    "print(true)\n"
    "}\n"
    "else {\n"
    "print(false)\n"
    "}\n";


    std::string expected = """"
    "0\n1\n";

    return run_source_test(src, expected);
}

inline TestOutput test_bytes_print() {

    std::string src = """"
    "var header = b\"PNG\"\n"
    "print(header)\n";

    std::string expected = """"
    "b\"PNG\"\n";
    return run_source_test(src, expected);
}

inline TestOutput test_bytes_hex_print() {

    std::string src = """"
    "var header = b\"\\xFF\"\n"
    "print(to_num(header[0]))\n";

    std::string expected = """"
    "255\n";
    return run_source_test(src, expected);
}

inline TestOutput test_byte_iterator() {

    std::string src = """"
    "var header = b\"1298\"\n"
    "for (x in header) {\n"
    "print(x)\n"
    "}";
    std::string expected = """"
    "49\n50\n57\n56\n";
    return run_source_test(src, expected);
}

inline TestOutput test_map_iterator() {

    std::string src = """"
    "var header = {\"cat\"=\"Glad\", \"dog\"=\"Meg\"}"
    "for (key, value in header) {\n"
    "print(f\"{key}, {value}\")\n"
    "}";
    std::string expected = """"
    "cat, Glad\ndog, Meg\n";
    return run_source_test(src, expected);
}

inline TestOutput test_map_passing() {

    std::string src = """"
    "fn foo() {\n"
        "var header = {}\n"
        "for (x in 0...3) {\n"
            "header[x] = x+1\n"
        "}\n"
        "print(header)\n"
        "ret header\n"
    "}\n"
    "var o = foo()\n"
    "print(o)\n";
    std::string expected = """"
    "{0: 1, 1: 2, 2: 3, 3: 4}\n{0: 1, 1: 2, 2: 3, 3: 4}\n";
    return run_source_test(src, expected);
}

inline TestOutput test_byte_slice_iterator() {

    std::string src = """"
    "var header = b\"1298\"\n"
    "var slice = header[1:2]"
    "for (x in slice) {\n"
        "print(x)\n"
    "}";
    std::string expected = """"
    "50\n57\n";
    return run_source_test(src, expected);
}

inline TestOutput test_byte_slice_assignment() {

    std::string src = """"
    "var header = b\"1111\"\n"
    "header[1:2] = b\"22\"\n"
    "for (x in header) {\n"
        "print(x)\n"
    "}";
    std::string expected = """"
    "49\n50\n50\n49\n";
    return run_source_test(src, expected);
}

inline TestOutput test_array_slice_assignment() {

    std::string src = """"
    "var header = [1, 2, 3, 4]\n"
    "header[1:2] = [5, 6]\n"
    "for (x in header) {\n"
        "print(x)\n"
    "}";
    std::string expected = """"
    "1\n5\n6\n4\n";
    return run_source_test(src, expected);
}

inline TestOutput test_list_reserve() {

    std::string src = """"
    "var header: num[] = list(5)\n"
    "for (x in header) {\n"
        "print(x)\n"
    "}";
    std::string expected = """"
    "0\n0\n0\n0\n0\n";
    return run_source_test(src, expected);
}

inline TestOutput test_list_add() {

    std::string src = """"
    "var header: num[] = list(4)\n"
    "header = add(header, 1, 22)"
    "for (x in header) {\n"
        "print(x)\n"
    "}";
    std::string expected = """"
    "0\n22\n0\n0\n0\n";
    return run_source_test(src, expected);
}

inline TestOutput test_list_remove() {

    std::string src = """"
    "var header: num[] = list(4)\n"
    "header = add(header, 1, 22)\n"
    "header = remove(header, 0)"
    "for (x in header) {\n"
        "print(x)\n"
    "}";
    std::string expected = """"
    "22\n0\n0\n0\n";
    return run_source_test(src, expected);
}

inline TestOutput test_map_remove() {

    std::string src = """"
    "var header = {\"cat\"=\"Glad\", \"dog\"=\"Meg\"}\n"
    "header = remove(header, \"cat\")\n"
    "for (key, value in header) {\n"
    "print(f\"{key}, {value}\")\n"
    "}";
    std::string expected = """"
    "dog, Meg\n";
    return run_source_test(src, expected);
}

inline TestOutput test_arr_str_slice_assignment() {

    std::string src = """"
    "var header = [\"cart\", \"dorg\", \"fox\", \"rat\"]\n"
    "header[0:1] = [\"cat\", \"dog\"]\n"
    "for (x in header) {\n"
        "print(x)\n"
    "}";
    std::string expected = """"
    "cat\ndog\nfox\nrat\n";
    return run_source_test(src, expected);
}


inline TestOutput test_vector_ops_add() {

    std::string src = """"
    "var x = [1,2,3,4,5]\n"
    "x = x[2:3] + x[2]\n"
    "for (y in x) {\n"
        "print(y)\n"
    "}";
    std::string expected = """"
    "6\n7\n";
    return run_source_test(src, expected);
}

inline TestOutput test_vector_ops_mul() {

    std::string src = """"
    "var x = [1,2,3,4,5]\n"
    "x = x * x[2]\n"
    "for (y in x) {\n"
        "print(y)\n"
    "}";
    std::string expected = """"
    "3\n6\n9\n12\n15\n";
    return run_source_test(src, expected);
}

inline TestOutput test_vector_str_add() {

    std::string src = """"
    "var x = [\"cat\",\"dog\"]\n"
    "x = x + \"fish\"\n"
    "for (y in x) {\n"
        "print(y)\n"
    "}";
    std::string expected = """"
    "catfish\ndogfish\n";
    return run_source_test(src, expected);
}

inline TestOutput test_vector_byte_add() {

    std::string src = """"
    "var x = [b\"\\xFE\",b\"\\xFE\"]\n"
    "x = x + b\"\\xFE\"\n"
    "for (y in x) {\n"
        "print(y)\n"
    "}";
    std::string expected = """"
    "b\"\\xFE\\xFE\"\nb\"\\xFE\\xFE\"\n";
    return run_source_test(src, expected);
}

inline TestOutput test_bytes_vector_add() {

    std::string src = """"
    "var header = b\"\\xFE\\xFE\"\n"
    "header = header + 1"
    "print(to_num(header[0]))\n";
    std::string expected = """"
    "255\n";
    return run_source_test(src, expected);
}


inline TestOutput test_where_str() {

    std::string src = """"
        "var header = \"ohblast\""
        "print(where(header,\"blast\"))\n";
    std::string expected = """"
        "2\n";
    return run_source_test(src, expected);
}

inline TestOutput test_where_list() {

    std::string src = """"
        "var header = [1,2,3,4,5]\n"
        "print(where(header,4))\n";
    std::string expected = """"
        "3\n";
    return run_source_test(src, expected);
}

inline TestOutput test_split_str() {

    std::string src = """"
        "var header = \"a,b,c\"\n"
        "var parts = split(header, \",\")\n"
        "print(parts[0])\n"
        "print(parts[1])\n";
    std::string expected = """"
        "a\nb\n";
    return run_source_test(src, expected);
}

inline TestOutput test_print_array() {

    std::string src = """"
        "var header = \"[1,2,3,4,5,6,7,8,99]\"\n"
        "print(header)\n";
    std::string expected = """"
        "[1,2,3,4,5,6,7,8,99]\n";
    return run_source_test(src, expected);
}

inline TestOutput test_print_map() {

    std::string src = """"
        "var header = {\"foo\"=99, \"bar\"=88}\n"
        "print(header)\n";
    std::string expected = """"
        "{foo: 99, bar: 88}\n";
    return run_source_test(src, expected);
}

inline TestOutput test_range() {

    std::string src = """"
        "var header = range(0,2,10)\n"
        "print(header)\n";
    std::string expected = """"
        "[0, 2, 4, 6, 8, 10]\n";
    return run_source_test(src, expected);
}

inline TestOutput test_range_rev() {
    std::string src = """"
        "var header = range(10,2,-2)\n"
        "print(header)\n";
    std::string expected = """"
        "[10, 8, 6, 4, 2, 0, -2]\n";
    return run_source_test(src, expected);
}

inline TestOutput test_range_dec() {
    std::string src = """"
        "var header = range(-0.1,0.1,0)\n"
        "print(header)\n";
    std::string expected = """"
        "[-0.1, 0]\n";
    return run_source_test(src, expected);
}

inline TestOutput test_for_list() {
    std::string src = """"
        "fn foo(x) { ret x*2} \n"
        "var header = range(0,1,3)\n"
        "var x = for_list(\"foo\", header)\n"
        "print(x)";
    std::string expected = """"
        "[0, 2, 4, 6]\n";
    return run_source_test(src, expected);
}

inline TestOutput test_for_list_str() {
    std::string src = """"
        "fn foo(x) { ret f\"{x}fish\"} \n"
        "var header = [\"cat\",\"dog\"]\n"
        "var x = for_list(\"foo\", header)\n"
        "print(x)";
    std::string expected = """"
        "[\"catfish\", \"dogfish\"]\n";
    return run_source_test(src, expected);
}

inline TestOutput test_min() {
    std::string src = """"
        "print(min(0.4, 0.2))\n";
    std::string expected = """"
        "0.2\n";
    return run_source_test(src, expected);
}

inline TestOutput test_max() {
    std::string src = """"
        "print(max(0.4, 0.2))\n";
    std::string expected = """"
        "0.4\n";
    return run_source_test(src, expected);
}

inline TestOutput test_dist() {
    std::string src = """"
        "print(distance(0,0,1,1))\n";
    std::string expected = """"
        "1.41421\n";
    return run_source_test(src, expected);
}

inline TestOutput test_mix() {
    std::string src = """"
        "print(mix(0,1,.5))\n";
    std::string expected = """"
        "0.5\n";
    return run_source_test(src, expected);
}

inline TestOutput test_list_min_max() {
    std::string src = """"
    "var nums = [10, 5, 20, 2]\n"
    "print(min_list(nums))\n" // 2
    "print(max_list(nums))\n"; // 20\n";
    std::string expected = """"
        "2\n20\n";
    return run_source_test(src, expected);
}


inline TestOutput test_strong_types_list() {
    std::string src = """"
    "var pixels: byte[] = [255, 0, 128]\n"  // Stored as bytes in heap
    "var mesh: f32[] = [1.5, 0.2, -3.4]\n" // Stored as floats in heap
    "var counts: i32[] = [1000, 2000]\n"   // Stored as ints in heap
    "print(pixels)\n"
    "print(mesh)\n"
    "print(counts)\n";
    std::string expected = """"
        "b\"\\xFF\\x00\\x80\"\n[1.5, 0.2, -3.4]\n[1000, 2000]\n";
    return run_source_test(src, expected);
}


// Region tests removed because GC supersedes regions

inline TestOutput test_loop_scoping() {
    std::string src = 
    "var x = 0\n"
    "forever {var f = range(0,1,1000)\n x = x + 1 \n if (x > 2) {break}}\n"
    "x = 0 \n for (f in range(0,1,1000)){\n x = x + 1 \n if (x > 2) {break}}\n"
    "print(1)";
    std::string expected = 
        "1\n";
    auto x = run_source_test(src, expected, false);
    vm_cleanup(&test_vm);
    return x;
}

inline TestOutput test_strong_types_i() {
    std::string src = """"
            "var z : i64 = 67.2 + 33.7\n"
            "var x : i32 = 67.2 + 33.7\n"
            "var y : i16 = 67.2 + 33.7\n"
            "print(z)\n"
            "print(x)\n"
            "print(y)\n";
    std::string expected = """"
            "100\n100\n100\n";
    return run_source_test(src, expected);
}

inline TestOutput test_type_promototion_bool() {
    std::string src = """"
    "var x = b\"\\xFE\"\n"
    "print(x + 2)\n";
    std::string expected = """"
        "[256]\n"; // As it is an array, it is promoted to num array :')
    return run_source_test(src, expected);
}

inline TestOutput test_typed_struct() {
    std::string src = """"
    "struct A {\n"
    "var a : i16\n"
    "var b : i32\n"
    "}\n"
    "var x : A = {a=88.2, b=11.33}\n"
    "print(f\"{x.a},{x.b}\")\n";
    std::string expected = """"
        "88,11\n"; // As it is an array, it is promoted to num array :')
    return run_source_test(src, expected);
}

inline TestOutput test_thread() {
    std::string src = 
    "bus_set(\"input\", 100)\n"
    "fn foo_1() {\n"
    "   var input = bus_get(\"input\")\n"
    "   bus_set(\"result\", input + 42)\n"
    "}\n"
    "var worker_id = create_worker(\"foo_1\")\n"
    "if (worker_id < 0) {print(-1)}\n"
    "var running = 1\n forever { if (running != 1) { break } \n  var status = check_worker(worker_id) \n"
    "if (status == 1) { running = 0} }\n"
    "dock_worker(worker_id)\n"
    "print(bus_get(\"result\"))";
    std::string expected = 
        "142\n";
    return run_source_test(src, expected);
}

inline TestOutput test_bus() {
    std::string src = 
    "fn foo_1() {\n"
    "   bus_set(\"test\", 42)\n"
    "}\n"
    "var worker_id = create_worker(\"foo_1\")\n"
    "if (worker_id < 0) {print(-1)}\n"
    "var running = 1\n forever { if (running != 1) { break } \n  var status = check_worker(worker_id) \n"
    "if (status == 1) { running = 0} }\n"
    "dock_worker(worker_id)\n"
    "var t = bus_get(\"test\")\n"
    "print(t)";
    std::string expected = 
        "42\n"; 
    return run_source_test(src, expected);
}

// Region tests removed because GC supersedes regions

inline TestOutput test_nested_for_loop() {

    std::string src = """"
    "for (var x in 0...2) {\n"
    "for (var y in 0...2) {\n"
        "print(f\"{x}, {y}\")\n"
    "}"
    "}\n";
    std::string expected = """"
    "0, 0\n"
    "0, 1\n"
    "0, 2\n"
    "1, 0\n"
    "1, 1\n"
    "1, 2\n"
    "2, 0\n"
    "2, 1\n"
    "2, 2\n";

    return run_source_test(src, expected);
}

inline TestOutput test_type_chaining() {

    std::string src = """"
    "struct Vec3 {"
    "var x\n"
    "var y\n"
    "var z\n"
    "}\n"
    "struct Foo {\n"
    "var c : Vec3\n"
    "}\n"
    "var t : Foo = {}\n"
    "t.c = {x=255, y=0, z=0}\n"
    "print(t.c.x)\n"
    "t.c.y = 99\n"
    "print(t.c.y)\n";

    std::string expected = """"
    "255\n99\n";
    return run_source_test(src, expected);
}

inline TestOutput test_nested_scope_conditional_return() {

    std::string src = """"
    "fn bar(x) {\n"
    "if (x > 5) {\n"
        "ret 0...2\n"
    "}ret 1\n"
    "}\n"
    "print(bar(10))";

    std::string expected = """"
    "[0, 1, 2]\n";

    return run_source_test(src, expected);
}

inline TestOutput test_print_interp_len() {

    std::string src = """"
    "var x = \"hi\"\n"
    "print(f\"123456789123456789123456789123456789123456789123456789123456789123456789123456789123456789{x}\")";

    std::string expected = """"
    "123456789123456789123456789123456789123456789123456789123456789123456789123456789123456789hi\n";

    return run_source_test(src, expected);
}

inline TestOutput test_forever_stack_leak() {
    std::string src =
    "var i = 0\n"
    "forever {\n"
    "    // 1. Define a local variable (allocated on stack)\n"
    "    var x = 10\n"
    "    \n"
    "    // 2. Define a local array (allocated on heap + ref on stack)\n"
    "    var y = [1, 2]\n"
    "    \n"
    "    // 3. Loop causing inner scope allocations\n"
    "    for (z in y) {\n"
    "        var a = z + 1\n"
    "    }\n"
    "    \n"
    "    // 4. Loop Condition\n"
    "    i = i + 1\n"
    "    if (i > 1000) { break }\n"
    "}\n"
    "print(i)";

    // Reset VM
    vm_init(&test_vm);
    compiler_reset();
    MyloConfig.print_to_memory = true;

    parse(&test_vm, const_cast<char *>(src.c_str()));
    run_vm(&test_vm, false);

    MyloConfig.print_to_memory = false;

    TestOutput output;

    // VALIDATION:
    // If sp (Stack Pointer) is not -1, items were left on the stack.
    if (test_vm.sp != -1) {
        output.result = false;
        output.result_string = "Stack Leak Detected! SP is " + std::to_string(test_vm.sp) + " (Expected -1)";
    } else {
        output.result = true;
    }

    vm_cleanup(&test_vm);
    return output;
}

inline TestOutput test_conditional_scoped_var_fn() {

    std::string src = """"
    "struct Dummy {\n"
    "    var val \n"
    "}\n"

    "fn test_if_scope() {\n"
    "    var trigger = 0\n"

        // The compiler will assign 'inner_var' to stack offset 1
    "    if (trigger == 1) {\n"
    "        var inner_var = 99\n"
    "        print(inner_var)\n"
    "    }\n"

        // The compiler will assign 'obj' to stack offset 2.
        // However, because the 'if' condition was false, 'inner_var' was never
        // pushed to the physical VM stack. 'obj' is actually sitting at offset 1!
        "var obj: Dummy = {val: 42}\n"

        // The VM attempts to read offset 2, finds uninitialized memory (0.0),
        // and throws a "Type Mismatch" when trying to access '.val'
        "print(obj.val)\n"
    "}\n"
    "test_if_scope()";
    std::string expected = """"
    "42\n";

    return run_source_test(src, expected);
}

inline TestOutput test_conditional_scoped_var_forever() {

    std::string src = """"
    "struct Dummy {\n"
    "    var val \n"
    "}\n"

    "forever {\n"
    "    var trigger = 0\n"

        // The compiler will assign 'inner_var' to stack offset 1
    "    if (trigger == 1) {\n"
    "        var inner_var = 99\n"
    "        print(inner_var)\n"
    "    }\n"

        // The compiler will assign 'obj' to stack offset 2.
        // However, because the 'if' condition was false, 'inner_var' was never
        // pushed to the physical VM stack. 'obj' is actually sitting at offset 1!
        "var obj: Dummy = {val: 42}\n"

        // The VM attempts to read offset 2, finds uninitialized memory (0.0),
        // and throws a "Type Mismatch" when trying to access '.val'
        "print(obj.val)\n"
        "break\n"
    "}\n";
    std::string expected = """"
    "42\n";

    return run_source_test(src, expected);
}

inline TestOutput test_conditional_scoped_var_for() {

    std::string src = """"
    "struct Dummy {\n"
    "    var val \n"
    "}\n"

    "for (x in 0...1) {\n"
    "    var trigger = 0\n"

        // The compiler will assign 'inner_var' to stack offset 1
    "    if (trigger == 1) {\n"
    "        var inner_var = 99\n"
    "        print(inner_var)\n"
    "    }\n"

        // The compiler will assign 'obj' to stack offset 2.
        // However, because the 'if' condition was false, 'inner_var' was never
        // pushed to the physical VM stack. 'obj' is actually sitting at offset 1!
        "var obj: Dummy = {val: 42}\n"

        // The VM attempts to read offset 2, finds uninitialized memory (0.0),
        // and throws a "Type Mismatch" when trying to access '.val'
        "print(obj.val)\n"
        "break\n"
    "}\n";
    std::string expected = """"
    "42\n";

    return run_source_test(src, expected);
}

inline TestOutput test_enum_string_repl() {

    std::string src = """"
    "enum GOO {"
    "poo,"
    "too,"
    "}"
    "print(GOO::poo)"
    "print(f\">{GOO::too}\")\n";
    std::string expected = """"
    "poo\n>too\n";

    return run_source_test(src, expected);
}

inline TestOutput test_type_infer() {

    std::string src = """"
    "enum GOO {\n"
    "poo,\n"
    "too,\n"
    "}\n"
    "print(type(GOO::poo))\n"
    "var x = \"hi\"\n"
    "var y = 99\n"
    "print(type(x))\n"
    "print(type(y))\n";
    std::string expected = """"
    "GOO\nstr\nnum\n";

    return run_source_test(src, expected);
}

inline TestOutput test_enum_iter() {
    std::string src = """"
    "enum GOO {\n"
    "poo,\n"
    "too,\n"
    "}\n"
    "enum FOO {\n"
    "zoo,\n"
    "boo,\n"
    "}\n"
    "for (x in GOO) { print(x)}\n"
    "for (x in FOO) { print(x)}\n";

    std::string expected = """"
    "poo\ntoo\nzoo\nboo\n";

    return run_source_test(src, expected);
}

inline TestOutput test_oo() {
    std::string src = 
    "struct Point {\n"
    "    var x\n"
    "    var y\n"
    "    fn get_x(self) {\n"
    "        ret self.x\n"
    "    }\n"
    "    fn distance(self, other: Point) {\n"
    "        ret self.x + self.y + other.x + other.y\n"
    "    }\n"
    "}\n"
    "var p1 : Point = { x=1, y=2 }\n"
    "var p2 : Point = { x=3, y=4 }\n"
    "print(p1.get_x())\n"
    "print(p1.distance(p2))\n";
    std::string expected = """"
        "1\n"
        "10\n";  
    return run_source_test(src, expected);
}

inline void test_generate_list() {
    ADD_TEST("Test Test", test_test);
    ADD_TEST("Test Print", test_hello_world);
    ADD_TEST("Test Simple Math", test_math_simple);
    ADD_TEST("Test Logic", test_logic);
    ADD_TEST("Test Change Types", test_change_types);
    ADD_TEST("Test Fib(10)", test_fib);
    ADD_TEST("Test If Statements", test_ifs);
    ADD_TEST("Test Elseif Statements", test_elif);
    ADD_TEST("Test Ternary Operation", test_ternary);
    ADD_TEST("Test Literal Loop", test_literal_loop);
    ADD_TEST("Test Variable Loop", test_variable_loop);
    ADD_TEST("Test While For Loop", test_while_for);
    ADD_TEST("Test Struct", test_struct);
    ADD_TEST("Test Type Struct", test_typed_struct);
    ADD_TEST("Test String Interpolation", test_string_interp);
    ADD_TEST("Test String Concat", test_string_concat);
    ADD_TEST("Test Scope", test_scope);
    ADD_TEST("Test Variable Access", test_var_access);
    ADD_TEST("Test Reverse Loop", test_literal_reverse);
    ADD_TEST("Test Arrays", test_arrays);
    ADD_TEST("Test Namespace", test_namespace);
    ADD_TEST("Test List Array", test_list_array);
    ADD_TEST("Test List Concatenation", test_list_concat);
    ADD_TEST("Test List Slicing", test_list_slices);
    ADD_TEST("Test Func. Iter. Passing", test_func_iterator_passing);
    ADD_TEST("Test For Loop Break", test_for_break);
    ADD_TEST("Test For Var Loop Break", test_for_var_break);
    ADD_TEST("Test For Loop Continue", test_for_continue);
    ADD_TEST("Test For File IO (str)", test_file_str_io);
    ADD_TEST("Test For File IO (bin)", test_file_bin_io);
    ADD_TEST("Test for len(array)", test_len);
    ADD_TEST("Test for contains(array)", test_contains);
    ADD_TEST("Test for Enums", test_enums);
    ADD_TEST("Test for Module Path & Import", test_module_path_and_import);
    ADD_TEST("Test for Maps", test_maps);
    ADD_TEST("Test to_num", test_to_num);
    ADD_TEST("Test to_str", test_to_str);
    ADD_TEST("Test bool", test_bool);
    ADD_TEST("Test forever", test_forever);
    ADD_TEST("Test bytes (print)", test_bytes_print);
    ADD_TEST("Test bytes (hex)", test_bytes_hex_print);
    ADD_TEST("Test bytes (for)", test_byte_iterator);
    ADD_TEST("Test bytes (slice)", test_byte_slice_iterator);
    ADD_TEST("Test bytes (slice-assignment)", test_byte_slice_assignment);
    ADD_TEST("Test Array (slice-assignment)", test_array_slice_assignment);
    ADD_TEST("Test Array Str (slice-assignment)", test_arr_str_slice_assignment);
    ADD_TEST("Test List (list(10))", test_list_reserve);
    ADD_TEST("Test List (add())", test_list_add);
    ADD_TEST("Test List (remove())", test_list_remove);
    ADD_TEST("Test Map Iterator", test_map_iterator);
    ADD_TEST("Test Map Passing", test_map_passing);
    ADD_TEST("Test Map (remove())", test_map_remove);
    ADD_TEST("Test Vector (add())", test_vector_ops_add);
    ADD_TEST("Test Vector (mul())", test_vector_ops_mul);
    ADD_TEST("Test Vector(str) (add())", test_vector_str_add);
    ADD_TEST("Test Vector(byte) (add())", test_bytes_vector_add);
    ADD_TEST("Test Where str", test_where_str);
    ADD_TEST("Test Where list", test_where_list);
    ADD_TEST("Test Split str", test_split_str);
    ADD_TEST("Test print (list)", test_print_array);
    ADD_TEST("Test print (map)", test_print_map);
    ADD_TEST("Test range", test_range);
    ADD_TEST("Test range reverse", test_range_rev);
    ADD_TEST("Test range decimal", test_range_dec);
    ADD_TEST("Test for-list", test_for_list);
    ADD_TEST("Test for-list str", test_for_list_str);
    ADD_TEST("Test Math min", test_min);
    ADD_TEST("Test Math max", test_max);
    ADD_TEST("Test Math dist", test_dist);
    ADD_TEST("Test Math mix", test_mix);
    ADD_TEST("Test Min-Max Array", test_list_min_max);
    ADD_TEST("Test Strongly Types Arrays", test_strong_types_list);
    ADD_TEST("Test Loop Scoping", test_loop_scoping);
    ADD_TEST("Test Strong Types ints", test_strong_types_i);
    ADD_TEST("Test String Types Promotion byte -> num", test_type_promototion_bool);
    ADD_TEST("Test Vector(byte) (add())", test_vector_byte_add);
    ADD_TEST("Test Threading...", test_thread);
    ADD_TEST("Test Bus...", test_bus);
    ADD_TEST("Test Forever Stack Leak", test_forever_stack_leak);
    ADD_TEST("Test Nested Scope Conditional Return", test_nested_scope_conditional_return);
    ADD_TEST("Test Nested For Loop", test_nested_for_loop);
    ADD_TEST("Test Type Chaining", test_type_chaining);
    ADD_TEST("Test Conditional Scoping (func)", test_conditional_scoped_var_fn);
    ADD_TEST("Test Conditional Scoping (forever)", test_conditional_scoped_var_forever);
    ADD_TEST("Test Conditional Scoping (for)", test_conditional_scoped_var_for);
    ADD_TEST("Test String Interpolation * Long Print", test_print_interp_len);
    ADD_TEST("Test Enum String Representation", test_enum_string_repl);
    ADD_TEST("Test Enum Iteration", test_enum_iter);
    ADD_TEST("Test Type Inference (type())", test_type_infer);
    ADD_TEST("Test Object Orientation", test_oo);

}

#endif //MYLO_TEST_GENERATE_LIST_H
