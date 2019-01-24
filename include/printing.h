#pragma once

/*
 * Support for printing out to stdout/stderr/elsewhere -- functions to use
 * instead of printf, etc.
 *
 */

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <string>

#include "compat.h"
#include "printing_fns.h"

using namespace std::string_literals;

#define DEBUG 0

// ============================================================
// Default printing functions
// ============================================================

static void print_message_to_stdout(const char* message)
{
#if DEBUG
    fputs("1>>", stdout);
#endif
    (void)fputs(message, stdout);
}
static void print_message_to_stderr(const char* message)
{
#if DEBUG
    fputs("2>>", stderr);
#endif
    (void)fputs(message, stderr);
}
static void fprint_message_to_stdout(const char* format, va_list arg_ptr)
{
#if DEBUG
    fputs("3>>", stdout);
#endif
    (void)vfprintf(stdout, format, arg_ptr);
}
static void fprint_message_to_stderr(const char* format, va_list arg_ptr)
{
#if DEBUG
    fputs("4>>", stderr);
#endif
    (void)vfprintf(stderr, format, arg_ptr);
}
static void flush_stdout(void) { (void)fflush(stdout); }

// ============================================================
// Print redirection defaults to all output going to stdout
// ============================================================

struct print_fns {
    void (*print_message_fn)(const char* message);
    void (*print_error_fn)(const char* message);

    void (*fprint_message_fn)(const char* format, va_list arg_ptr);
    void (*fprint_error_fn)(const char* format, va_list arg_ptr);

    void (*flush_message_fn)(void);
};

static struct print_fns fns = { print_message_to_stdout, print_message_to_stdout,
    fprint_message_to_stdout, fprint_message_to_stdout, flush_stdout };

#if DEBUG
static void report_fns(const std::string why)
{
    printf("Printing bound to (%s) m:%p, e:%p, fm:%p, fe:%p\n", why.c_str(), fns.print_message_fn,
        fns.print_error_fn, fns.fprint_message_fn, fns.fprint_error_fn);
}
#endif

// ============================================================
// Functions for printing
// ============================================================
/*
 * Prints the given string, as a normal message.
 */
void print_msg(const std::string text)
{
#if DEBUG
    printf("m:%p %s", fns.print_message_fn, text);
    report_fns("m"s);
#endif
    fns.print_message_fn(text.c_str());
}

/*
 * Prints the given string, as an error message.
 */
void print_err(const std::string text)
{
#if DEBUG
    printf("e:%p %s", fns.print_error_fn, text);
    report_fns("e"s);
#endif
    fns.print_error_fn(text.c_str());
}

/*
 * Prints the given format text, as a normal message.
 */
void fprint_msg(const char* format, ...)
{
    va_list va_arg;
    va_start(va_arg, format);
#if DEBUG
    printf("fm:%p %s", fns.fprint_message_fn, format);
    report_fns("fm"s);
#endif
    fns.fprint_message_fn(format, va_arg);
    va_end(va_arg);
}

/*
 * Prints the given formatted text, as an error message.
 */
void fprint_err(const char* format, ...)
{
    va_list va_arg;
    va_start(va_arg, format);
#if DEBUG
    printf("fe:%p %s", fns.fprint_error_fn, format);
    report_fns("fe"s);
#endif
    fns.fprint_error_fn(format, va_arg);
    va_end(va_arg);
}

/*
 * Prints the given formatted text, as a normal or error message.
 * If `is_msg`, then as a normal message, else as an error
 */
void fprint_msg_or_err(bool is_msg, const char* format, ...)
{
    va_list va_arg;
    va_start(va_arg, format);
    if (is_msg) {
#if DEBUG
        printf("?m:%p %s", fns.fprint_message_fn, format);
        report_fns("?m"s);
#endif
        fns.fprint_message_fn(format, va_arg);
    } else {
#if DEBUG
        printf("?e:%p %s", fns.fprint_error_fn, format);
        report_fns("?e"s);
#endif
        fns.fprint_error_fn(format, va_arg);
    }
    va_end(va_arg);
}
/*
 * Prints the given string, as a normal message.
 */
void flush_msg(void) { fns.flush_message_fn(); }

// ============================================================
// Choosing what the printing functions do
// ============================================================

/*
 * Calling this causes errors to go to stderr, and all other output
 * to go to stdout. This is the "traditional" mechanism used by
 * Unices.
 */
void redirect_output_stderr(void)
{
    fns.print_message_fn = &print_message_to_stdout;
    fns.print_error_fn = &print_message_to_stderr;
    fns.fprint_message_fn = &fprint_message_to_stdout;
    fns.fprint_error_fn = &fprint_message_to_stderr;
    fns.flush_message_fn = &flush_stdout;

#if DEBUG
    report_fns("traditional"s);
#endif
}

/*
 * Calling this causes all output to go to stdout. This is simpler,
 * and is likely to be more use to most users.
 *
 * This is the default state.
 */
void redirect_output_stdout(void)
{
    fns.print_message_fn = &print_message_to_stdout;
    fns.print_error_fn = &print_message_to_stdout;
    fns.fprint_message_fn = &fprint_message_to_stdout;
    fns.fprint_error_fn = &fprint_message_to_stdout;
    fns.flush_message_fn = &flush_stdout;

#if DEBUG
    report_fns("stdout"s);
#endif
}

/*
 * This allows the user to specify a set of functions to use for
 * formatted printing and non-formatted printing of errors and
 * other messages.
 *
 * It is up to the caller to ensure that all of the functions
 * make sense. All four functions must be specified.
 *
 * * `new_print_message_fn` takes a string and prints it out to the "normal"
 *    output stream.
 * * `new_print_error_fn` takes a string and prints it out to the error output
 *    stream.
 * * `new_fprint_message_fn` takes a printf-style format string and the
 *    appropriate arguments, and writes the result out to the "normal" output.
 * * `new_fprint_error_fn` takes a printf-style format string and the
 *    appropriate arguments, and writes the result out to the "error" output.
 * * `new_flush_msg_fn` flushes the "normal" message output.
 *
 * Returns 0 if all goes well, 1 if something goes wrong.
 */
int redirect_output(void (*new_print_message_fn)(const char* message),
    void (*new_print_error_fn)(const char* message),
    void (*new_fprint_message_fn)(const char* format, va_list arg_ptr),
    void (*new_fprint_error_fn)(const char* format, va_list arg_ptr),
    void (*new_flush_msg_fn)(void))
{
    if (new_print_message_fn == nullptr || new_print_error_fn == nullptr
        || new_fprint_message_fn == nullptr || new_fprint_error_fn == nullptr
        || new_flush_msg_fn == nullptr)
        return 1;

    fns.print_message_fn = new_print_message_fn;
    fns.print_error_fn = new_print_error_fn;
    fns.fprint_message_fn = new_fprint_message_fn;
    fns.fprint_error_fn = new_fprint_error_fn;
    fns.flush_message_fn = new_flush_msg_fn;

#if DEBUG
    report_fns("specific"s);
#endif

    return 0;
}

void test_C_printing(void)
{
    std::cout << "C Message" << std::endl;
    std::cerr << "C Error" << std::endl;
    fprint_msg("C Message %s\n", "Fred");
    fprint_err("C Error %s\n", "Fred");
}
